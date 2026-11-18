#include "client/selftest/SelfTests.hpp"
#include <string_view>
#include "util/TaskGroup.hpp"
#include "client/systems/diagnostics/SampleProfiler.hpp"
#include "util/Relaunch.hpp"
#include "client/platform/EmbeddedProvider.hpp"
#include "client/application/Perlin.hpp"
#include "client/systems/renderer/chunk/MeshBench.hpp"
#include "client/services/discord/DiscordRpc.hpp"
#include "client/platform/sdl/AppPlatform_SDL.hpp"
#include "util/GraphicsDebugger.hpp"
#include "util/Paths.hpp"

#include <SDL3/SDL_main.h>
#include "client/services/update/Updater.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#if defined(__GLIBC__)
#include <csignal>
#include <cstring>
#include <execinfo.h>
#include <fcntl.h>
#include <malloc.h>
#endif
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <DbgHelp.h>
#endif

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#else
#include <unistd.h>
#endif
#include <chrono>
#include <ctime>
#include <thread>

namespace {
    std::unique_ptr<perlin::Application> MakeApplication(const perlin::LaunchArgs& args) {
        return std::make_unique<perlin::Perlin>(args);
    }

    void BootMark(const char* what) {
        const std::string dir = perlin::paths::DataDir();
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
        if (FILE* f = std::fopen((dir + "/boot.log").c_str(), "ab")) {
            const std::time_t now = std::time(nullptr);
            char stamp[64];
            std::strftime(stamp, sizeof(stamp), "%Y-%m-%d %H:%M:%S", std::localtime(&now));
            std::fprintf(f, "%s %s%s\n", stamp, what, perlin::gfxdebug::Attached() ? " (graphics debugger attached)" : "");
            std::fclose(f);
        }
    }

    struct BootMarker {
        BootMarker() { BootMark("process started"); }
    } gBootMarker;

    void AttachParentConsole() {
#ifdef _WIN32
        HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
        DWORD mode = 0;
        const bool isConsole = out && out != INVALID_HANDLE_VALUE && GetConsoleMode(out, &mode);
        if (isConsole && AttachConsole(ATTACH_PARENT_PROCESS)) {
            FILE* f = nullptr;
            freopen_s(&f, "CONOUT$", "w", stdout);
            freopen_s(&f, "CONOUT$", "w", stderr);
        }
#endif
        setvbuf(stdout, nullptr, _IONBF, 0);
    }

