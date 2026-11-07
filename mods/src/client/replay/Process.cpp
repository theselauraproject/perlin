#include "client/replay/Process.hpp"

#include <algorithm>
#include <cstring>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <cerrno>
#include <sys/types.h>
#include <csignal>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace perlin::replay {
    namespace {
        constexpr size_t kOutputCap = 64 * 1024;
    }

    struct Process::Impl {
        std::mutex outMutex;
        std::string out;
        std::thread reader;
        bool started = false;
        bool waited = false;
        int exitCode = -1;
#ifdef _WIN32
        HANDLE process = nullptr;
        HANDLE stdinWrite = nullptr;
        HANDLE outRead = nullptr;
#else
        pid_t pid = -1;
        int stdinFd = -1;
        int outFd = -1;
#endif
        void Append(const char* data, size_t n) {
            std::lock_guard<std::mutex> lk(outMutex);
            out.append(data, n);
            if (out.size() > kOutputCap)
                out.erase(0, out.size() - kOutputCap);
        }
    };

    Process::Process() : impl(std::make_unique<Impl>()) {}

    Process::~Process() {
        if (impl->started && !impl->waited) {
            Kill();
            Wait();
        }
    }

    std::string Process::Output() {
        std::lock_guard<std::mutex> lk(impl->outMutex);
        return impl->out;
    }

#ifdef _WIN32
    namespace {
        std::wstring Widen(const std::string& s) {
            if (s.empty())
                return {};
            const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
            std::wstring w(static_cast<size_t>(n), L'\0');
            MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
            return w;
        }

        std::wstring Quote(const std::wstring& arg) {
            if (!arg.empty() && arg.find_first_of(L" \t\n\v\"") == std::wstring::npos)
                return arg;
            std::wstring out = L"\"";
            size_t backslashes = 0;
            for (wchar_t c : arg) {
                if (c == L'\\') {
                    backslashes++;
                } else if (c == L'"') {
                    out.append(backslashes * 2 + 1, L'\\');
                    out += L'"';
                    backslashes = 0;
                } else {
                    out.append(backslashes, L'\\');
                    out += c;
                    backslashes = 0;
                }
            }
            out.append(backslashes * 2, L'\\');
            out += L'"';
            return out;
        }
    }

    bool Process::Start(const std::vector<std::string>& argv, bool pipeStdin, std::string& error) {
        if (argv.empty()) {
            error = "no command";
            return false;
        }
        SECURITY_ATTRIBUTES sa{};
        sa.nLength = sizeof sa;
        sa.bInheritHandle = TRUE;
        HANDLE outRead = nullptr, outWrite = nullptr, inRead = nullptr, inWrite = nullptr;
        if (!CreatePipe(&outRead, &outWrite, &sa, 0)) {
            error = "CreatePipe failed";
            return false;
        }
        SetHandleInformation(outRead, HANDLE_FLAG_INHERIT, 0);
        if (pipeStdin) {
            if (!CreatePipe(&inRead, &inWrite, &sa, 1 << 20)) {
                CloseHandle(outRead);
                CloseHandle(outWrite);
                error = "CreatePipe failed";
                return false;
            }
            SetHandleInformation(inWrite, HANDLE_FLAG_INHERIT, 0);
        }
        STARTUPINFOW si{};
        si.cb = sizeof si;
        si.dwFlags = STARTF_USESTDHANDLES;
        si.hStdOutput = outWrite;
        si.hStdError = outWrite;
        si.hStdInput = pipeStdin ? inRead : GetStdHandle(STD_INPUT_HANDLE);
        std::wstring cmd;
        for (size_t i = 0; i < argv.size(); i++) {
            if (i)
                cmd += L' ';
            cmd += Quote(Widen(argv[i]));
        }
        PROCESS_INFORMATION pi{};
        const BOOL ok = CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr,
                                       nullptr, &si, &pi);
        CloseHandle(outWrite);
        if (inRead)
            CloseHandle(inRead);
        if (!ok) {
            error = "could not start " + argv[0] + " (error " + std::to_string(GetLastError()) + ")";
            CloseHandle(outRead);
            if (inWrite)
                CloseHandle(inWrite);
            return false;
        }
        CloseHandle(pi.hThread);
        impl->process = pi.hProcess;
        impl->stdinWrite = inWrite;
        impl->outRead = outRead;
        impl->started = true;
        Impl* raw = impl.get();
        impl->reader = std::thread([raw] {
            char buf[4096];
            DWORD n = 0;
            while (ReadFile(raw->outRead, buf, sizeof buf, &n, nullptr) && n > 0)
                raw->Append(buf, n);
        });
        return true;
    }

    bool Process::Running() {
        if (!impl->started || impl->waited)
            return false;
        return WaitForSingleObject(impl->process, 0) == WAIT_TIMEOUT;
    }

    bool Process::WriteStdin(const void* data, size_t size) {
        if (!impl->stdinWrite)
            return false;
        const char* p = static_cast<const char*>(data);
        while (size > 0) {
            DWORD written = 0;
            const DWORD chunk = static_cast<DWORD>(std::min<size_t>(size, 1u << 20));
            if (!WriteFile(impl->stdinWrite, p, chunk, &written, nullptr) || written == 0)
                return false;
            p += written;
            size -= written;
        }
        return true;
    }

    void Process::CloseStdin() {
        if (impl->stdinWrite) {
            CloseHandle(impl->stdinWrite);
            impl->stdinWrite = nullptr;
        }
    }

    int Process::Wait() {
        if (!impl->started)
            return -1;
        if (impl->waited)
            return impl->exitCode;
        CloseStdin();
        WaitForSingleObject(impl->process, INFINITE);
        DWORD code = 0;
        impl->exitCode = GetExitCodeProcess(impl->process, &code) ? static_cast<int>(code) : -1;
        CloseHandle(impl->process);
        impl->process = nullptr;
        if (impl->reader.joinable())
            impl->reader.join();
        CloseHandle(impl->outRead);
        impl->outRead = nullptr;
        impl->waited = true;
        return impl->exitCode;
    }

    void Process::Kill() {
        if (impl->started && !impl->waited && impl->process)
            TerminateProcess(impl->process, 1);
    }
