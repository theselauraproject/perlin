#pragma once
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "perlin/abi.hpp"

namespace perlin::mods::support {
    struct PermissionName {
        abi::Permission bit;
        const char* name;
    };
    inline constexpr PermissionName kPermissionNames[] = {
        {abi::Permission::Network, "network"},     {abi::Permission::Packets, "packets"},
        {abi::Permission::Control, "control"},     {abi::Permission::Files, "files"},
        {abi::Permission::Native, "native"},       {abi::Permission::Microphone, "microphone"},
        {abi::Permission::Clipboard, "clipboard"}, {abi::Permission::Window, "window"},
        {abi::Permission::Sockets, "sockets"},     {abi::Permission::Gpu, "gpu"},
    };

    int64_t UnixMs();
    bool ReadFile(const std::string& path, std::vector<uint8_t>& out);
    std::string EmbeddedManifest(const std::string& path);
    bool SafeRelative(const std::string& rel, std::filesystem::path& out);
}