    void StartLogCapture(const std::vector<std::string>& paths) {
        std::vector<FILE*> logs;
        for (const std::string& path : paths)
            if (FILE* f = std::fopen(path.c_str(), "wb")) {
                std::fputs("[perlin] log opened\n", f);
                std::fflush(f);
                logs.push_back(f);
            }
        if (logs.empty())
            return;
        std::fflush(stdout);
        std::fflush(stderr);
#ifdef _WIN32
        for (FILE* f : logs)
            std::fclose(f);
        const std::string& primary = paths.front();
        if (!std::freopen(primary.c_str(), "ab", stdout) || !std::freopen(primary.c_str(), "ab", stderr))
            return;
        setvbuf(stdout, nullptr, _IONBF, 0);
        setvbuf(stderr, nullptr, _IONBF, 0);
        std::ios::sync_with_stdio(true);
        return;
#else
        const int saved = dup(1);
        int fds[2];
        if (pipe(fds) != 0)
            return;
        dup2(fds[1], 1);
        dup2(fds[1], 2);
        close(fds[1]);
        const int readFd = fds[0];
        setvbuf(stdout, nullptr, _IONBF, 0);
        setvbuf(stderr, nullptr, _IONBF, 0);
        std::thread([logs, saved, readFd] {
            char buf[8192];
            for (;;) {
#ifdef _WIN32
                const int n = _read(readFd, buf, sizeof(buf));
#else
                const ssize_t n = read(readFd, buf, sizeof(buf));
#endif
                if (n <= 0)
                    break;
                for (FILE* log : logs) {
                    std::fwrite(buf, 1, static_cast<size_t>(n), log);
                    std::fflush(log);
                }
                if (saved >= 0) {
#ifdef _WIN32
                    (void)_write(saved, buf, static_cast<unsigned>(n));
#else
                    (void)!write(saved, buf, static_cast<size_t>(n));
#endif
                }
            }
        }).detach();
        std::atexit([] {
            std::fflush(stdout);
            std::this_thread::sleep_for(std::chrono::milliseconds(80));
        });
#endif
    }

#ifdef _WIN32
    void PrintFrame(HANDLE process, DWORD64 address) {
        char symbolBuffer[sizeof(SYMBOL_INFO) + 256] = {};
        SYMBOL_INFO* symbol = reinterpret_cast<SYMBOL_INFO*>(symbolBuffer);
        symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
        symbol->MaxNameLen = 255;
        DWORD64 displacement = 0;
        IMAGEHLP_MODULE64 module = {};
        module.SizeOfStruct = sizeof(module);
        const bool haveModule = SymGetModuleInfo64(process, address, &module) != FALSE;
        const unsigned long long rva = haveModule ? address - module.BaseOfImage : address;
        if (SymFromAddr(process, address, &displacement, symbol)) {
            IMAGEHLP_LINE64 line = {};
            line.SizeOfStruct = sizeof(line);
            DWORD lineDisplacement = 0;
            if (SymGetLineFromAddr64(process, address, &lineDisplacement, &line))
                std::printf("[crash]   %s+0x%llx (%s:%lu) [%s+0x%llx]\n", symbol->Name,
                            static_cast<unsigned long long>(displacement), line.FileName, line.LineNumber,
                            haveModule ? module.ModuleName : "?", rva);
            else
                std::printf("[crash]   %s+0x%llx [%s+0x%llx]\n", symbol->Name,
                            static_cast<unsigned long long>(displacement),
                            haveModule ? module.ModuleName : "?", rva);
        } else {
            std::printf("[crash]   %p [%s+0x%llx]\n", reinterpret_cast<void*>(address),
                        haveModule ? module.ModuleName : "?", rva);
        }
    }

    LONG WINAPI CrashFilter(EXCEPTION_POINTERS* info) {
        const EXCEPTION_RECORD* record = info->ExceptionRecord;
        std::printf("[crash] unhandled exception 0x%08lx at %p (thread %lu)\n", record->ExceptionCode,
                    record->ExceptionAddress, GetCurrentThreadId());
        if (record->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && record->NumberParameters >= 2)
            std::printf("[crash] access violation: %s address %p\n",
                        record->ExceptionInformation[0] == 0   ? "reading"
                        : record->ExceptionInformation[0] == 1 ? "writing"
                                                               : "executing",
                        reinterpret_cast<void*>(record->ExceptionInformation[1]));
        const HANDLE process = GetCurrentProcess();
        SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS);
        SymInitialize(process, nullptr, TRUE);
        CONTEXT context = *info->ContextRecord;
        STACKFRAME64 frame = {};
#if defined(_M_ARM64) || defined(__aarch64__)
        frame.AddrPC.Offset = context.Pc;
        frame.AddrFrame.Offset = context.Fp;
        frame.AddrStack.Offset = context.Sp;
        constexpr DWORD kMachine = IMAGE_FILE_MACHINE_ARM64;
#else
        frame.AddrPC.Offset = context.Rip;
        frame.AddrFrame.Offset = context.Rbp;
        frame.AddrStack.Offset = context.Rsp;
        constexpr DWORD kMachine = IMAGE_FILE_MACHINE_AMD64;
#endif
        frame.AddrPC.Mode = frame.AddrFrame.Mode = frame.AddrStack.Mode = AddrModeFlat;
        for (int i = 0; i < 40; i++) {
            PrintFrame(process, frame.AddrPC.Offset);
            if (!StackWalk64(kMachine, process, GetCurrentThread(), &frame, &context, nullptr,
                             SymFunctionTableAccess64, SymGetModuleBase64, nullptr) ||
                frame.AddrPC.Offset == 0)
                break;
        }
        std::fflush(stdout);
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        return EXCEPTION_CONTINUE_SEARCH;
    }
