#include "util/Relaunch.hpp"
#include "client/EmbeddedProvider.hpp"
#include "client/Perlin.hpp"
#include "client/platform/sdl/AppPlatform_SDL.hpp"
#include "util/Paths.hpp"

#include <SDL3/SDL_main.h>
#include "client/update/Updater.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#if defined(__GLIBC__)
#include <malloc.h>
#endif
#include <fstream>
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
        frame.AddrPC.Offset = context.Rip;
        frame.AddrFrame.Offset = context.Rbp;
        frame.AddrStack.Offset = context.Rsp;
        frame.AddrPC.Mode = frame.AddrFrame.Mode = frame.AddrStack.Mode = AddrModeFlat;
        for (int i = 0; i < 40; i++) {
            PrintFrame(process, frame.AddrPC.Offset);
            if (!StackWalk64(IMAGE_FILE_MACHINE_AMD64, process, GetCurrentThread(), &frame, &context, nullptr,
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
        // a preview screenshot runs on its own: no window needed (PERLIN_PREVIEW_VISIBLE=1 shows it anyway)
        if (std::getenv("PERLIN_PREVIEW_SHOT") && !std::getenv("PERLIN_PREVIEW_VISIBLE"))
            args.hidden = true;
        for (int i = 1; i < argc; i++) {
            const std::string arg = argv[i];
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
                    "  --benchmark-out <f>  where the benchmark json goes (default <data>/benchmarks)\n");
        }
        return args;
    }
}

int main(int argc, char** argv) {
#if defined(__GLIBC__)
    mallopt(M_ARENA_MAX, 2);
    mallopt(M_MMAP_THRESHOLD, 256 * 1024);
    mallopt(M_TRIM_THRESHOLD, 512 * 1024);
#endif
    perlin::RememberCommandLine(argc, argv);
    AttachParentConsole();
    perlin::RegisterEmbeddedResources();
    perlin::update::OnStartup();

    const std::string dataDir = perlin::paths::DataDir();
    perlin::paths::EnsureDir(dataDir);
    perlin::paths::EnsureDir(dataDir + "/texturepacks");
    perlin::paths::EnsureDir(dataDir + "/shaderpacks");
#ifndef __EMSCRIPTEN__
    StartLogCapture({dataDir + "/perlin.log"});
#endif
#ifdef _WIN32
    SetUnhandledExceptionFilter(CrashFilter);
#endif
    {
        const std::time_t now = std::time(nullptr);
        char stamp[64];
        std::strftime(stamp, sizeof(stamp), "%Y-%m-%d %H:%M:%S", std::localtime(&now));
        std::printf("[perlin] starting %s, data dir %s, log %s/perlin.log\n", stamp, dataDir.c_str(),
                    dataDir.c_str());
    }

#ifdef __EMSCRIPTEN__
    auto* platform = new AppPlatform_SDL(ParseArgs(argc, argv));
    if (!platform->Initialize())
        return 1;
    return 0;
#else
    AppPlatform_SDL platform(ParseArgs(argc, argv));
    if (!platform.Initialize())
        return 1;

    platform.Run();
    platform.Shutdown();
    perlin::update::OnExit();
    return 0;
#endif
}
