#include "core/Server.hpp"

namespace perlin::server {
    std::unique_ptr<Listener> CreateNetherNetListener(const ServerConfig&, std::function<std::string()>) {
        return nullptr;
    }
}