#endif

    perlin::LaunchArgs ParseArgs(int argc, char** argv) {
        perlin::LaunchArgs args;
        if (std::getenv("PERLIN_PREVIEW_SHOT") && !std::getenv("PERLIN_PREVIEW_VISIBLE"))
            args.hidden = true;
        for (int i = 1; i < argc; i++) {
            const std::string arg = argv[i];

            if (arg.starts_with("perlin://")) {
                args.server = perlin::discord::ServerFromUrl(arg);
                continue;
            }
            if (arg.starts_with("discord-"))
                continue;
            if (arg == "--join" && i + 1 < argc)
                args.server = argv[++i];
            else if (arg == "--name" && i + 1 < argc)
                args.playerName = argv[++i];
            else if (arg == "--skin" && i + 1 < argc)
                args.skin = argv[++i];
            else if (arg == "--cape" && i + 1 < argc)
                args.cape = argv[++i];
            else if (arg == "--slim")
                args.slimArms = true;
            else if (arg == "--login")
                args.login = true;
            else if (arg == "--offline")
                args.offline = true;
            else if (arg == "--hidden")
                args.hidden = true;
            else if (arg == "--control-port" && i + 1 < argc) {
                const std::string port = argv[++i];
                char* end = nullptr;
                const long value = std::strtol(port.c_str(), &end, 10);
                if (!end || *end || value < 1024 || value > 65535) {
                    std::fprintf(stderr, "Invalid control port (expected 1024..65535)\n");
                    std::exit(2);
                }
                args.controlPort = static_cast<int>(value);
            } else if (arg == "--raknet")
                args.transport = "raknet";
            else if (arg == "--nethernet")
                args.transport = "nethernet";
            else if (arg == "--logout")
                args.logout = true;
            else if (arg == "--tls")
                args.secure = true;
            else if (arg == "--accept-eula")
                args.acceptEula = true;
            else if (arg == "--stun" && i + 1 < argc)
                args.stun = argv[++i];
            else if (arg == "--benchmark") {
                args.benchmarkSeconds = 60.0;
                if (i + 1 < argc && std::isdigit(static_cast<unsigned char>(argv[i + 1][0])))
                    args.benchmarkSeconds = std::max(5.0, std::atof(argv[++i]));
            } else if (arg == "--benchmark-seed" && i + 1 < argc)
                args.benchmarkSeed = std::strtoull(argv[++i], nullptr, 10);
            else if (arg == "--benchmark-out" && i + 1 < argc)
                args.benchmarkOut = argv[++i];
            else if (arg == "--help" || arg == "-h")
                std::printf(
                    "Perlin options:\n"
                    "  --join <host:port>   server to join\n"
                    "  --name <name>        offline player name\n"
                    "  --skin <png>         skin image (64x32, 64x64 or 128x128)\n"
                    "  --cape <png>         cape image (64x32)\n"
                    "  --slim               slim (Alex) arms for the skin\n"
                    "  --login              sign in with a Microsoft/Xbox account (remembered afterwards)\n"
                    "  --offline            skip the saved Microsoft login for this run\n"
                    "  --hidden             run without showing or focusing the game window\n"
                    "  --control-port <n>   enable localhost developer HTTP controls\n"
                    "  --logout             forget the saved Microsoft login\n"
                    "  --raknet             connect over RakNet (Lifeboat, The Hive, any public server)\n"
                    "  --nethernet          connect over NetherNet (BDS with the nethernet transport)\n"
                    "  --tls                use https for the NetherNet signaling requests\n"
                    "  --stun <urls>        comma separated STUN servers for NetherNet\n"
                    "  --benchmark [secs]   fly through a fixed world, write the frame times and quit\n"
                    "  --benchmark-seed <n> world seed for the benchmark (default 8675309)\n"
                    "  --benchmark-out <f>  where the benchmark json goes (default <data>/benchmarks)\n"
                    "  --renderdoc          load RenderDoc in-process (F12 captures, attach from the RenderDoc UI)\n");
        }
        return args;
    }
}

