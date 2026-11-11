// dedicated server: perlin-server [--data <dir>] [--port <n>] [--config-json '{...}']
#include "core/Config.hpp"
#include "core/Log.hpp"
#include "core/Server.hpp"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#include <atomic>
#include <csignal>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <string>
#include <thread>

using namespace perlin::server;

namespace {
    std::atomic<Server*> g_server{nullptr};

    void OnSignal(int) {
        if (Server* s = g_server.load())
            s->RequestStop();
    }
}

int main(int argc, char** argv) {
#ifdef _WIN32
    // player names and plugin output are utf-8
    SetConsoleOutputCP(CP_UTF8);
#endif
    std::string dir = ".";
    int port = 0;
    for (int i = 1; i < argc; i++) {
        const std::string a = argv[i];
        if (a == "--data" && i + 1 < argc)
            dir = argv[++i];
        else if (a == "--port" && i + 1 < argc)
            port = std::atoi(argv[++i]);
        else if (a == "--help" || a == "-h") {
            std::cout << "perlin-server [--data <dir>] [--port <n>]\n"
                         "  reads <dir>/server.properties (written with defaults on first run; the bds file "
                         "format), allowlist.json,\n"
                         "  permissions.json and plugins from <dir>/plugins\n";
            return 0;
        }
    }
    std::filesystem::create_directories(dir);
    // bds reads server.properties from its working directory; a server.json from before that is still read when there is no .properties
    const std::string propsPath = dir + "/server.properties";
    const bool hadProps = std::filesystem::exists(propsPath);
    const bool hadLegacy = std::filesystem::exists(dir + "/server.json");
    std::string error;
    ServerConfig config = ServerConfig::LoadDirectory(dir, &error);
    if (!error.empty()) {
        LogError(error);
        return 2;
    }
    if (!hadProps && !hadLegacy) {
        // first run: a random seed, and the file written out so there is something to edit
        config.randomSeed = true;
        config.udpPortsSet = true;
        config.udpPortBegin = config.udpPortEnd = config.port;
        config.SaveProperties(propsPath);
        LogInfo("wrote default " + propsPath);
    }
    config.dataDir = dir;
    // a packaged server carries its block tables in resources/ next to the executable
    if (config.blockDataDir.empty() && argc > 0) {
        std::error_code ec;
        const std::filesystem::path beside = std::filesystem::path(argv[0]).parent_path() / "resources";
        if (std::filesystem::exists(beside / "block_states.json", ec))
            config.blockDataDir = beside.string();
    }
    if (config.pluginDir.empty())
        config.pluginDir = dir + "/plugins";
    if (port > 0)
        config.port = static_cast<uint16_t>(port);
    std::filesystem::create_directories(config.pluginDir);

    Server server(config);
    if (!server.Start(error)) {
        LogError("cannot start: " + error);
        return 1;
    }
    g_server = &server;
    std::signal(SIGINT, OnSignal);
    std::signal(SIGTERM, OnSignal);

    // console commands: a thread reads stdin; its commands run on the tick thread's behalf under the same single-threaded rules
    std::thread console;
    std::mutex lineMutex;
    std::vector<std::string> lines;
    std::atomic<bool> consoleDone{false};
    console = std::thread([&] {
        std::string line;
        while (std::getline(std::cin, line)) {
            std::lock_guard lock(lineMutex);
            lines.push_back(line);
        }
        consoleDone = true;
    });
    console.detach();

    using clock = std::chrono::steady_clock;
    auto next = clock::now();
    while (!server.StopRequested()) {
        {
            std::vector<std::string> pending;
            {
                std::lock_guard lock(lineMutex);
                pending.swap(lines);
            }
            for (const std::string& l : pending)
                server.RunConsoleCommand(l);
        }
        server.Tick();
        next += server.TickDelay();
        const auto now = clock::now();
        if (next < now)
            next = now;
        else
            std::this_thread::sleep_until(next);
    }
    LogInfo("stopping");
    g_server = nullptr;
    server.Stop();
    return 0;
}
