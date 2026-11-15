#include "network/AtomicFile.hpp"

#include <atomic>
#include <fstream>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace perlin::auth {
    namespace {
        std::atomic<uint64_t> g_counter{0};

        std::string TempPath(const std::string& path) {
#ifdef _WIN32
            const unsigned long pid = GetCurrentProcessId();
#else
            const unsigned long pid = static_cast<unsigned long>(getpid());
#endif
            return path + ".tmp." + std::to_string(pid) + "." + std::to_string(g_counter.fetch_add(1));
        }
    }

    void EnsurePrivateDirectory(const std::filesystem::path& dir) {
        namespace fs = std::filesystem;
        std::error_code ec;
        if (dir.empty() || fs::exists(dir, ec))
            return;
        EnsurePrivateDirectory(dir.parent_path());
        if (fs::create_directory(dir, ec)) {
#ifndef _WIN32
            chmod(dir.c_str(), 0700);
#endif
        }
    }

    bool WriteFileAtomic(const std::string& path, const std::string& bytes) {
        namespace fs = std::filesystem;
        std::error_code ec;
        const fs::path parent = fs::path(path).parent_path();
        EnsurePrivateDirectory(parent);
        const std::string tmp = TempPath(path);
#ifdef _WIN32
        {
            std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
            out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
            out.flush();
            if (!out) {
                out.close();
                fs::remove(tmp, ec);
                return false;
            }
        }
        if (!MoveFileExA(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            fs::remove(tmp, ec);
            return false;
        }
        return true;
#else
        const int fd = open(tmp.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
        if (fd < 0)
            return false;
        size_t done = 0;
        bool ok = true;
        while (done < bytes.size()) {
            const ssize_t n = write(fd, bytes.data() + done, bytes.size() - done);
            if (n < 0) {
                ok = false;
                break;
            }
            done += static_cast<size_t>(n);
        }
        ok = ok && fsync(fd) == 0;
        ok = (close(fd) == 0) && ok;
        if (!ok || rename(tmp.c_str(), path.c_str()) != 0) {
            unlink(tmp.c_str());
            return false;
        }
        const std::string dir = parent.empty() ? std::string(".") : parent.string();
        const int dfd = open(dir.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
        if (dfd >= 0) {
            fsync(dfd);
            close(dfd);
        }
        return true;
#endif
    }
}
