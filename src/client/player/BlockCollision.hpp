#pragma once
#include "client/player/PlayerPhysics.hpp"
#include "world/level/Level.hpp"

#include <cstdint>
#include <vector>

#include <memory>

namespace perlin {
    class BlockLibrary;

    void SetEntityCollisionBoxes(std::vector<physics::Box> boxes);

    class LevelCollision final : public physics::World {
    public:
        LevelCollision(const Level& level, const BlockLibrary* blocks);

        physics::BlockShape Query(int x, int y, int z) const override;
        physics::bds::ScaffoldingCell ClimberCell(int x, int y, int z) const override;
        bool CollisionChunkReady(int x, int z) const override;
        physics::bds::TerrainHeightLimits CollisionHeightLimits() const override;
        physics::BlockShape CollisionQuery(int x, int y, int z) const override { return QueryMain(x, y, z); }
        const std::vector<physics::Box>* CollisionEntityBoxes() const override { return entityBoxes.get(); }
        static const physics::BlockShape& ShapeOf(uint16_t id);
        bool Selection(int x, int y, int z, std::vector<physics::Box>& out) const;

    private:
        enum class Kind : uint8_t { Plain, Fence, Wall, Pane, Door };
        struct Info {
            bool ready = false;
            Kind kind = Kind::Plain;
            bool full = false;
            uint64_t definitionRevision = 0;
            uint64_t voxelRevision = 0;
            physics::BlockShape shape;
            bool selectionDefined = false;
            std::vector<physics::Box> selection;
        };

        physics::BlockShape QueryMain(int x, int y, int z) const;
        static const Info& Lookup(uint16_t id, const BlockLibrary* blocks);
        static Info Classify(uint16_t id, const BlockLibrary* blocks);
        static Info ClassifyVanilla(uint16_t id, const BlockLibrary* blocks);

        const Level& level;
        const BlockLibrary* blocks;
        std::shared_ptr<const std::vector<physics::Box>> entityBoxes;
    };
}
