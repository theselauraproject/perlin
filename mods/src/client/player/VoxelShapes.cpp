#include "client/player/VoxelShapes.hpp"

#include <algorithm>
#include <mutex>
#include <unordered_map>

namespace perlin {
    namespace {
        std::mutex gMutex;
        std::vector<std::vector<physics::Box>> gShapes;
        std::unordered_map<std::string, uint16_t> gNames;

        std::vector<physics::Box> Bake(const proto::VoxelShape& shape) {
            std::vector<physics::Box> out;
            const int nx = shape.cells.xSize, ny = shape.cells.ySize, nz = shape.cells.zSize;
            if (nx <= 0 || ny <= 0 || nz <= 0 || shape.xCoordinates.size() < static_cast<size_t>(nx) + 1 ||
                shape.yCoordinates.size() < static_cast<size_t>(ny) + 1 ||
                shape.zCoordinates.size() < static_cast<size_t>(nz) + 1)
                return out;
            const auto filled = [&](int x, int y, int z) {
                const size_t bit = (static_cast<size_t>(x) * ny + y) * nz + z;
                const size_t byte = bit >> 3;
                return byte < shape.cells.storage.size() &&
                       ((shape.cells.storage[byte] >> (bit & 7)) & 1) != 0;
            };
            std::vector<uint8_t> used(static_cast<size_t>(nx) * ny * nz, 0);
            const auto at = [&](int x, int y, int z) -> uint8_t& {
                return used[(static_cast<size_t>(x) * ny + y) * nz + z];
            };
            for (int x = 0; x < nx; x++)
                for (int y = 0; y < ny; y++)
                    for (int z = 0; z < nz; z++) {
                        if (!filled(x, y, z) || at(x, y, z))
                            continue;
                        int z1 = z;
                        while (z1 + 1 < nz && filled(x, y, z1 + 1) && !at(x, y, z1 + 1))
                            z1++;
                        int y1 = y;
                        const auto rowOk = [&](int yy) {
                            for (int zz = z; zz <= z1; zz++)
                                if (!filled(x, yy, zz) || at(x, yy, zz))
                                    return false;
                            return true;
                        };
                        while (y1 + 1 < ny && rowOk(y1 + 1))
                            y1++;
                        int x1 = x;
                        const auto planeOk = [&](int xx) {
                            for (int yy = y; yy <= y1; yy++)
                                for (int zz = z; zz <= z1; zz++)
                                    if (!filled(xx, yy, zz) || at(xx, yy, zz))
                                        return false;
                            return true;
                        };
                        while (x1 + 1 < nx && planeOk(x1 + 1))
                            x1++;
                        for (int xx = x; xx <= x1; xx++)
                            for (int yy = y; yy <= y1; yy++)
                                for (int zz = z; zz <= z1; zz++)
                                    at(xx, yy, zz) = 1;
                        out.push_back({shape.xCoordinates[x], shape.yCoordinates[y], shape.zCoordinates[z],
                                       shape.xCoordinates[x1 + 1], shape.yCoordinates[y1 + 1],
                                       shape.zCoordinates[z1 + 1]});
                    }
            return out;
        }
    }

    VoxelShapeRegistry& VoxelShapeRegistry::Instance() {
        static VoxelShapeRegistry registry;
        return registry;
    }

    void VoxelShapeRegistry::Set(const proto::VoxelShapesPacket& packet) {
        std::lock_guard lock(gMutex);
        gShapes.clear();
        gNames.clear();
        for (const proto::VoxelShape& s : packet.shapes)
            gShapes.push_back(Bake(s));
        for (const proto::VoxelShapeNameEntry& n : packet.nameMap)
            gNames[n.name] = n.id;
        revision.fetch_add(1, std::memory_order_release);
    }

    void VoxelShapeRegistry::Clear() {
        std::lock_guard lock(gMutex);
        gShapes.clear();
        gNames.clear();
        revision.fetch_add(1, std::memory_order_release);
    }

    bool VoxelShapeRegistry::Find(const std::string& blockName, std::vector<physics::Box>& out) const {
        std::lock_guard lock(gMutex);
        const auto it = gNames.find(blockName);
        if (it == gNames.end() || it->second >= gShapes.size())
            return false;
        out = gShapes[it->second];
        return true;
    }

    size_t VoxelShapeRegistry::Size() const {
        std::lock_guard lock(gMutex);
        return gShapes.size();
    }
}