#if defined(__GLIBC__)
#include <atomic>
#include <dlfcn.h>
#include <elf.h>
#include <sys/mman.h>
#include <sys/stat.h>
namespace {
    struct CrashSymbol {
        uintptr_t addr;
        uintptr_t size;
        const char* name;
    };
    std::atomic<std::vector<CrashSymbol>*> gCrashSymbols{nullptr};
    uintptr_t gImageBase = 0;

    void LoadCrashSymbols() {
        Dl_info info{};
        if (!dladdr(reinterpret_cast<void*>(&LoadCrashSymbols), &info) || !info.dli_fbase)
            return;
        gImageBase = reinterpret_cast<uintptr_t>(info.dli_fbase);
        const int fd = open("/proc/self/exe", O_RDONLY | O_CLOEXEC);
        if (fd < 0)
            return;
        struct stat st{};
        if (fstat(fd, &st) != 0 || st.st_size < static_cast<off_t>(sizeof(Elf64_Ehdr))) {
            close(fd);
            return;
        }
        void* map = mmap(nullptr, static_cast<size_t>(st.st_size), PROT_READ, MAP_PRIVATE, fd, 0);
        close(fd);
        if (map == MAP_FAILED)
            return;
        const auto* base = static_cast<const uint8_t*>(map);
        const size_t fileSize = static_cast<size_t>(st.st_size);
        const auto* eh = reinterpret_cast<const Elf64_Ehdr*>(base);
        if (std::memcmp(eh->e_ident, ELFMAG, SELFMAG) != 0 || eh->e_ident[EI_CLASS] != ELFCLASS64 ||
            eh->e_shoff == 0 || eh->e_shoff + static_cast<size_t>(eh->e_shnum) * sizeof(Elf64_Shdr) > fileSize) {
            munmap(map, fileSize);
            return;
        }
        const auto* sh = reinterpret_cast<const Elf64_Shdr*>(base + eh->e_shoff);
        auto symbols = std::make_unique<std::vector<CrashSymbol>>();
        for (int i = 0; i < eh->e_shnum; i++) {
            if (sh[i].sh_type != SHT_SYMTAB || sh[i].sh_link >= eh->e_shnum)
                continue;
            const Elf64_Shdr& strs = sh[sh[i].sh_link];
            if (sh[i].sh_offset + sh[i].sh_size > fileSize || strs.sh_offset + strs.sh_size > fileSize)
                continue;
            const auto* syms = reinterpret_cast<const Elf64_Sym*>(base + sh[i].sh_offset);
            const size_t count = sh[i].sh_size / sizeof(Elf64_Sym);
            const char* names = reinterpret_cast<const char*>(base + strs.sh_offset);
            for (size_t k = 0; k < count; k++)
                if (ELF64_ST_TYPE(syms[k].st_info) == STT_FUNC && syms[k].st_value != 0 &&
                    syms[k].st_name < strs.sh_size)
                    symbols->push_back({static_cast<uintptr_t>(syms[k].st_value),
                                        static_cast<uintptr_t>(syms[k].st_size), names + syms[k].st_name});
        }
        if (symbols->empty()) {
            munmap(map, fileSize);
            return;
        }
        std::sort(symbols->begin(), symbols->end(),
                  [](const CrashSymbol& a, const CrashSymbol& b) { return a.addr < b.addr; });
        gCrashSymbols.store(symbols.release(), std::memory_order_release);
    }

