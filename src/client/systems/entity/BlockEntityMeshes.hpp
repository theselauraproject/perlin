#pragma once
#include "client/systems/gfx/RenderContext.hpp"

#include <cstdint>
#include <functional>
#include <glm/glm.hpp>
#include <map>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace perlin {
    struct Frustum;
    namespace ui {
        class McFont;
    }
}

namespace perlin::entity {
    struct BlockEntityDraw {
        enum class Kind { Bed, Sign, Skull };
        int skull = 0;
        std::vector<std::pair<std::string, int>> patterns;
        bool onWall = false;
        float lid = 0.0f;
        Kind kind = Kind::Bed;
        glm::dvec3 origin{0.0};
        float yaw = 0.0f;
        int color = 14;
        bool hanging = false;
        bool wall = false;
        std::string front, back;
        uint32_t frontColor = 0xFF000000u, backColor = 0xFF000000u;
        bool frontGlow = false, backGlow = false;
    };

    struct MeshBatch {
        std::vector<UiVertex> vertices;
        std::vector<uint32_t> indices;
        std::vector<int8_t> light;
    };

    struct OverlayMeshBatch : MeshBatch {
        bool overlay = false;
    };

    struct BlockEntityBatches {
        std::unordered_map<int, MeshBatch> beds;
        std::map<std::string, OverlayMeshBatch> skulls;
        MeshBatch text;
    };

    struct BlockEntityScene {
        glm::dvec3 camera{0.0};
        const Frustum* frustum = nullptr;
        std::function<bool(int, int, int)> sectionReached;
        std::function<glm::vec3(const glm::dvec3&)> light;
        std::function<glm::ivec2(const glm::dvec3&)> lightLevels;
        double worldTicks = 0;
        bool packLit = false;
        ui::McFont* font = nullptr;
        bool signTextAvailable = false;
    };

    BlockEntityBatches BuildBlockEntityBatches(const std::vector<BlockEntityDraw>& draws, const BlockEntityScene& scene);

    const char* SkullBlockName(const std::string& texture);
    const char* BedBlockName(int color);
    std::string BedTextureName(int color);
}
