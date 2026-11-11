#include "client/player/VoxelShapes.hpp"

#include <cstdio>

int main() {
    perlin::proto::VoxelShape shape;
    shape.cells.xSize = 32;
    shape.cells.ySize = shape.cells.zSize = 1;
    shape.cells.storage = {0x55, 0x55, 0x55, 0x55};
    for (int x = 0; x <= 32; ++x)
        shape.xCoordinates.push_back(static_cast<float>(x) / 32.0f);
    shape.yCoordinates = shape.zCoordinates = {0, 1};
    perlin::proto::VoxelShapesPacket packet;
    packet.shapes.push_back(shape);
    packet.nameMap.push_back({"test:disjoint", 0});
    auto& registry = perlin::VoxelShapeRegistry::Instance();
    const auto before = registry.Revision();
    registry.Set(packet);
    if (registry.Revision() == before)
        return 1;
    std::vector<perlin::physics::Box> boxes;
    if (!registry.Find("test:disjoint", boxes) || boxes.size() != 16)
        return 1;
    for (int x = 0; x < 32; ++x) {
        const double center = (x + 0.5) / 32.0;
        bool contained = false;
        for (const auto& box : boxes)
            contained |= box.x0 < center && box.x1 > center && box.y0 < 0.5 && box.y1 > 0.5 && box.z0 < 0.5 &&
                         box.z1 > 0.5;
        if (contained != (x % 2 == 0))
            return 1;
    }
    const auto setRevision = registry.Revision();
    registry.Clear();
    if (registry.Revision() == setRevision)
        return 1;
    std::puts("Voxel geometry preserves 16 disjoint shapes and all separating gaps");
    return 0;
}
