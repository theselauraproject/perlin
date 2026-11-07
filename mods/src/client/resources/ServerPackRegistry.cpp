#include "client/resources/ServerPacks.hpp"

namespace perlin {
    ServerPacks& ServerPacks::Instance() {
        static ServerPacks instance;
        return instance;
    }

    void ServerPacks::SetRoot(const std::string& directory) {
        std::lock_guard lock(mutex);
        root = directory;
    }

    void ServerPacks::Clear() {
        std::lock_guard lock(mutex);
        if (dirs.size() != pinned.size()) {
            dirs = pinned; // test folders (AddDirectory) stay mounted
            generation++;
        }
    }

    void ServerPacks::AddDirectory(const std::string& directory) {
        std::lock_guard<std::mutex> lk(mutex);
        dirs.push_back(directory);
        pinned.push_back(directory);
        generation++;
    }

    std::vector<std::string> ServerPacks::Directories() const {
        std::lock_guard lock(mutex);
        return dirs;
    }
}