#elif defined(__EMSCRIPTEN__)
    bool Process::Start(const std::vector<std::string>&, bool, std::string& error) {
        error = "starting programs is not supported on this platform";
        return false;
    }
    bool Process::Running() {
        return false;
    }
    bool Process::WriteStdin(const void*, size_t) {
        return false;
    }
    void Process::CloseStdin() {}
    int Process::Wait() {
        return -1;
    }
    void Process::Kill() {}
#else
    bool Process::Start(const std::vector<std::string>& argv, bool pipeStdin, std::string& error) {
        if (argv.empty()) {
            error = "no command";
            return false;
        }
        static bool sigpipeIgnored = [] {
            std::signal(SIGPIPE, SIG_IGN);
            return true;
        }();
        (void)sigpipeIgnored;
        int outPipe[2] = {-1, -1}, inPipe[2] = {-1, -1};
        if (pipe(outPipe) != 0) {
            error = "pipe failed";
            return false;
        }
        if (pipeStdin && pipe(inPipe) != 0) {
            close(outPipe[0]);
            close(outPipe[1]);
            error = "pipe failed";
            return false;
        }
        std::vector<char*> args;
        args.reserve(argv.size());
        for (const std::string& a : argv)
            args.push_back(const_cast<char*>(a.c_str()));
        args.push_back(nullptr);
        const pid_t pid = fork();
        if (pid < 0) {
            error = "fork failed";
            return false;
        }
        if (pid == 0) {
            if (pipeStdin) {
                dup2(inPipe[0], 0);
                close(inPipe[0]);
                close(inPipe[1]);
            }
            dup2(outPipe[1], 1);
            dup2(outPipe[1], 2);
            close(outPipe[0]);
            close(outPipe[1]);
            execvp(args[0], args.data());
            _exit(127);
        }
        close(outPipe[1]);
        if (pipeStdin)
            close(inPipe[0]);
        impl->pid = pid;
        impl->outFd = outPipe[0];
        impl->stdinFd = pipeStdin ? inPipe[1] : -1;
        impl->started = true;
        Impl* raw = impl.get();
        impl->reader = std::thread([raw] {
            char buf[4096];
            for (;;) {
                const ssize_t n = read(raw->outFd, buf, sizeof buf);
                if (n <= 0)
                    break;
                raw->Append(buf, static_cast<size_t>(n));
            }
        });
        return true;
    }

    bool Process::Running() {
        if (!impl->started || impl->waited)
            return false;
        int status = 0;
        const pid_t r = waitpid(impl->pid, &status, WNOHANG);
        if (r == 0)
            return true;
        if (r == impl->pid) {
            impl->exitCode = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
            impl->waited = true;
            CloseStdin();
            if (impl->reader.joinable())
                impl->reader.join();
            close(impl->outFd);
            impl->outFd = -1;
        }
        return false;
    }

    bool Process::WriteStdin(const void* data, size_t size) {
        if (impl->stdinFd < 0)
            return false;
        const char* p = static_cast<const char*>(data);
        while (size > 0) {
            const ssize_t n = write(impl->stdinFd, p, size);
            if (n < 0) {
                if (errno == EINTR)
                    continue;
                return false;
            }
            p += n;
            size -= static_cast<size_t>(n);
        }
        return true;
    }

    void Process::CloseStdin() {
        if (impl->stdinFd >= 0) {
            close(impl->stdinFd);
            impl->stdinFd = -1;
        }
    }

    int Process::Wait() {
        if (!impl->started)
            return -1;
        if (impl->waited)
            return impl->exitCode;
        CloseStdin();
        int status = 0;
        while (waitpid(impl->pid, &status, 0) < 0 && errno == EINTR) {}
        impl->exitCode = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
        if (impl->reader.joinable())
            impl->reader.join();
        close(impl->outFd);
        impl->outFd = -1;
        impl->waited = true;
        return impl->exitCode;
    }

    void Process::Kill() {
        if (impl->started && !impl->waited && impl->pid > 0)
            kill(impl->pid, SIGKILL);
    }
#endif

    std::string RunCapture(const std::vector<std::string>& argv, int* exitCode) {
        Process p;
        std::string error;
        if (!p.Start(argv, false, error)) {
            if (exitCode)
                *exitCode = -1;
            return {};
        }
        const int code = p.Wait();
        if (exitCode)
            *exitCode = code;
        return p.Output();
    }
}