    void WriteHex(int fd, uintptr_t v) {
        char buf[20];
        int n = 0;
        do {
            const int d = static_cast<int>(v & 15u);
            buf[n++] = static_cast<char>(d < 10 ? '0' + d : 'a' + d - 10);
            v >>= 4;
        } while (v && n < 18);
        (void)!write(fd, "0x", 2);
        while (n > 0)
            (void)!write(fd, &buf[--n], 1);
    }

    void WriteSymbolizedFrames(int fd, void* const* frames, int depth) {
        const std::vector<CrashSymbol>* symbols = gCrashSymbols.load(std::memory_order_acquire);
        if (!symbols || !gImageBase)
            return;
        static const char head[] = "[crash] symbols:\n";
        (void)!write(fd, head, sizeof(head) - 1);
        for (int i = 0; i < depth; i++) {
            const uintptr_t at = reinterpret_cast<uintptr_t>(frames[i]);
            if (at < gImageBase)
                continue;
            const uintptr_t offset = at - gImageBase;
            size_t lo = 0, hi = symbols->size();
            while (lo < hi) {
                const size_t mid = (lo + hi) / 2;
                if ((*symbols)[mid].addr <= offset)
                    lo = mid + 1;
                else
                    hi = mid;
            }
            if (lo == 0)
                continue;
            const CrashSymbol& s = (*symbols)[lo - 1];
            if (!s.size || offset >= s.addr + s.size + 64)
                continue;
            (void)!write(fd, "  ", 2);
            WriteHex(fd, offset);
            (void)!write(fd, " ", 1);
            (void)!write(fd, s.name, std::strlen(s.name));
            (void)!write(fd, "+", 1);
            WriteHex(fd, offset - s.addr);
            (void)!write(fd, "\n", 1);
        }
    }

    char gCrashLogPath[1024];
    char gEarlyCrashPath[1024];
    time_t gStartSeconds = 0;
    void OnFatalSignal(int sig) {
        static volatile sig_atomic_t entered = 0;
        if (!entered) {
            entered = 1;
            const char* name = sig == SIGSEGV   ? "SIGSEGV"
                               : sig == SIGABRT ? "SIGABRT"
                               : sig == SIGBUS  ? "SIGBUS"
                               : sig == SIGFPE  ? "SIGFPE"
                                                : "SIGILL";
            timespec now{};
            clock_gettime(CLOCK_MONOTONIC, &now);
            if (now.tv_sec - gStartSeconds < 120) {
                const int mark = open(gEarlyCrashPath, O_WRONLY | O_APPEND | O_CREAT, 0644);
                if (mark >= 0) {
                    (void)!write(mark, "x", 1);
                    close(mark);
                }
            }
            void* frames[64];
            const int depth = backtrace(frames, 64);
            for (int pass = 0; pass < 2; pass++) {
                const int fd = pass == 0 ? open(gCrashLogPath, O_WRONLY | O_APPEND | O_CREAT, 0644) : 2;
                if (fd < 0)
                    continue;
                static const char head[] = "[crash] fatal signal ";
                (void)!write(fd, head, sizeof(head) - 1);
                (void)!write(fd, name, std::strlen(name));
                (void)!write(fd, "; stack:\n", 9);
                backtrace_symbols_fd(frames, depth, fd);
                WriteSymbolizedFrames(fd, frames, depth);
                if (pass == 0)
                    close(fd);
            }
        }
        std::signal(sig, SIG_DFL);
        raise(sig);
    }
}
#endif

