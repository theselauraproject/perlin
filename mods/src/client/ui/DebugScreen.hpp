#pragma once
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace perlin::ui {
    struct DebugInfo {
        std::string version;
        int fps = 0;
        int maxFps = 0;
        bool vsync = false;
        bool fancyGraphics = true;
        bool clouds = true, fancyClouds = true;
        int biomeBlend = 2;
        std::string serverBrand = "vanilla";
        int tx = 0, rx = 0;
        int chunksRendered = 0, chunksLoaded = 0, renderDistance = 8;
        int pendingChunks = 0, pendingUploads = 0, availableBuffers = 0;
        int entitiesShown = 0, entitiesTotal = 0, entitiesCulled = 0, simulationDistance = 0;
        int particles = 0;
        std::string dimension = "minecraft:overworld";

        double x = 0, y = 0, z = 0;
        float yaw = 0, pitch = 0;
        int skyLight = 15, blockLight = 0;
        int heightWorldSurface = 0, heightMotionBlocking = 0;
        std::string biome = "minecraft:plains";
        bool inLoadedChunk = true;
        int dayTime = 0;

        std::string compiler;
        uint64_t memUsed = 0, memPeak = 0, memTotal = 0;
        int cpuThreads = 0;
        std::string cpuName;
        int displayW = 0, displayH = 0;
        std::string displayVendor, gpuLine1, gpuLine2;

        struct Target {
            bool valid = false;
            int x = 0, y = 0, z = 0;
            std::string name;
            std::vector<std::pair<std::string, std::string>> properties;
        };
        Target block;
        Target fluid;
    };

    void BuildDebugLines(const DebugInfo& info, std::vector<std::string>& left,
                         std::vector<std::string>& right);

    std::string FacingText(float javaYaw);
}
