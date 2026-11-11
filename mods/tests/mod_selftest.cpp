#include "client/mods/ModManager.hpp"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <thread>

using namespace perlin::mods;
namespace abi = perlin::abi;
namespace fs = std::filesystem;

static int failures = 0;
#define CHECK(c)                                                                                             \
    do {                                                                                                     \
        if (!(c)) {                                                                                          \
            std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c);                                          \
            failures++;                                                                                      \
        }                                                                                                    \
    } while (0)

template <typename F> static bool WaitFor(F&& f, int ms = 8000) {
    const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    while (std::chrono::steady_clock::now() < end) {
        ModManager::Instance().Update();
        if (f())
            return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    return f();
}

static ModInfo Find(const std::string& name) {
    for (const ModInfo& info : ModManager::Instance().List())
        if (info.name == name)
            return info;
    return {};
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::printf("usage: mod_selftest <plugin.wasm>...\n");
        return 2;
    }
    const fs::path dir = fs::temp_directory_path() / "perlin_mod_selftest";
    fs::remove_all(dir);
    fs::create_directories(dir);
    const auto write = [&](const std::string& name, const std::vector<unsigned char>& data) {
        std::ofstream out(dir / name, std::ios::binary | std::ios::trunc);
        out.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
    };
    std::vector<std::string> names;
    std::vector<unsigned char> first;
    for (int i = 1; i < argc; i++) {
        std::ifstream in(argv[i], std::ios::binary);
        std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(in)), {});
        CHECK(!bytes.empty());
        const std::string name = fs::path(argv[i]).stem().string();
        write(name + ".wasm", bytes);
        names.push_back(name);
        if (first.empty())
            first = bytes;
    }
    {
        std::vector<unsigned char> broken = first;
        broken.resize(20);
        write("broken.wasm", broken);
    }

    auto& mm = ModManager::Instance();
    std::mutex lock;
    std::vector<std::string> messages;
    mm.Register(abi::Op::LocalMessage, [&](CallContext& ctx, abi::Reader& r, abi::Writer&) {
        const std::string text = r.str();
        std::lock_guard guard(lock);
        messages.push_back(ctx.plugin + ": " + text);
        return abi::Status::Ok;
    });
    mm.Start(dir.string());

    for (const std::string& name : names) {
        CHECK(WaitFor([&] { return Find(name).state == "running"; }));
        if (Find(name).state != "running")
            std::printf("  %s: %s %s\n", name.c_str(), Find(name).state.c_str(), Find(name).error.c_str());
    }
    CHECK(WaitFor([&] { return Find("broken").state == "error"; }));
    std::printf("  broken: %s\n", Find("broken").error.c_str());

    mm.Dispatch(abi::Event::Connected);
    {
        std::lock_guard guard(lock);
        for (const std::string& m : messages)
            std::printf("  message %s\n", m.c_str());
    }

    if (std::getenv("MOD_BENCH")) {
        const auto t0 = std::chrono::steady_clock::now();
        const int frames = 2000;
        for (int i = 0; i < frames; i++)
            mm.Dispatch(abi::Event::Frame, 300.0);
        std::printf("  %.1f us per frame event (all plugins)\n",
                    std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count() /
                        frames);
    }
    for (int t = 0; t < 4; t++)
        mm.Dispatch(abi::Event::Tick, 300.0);
    for (const std::string& name : names) {
        const ModInfo info = Find(name);
        const bool spins = name.starts_with("loop_");
        std::printf("  %s after ticks: %s %s\n", name.c_str(), info.state.c_str(), info.error.c_str());
        CHECK(info.state == (spins ? "unresponsive" : "running"));
    }

    const std::string reloaded = names.front();
    if (!reloaded.starts_with("loop_")) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1100));
        std::vector<unsigned char> grown = first;
        const unsigned char section[] = {0, 5, 4, 'p', 'a', 'd', '!'};
        grown.insert(grown.end(), std::begin(section), std::end(section));
        write(reloaded + ".wasm", grown);
        CHECK(WaitFor([&] { return Find(reloaded).loads == 2; }));
        CHECK(Find(reloaded).state == "running");

        mm.SetDisabled({reloaded});
        CHECK(WaitFor([&] { return Find(reloaded).state == "disabled"; }));
        mm.SetDisabled({});
        CHECK(WaitFor([&] { return Find(reloaded).state == "running"; }));

        fs::remove(dir / (reloaded + ".wasm"));
        CHECK(WaitFor([&] { return Find(reloaded).name.empty(); }));
    }

    const auto stopAt = std::chrono::steady_clock::now();
    mm.Stop();
    std::printf("  stopped in %.0f ms\n",
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - stopAt).count());
    fs::remove_all(dir);
    std::printf(failures ? "mod_selftest FAILED\n" : "mod_selftest ok\n");
    return failures ? 1 : 0;
}