int main(int argc, char** argv) {
    if (argc >= 3 && std::string_view(argv[1]) == "--selftest") {
        perlin::RegisterEmbeddedResources();
        return perlin::selftest::Run(argv[2]);
    }
#if defined(__GLIBC__)
    mallopt(M_ARENA_MAX, 2);
    mallopt(M_MMAP_THRESHOLD, 256 * 1024);
    mallopt(M_TRIM_THRESHOLD, 512 * 1024);
#endif
    BootMark("main reached");
    if (std::getenv("PERLIN_SAMPLE_ALL"))
        perlin::StartSampleProfiler();
    perlin::RememberCommandLine(argc, argv);
    AttachParentConsole();
    const std::string dataDir = perlin::paths::DataDir();
    perlin::paths::EnsureDir(dataDir);
#ifndef __EMSCRIPTEN__
    StartLogCapture({dataDir + "/perlin.log"});
#endif
#ifdef _WIN32
    SetUnhandledExceptionFilter(CrashFilter);
#endif
#if defined(__GLIBC__)
    std::snprintf(gCrashLogPath, sizeof(gCrashLogPath), "%s/perlin.log", dataDir.c_str());
    std::snprintf(gEarlyCrashPath, sizeof(gEarlyCrashPath), "%s/gpu-early-crash", dataDir.c_str());
    {
        timespec started{};
        clock_gettime(CLOCK_MONOTONIC, &started);
        gStartSeconds = started.tv_sec;
    }
    {
        void* warm[4];
        backtrace(warm, 4);
    }
    for (int sig : {SIGSEGV, SIGABRT, SIGBUS, SIGFPE, SIGILL})
        std::signal(sig, OnFatalSignal);
    std::thread(LoadCrashSymbols).detach();
#endif
    std::printf("[boot] exe dir %s\n", perlin::paths::ExeDir().c_str());
    if (perlin::gfxdebug::WantsRenderDoc(argc, argv)) {
        perlin::paths::EnsureDir(dataDir + "/captures");
        perlin::gfxdebug::LoadRenderDoc(dataDir + "/captures");
    }
    if (const char* tool = perlin::gfxdebug::Attached())
        std::printf("[perlin] %s is attached: single device, no pipeline cache, no failure records\n", tool);
    perlin::RegisterEmbeddedResources();
    for (int i = 1; i < argc; i++)
        if (std::string(argv[i]) == "--uibench" && i + 1 < argc)
            return perlin::RunUiBench(argv[i + 1], i + 2 < argc ? std::atoi(argv[i + 2]) : 3);
        else if (std::string(argv[i]) == "--meshbench")
            return perlin::RunMeshBench(i + 1 < argc ? std::atoi(argv[i + 1]) : 6, i + 2 < argc ? std::atoi(argv[i + 2]) : 3);
#ifndef __EMSCRIPTEN__

    perlin::ProcessTasks().Run([] {
        const std::string exe = perlin::paths::ExePath();
        perlin::discord::RegisterUrlScheme("perlin", exe);
        perlin::discord::RegisterLaunch("1568359857915039784", exe);
    });
#endif
    std::printf("[boot] embedded resources registered\n");
    perlin::update::OnStartup();
    perlin::paths::EnsureDir(dataDir + "/texturepacks");
    perlin::paths::EnsureDir(dataDir + "/shaderpacks");
    {
        const std::time_t now = std::time(nullptr);
        char stamp[64];
        std::strftime(stamp, sizeof(stamp), "%Y-%m-%d %H:%M:%S", std::localtime(&now));
        std::printf("[perlin] starting %s, data dir %s, log %s/perlin.log\n", stamp, dataDir.c_str(),
                    dataDir.c_str());
    }

#ifdef __EMSCRIPTEN__
    auto* platform = new AppPlatform_SDL(ParseArgs(argc, argv), MakeApplication);
    if (!platform->Initialize())
        return 1;
    return 0;
#else
    AppPlatform_SDL platform(ParseArgs(argc, argv), MakeApplication);
    std::printf("[boot] starting the window and renderer\n");
    BootMark("starting the window and renderer");
    if (!platform.Initialize()) {
        BootMark("startup failed");
        std::printf("[boot] startup failed\n");
        return 1;
    }
    BootMark("running");

    platform.Run();
    platform.Shutdown();
    perlin::ProcessTasks().Shutdown();
    perlin::update::OnExit();
    return 0;
#endif
}
