#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace perlin::ios {
    // shows the system document picker (Files, iCloud Drive, other apps' providers); done gets the chosen file's bytes
    // and its extension (".png", ".mcpack", ...) on the main thread, and is not called when the user cancels.
    // kind: "skin" / "cape" limit the picker to images; anything else ("pack", "mod") accepts any file
    bool PickDocument(const std::string& kind, std::function<void(std::vector<uint8_t>, std::string)> done);
}
