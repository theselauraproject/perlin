#include "client/ui/DebugScreen.hpp"

#include "client/i18n/Lang.hpp"

#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>

namespace perlin::ui {
    namespace {
        std::string T(const std::string& key) {
            return i18n::Tr(key);
        }
        std::string T(const std::string& key, const std::vector<std::string>& args) {
            return i18n::Tr(key, args);
        }
        std::string N(long long v) {
            return std::to_string(v);
        }
        std::string F(double v, int decimals) {
            char buffer[64];
            std::snprintf(buffer, sizeof(buffer), "%.*f", decimals, v);
            return buffer;
        }

        int FloorDiv(int a, int b) {
            return static_cast<int>(std::floor(static_cast<double>(a) / b));
        }

        std::string Pad(int v, int digits) {
            std::string s = std::to_string(v);
            while (static_cast<int>(s.size()) < digits)
                s.insert(s.begin(), '0');
            return s;
        }

        uint64_t Mb(uint64_t bytes) {
            return bytes / 1024 / 1024;
        }

        void Target(const DebugInfo::Target& t, const char* headingKey, std::vector<std::string>& right) {
            right.push_back("");
            right.push_back(std::string("\xC2\xA7n") + T(headingKey, {N(t.x), N(t.y), N(t.z)}));
            right.push_back(t.name);
            for (const auto& [key, value] : t.properties) {
                std::string v = value;
                if (value == "true")
                    v = "\xC2\xA7"
                        "a" +
                        T("perlin.true");
                else if (value == "false")
                    v = "\xC2\xA7"
                        "c" +
                        T("perlin.false");
                right.push_back(key + ": " + v);
            }
        }
    }

    std::string FacingText(float javaYaw) {
        const int index = static_cast<int>(std::floor(javaYaw / 90.0f + 0.5f)) & 3;
        static const char* kKeys[4] = {"perlin.debug.facing.south", "perlin.debug.facing.west",
                                       "perlin.debug.facing.north", "perlin.debug.facing.east"};
        return T(kKeys[index]);
    }

    void BuildDebugLines(const DebugInfo& d, std::vector<std::string>& left,
                         std::vector<std::string>& right) {
        left.clear();
        right.clear();
        const std::string unknown = T("perlin.unknown");

        left.push_back(d.version);
        left.push_back(
            T("perlin.debug.fps",
              {N(d.fps), d.maxFps > 0 ? N(d.maxFps) : T("perlin.debug.inf"),
               d.vsync ? T("perlin.debug.vsync") : std::string(),
               T(d.fancyGraphics ? "options.graphics.fancy" : "options.graphics.fast"),
               d.clouds ? T(d.fancyClouds ? "perlin.debug.clouds_fancy" : "perlin.debug.clouds_fast")
                        : std::string(),
               N(d.biomeBlend)}));
        left.push_back(T("perlin.debug.server", {d.serverBrand, N(d.tx), N(d.rx)}));
        left.push_back(T("perlin.debug.chunks",
                         {N(d.chunksRendered), N(d.chunksLoaded), N(d.renderDistance),
                          Pad(d.pendingChunks, 3), Pad(d.pendingUploads, 2), Pad(d.availableBuffers, 2)}));
        left.push_back(T("perlin.debug.entities", {N(d.entitiesShown), N(d.entitiesTotal),
                                                   N(d.entitiesCulled), N(d.simulationDistance)}));
        left.push_back(T("perlin.debug.particles", {N(d.particles), N(d.entitiesTotal)}));
        left.push_back(T("perlin.debug.chunk_cache", {N(d.chunksLoaded)}));
        left.push_back(T("perlin.debug.dimension", {d.dimension}));
        left.push_back("");

        const int bx = static_cast<int>(std::floor(d.x)), by = static_cast<int>(std::floor(d.y)),
                  bz = static_cast<int>(std::floor(d.z));
        left.push_back(T("perlin.debug.xyz", {F(d.x, 3), F(d.y, 5), F(d.z, 3)}));
        left.push_back(T("perlin.debug.block", {N(bx), N(by), N(bz)}));
        left.push_back(T("perlin.debug.chunk", {N(bx & 15), N(by & 15), N(bz & 15), N(FloorDiv(bx, 16)),
                                                N(FloorDiv(by, 16)), N(FloorDiv(bz, 16))}));
        float yaw = std::fmod(d.yaw, 360.0f);
        if (yaw >= 180.0f)
            yaw -= 360.0f;
        if (yaw < -180.0f)
            yaw += 360.0f;
        left.push_back(T("perlin.debug.facing", {FacingText(d.yaw), F(yaw, 1), F(d.pitch, 1)}));
        if (d.inLoadedChunk) {
            left.push_back(T("perlin.debug.light", {N(d.skyLight > d.blockLight ? d.skyLight : d.blockLight),
                                                    N(d.skyLight), N(d.blockLight)}));
            left.push_back(
                T("perlin.debug.heightmaps", {N(d.heightWorldSurface), N(d.heightMotionBlocking)}));
            left.push_back(T("perlin.debug.biome", {d.biome}));
        } else {
            left.push_back(T("perlin.debug.outside"));
        }

        right.push_back(T("perlin.debug.compiler", {d.compiler.empty() ? unknown : d.compiler}));
        if (d.memTotal > 0) {
            right.push_back(T("perlin.debug.memory", {N(static_cast<long long>(d.memUsed * 100 / d.memTotal)),
                                                      N(static_cast<long long>(Mb(d.memUsed))),
                                                      N(static_cast<long long>(Mb(d.memTotal)))}));
            right.push_back(
                T("perlin.debug.allocated", {N(static_cast<long long>(d.memPeak * 100 / d.memTotal)),
                                             N(static_cast<long long>(Mb(d.memPeak)))}));
        }
        right.push_back("");
        right.push_back(T("perlin.debug.cpu", {N(d.cpuThreads), d.cpuName.empty() ? unknown : d.cpuName}));
        right.push_back("");
        right.push_back(T("perlin.debug.display", {N(d.displayW), N(d.displayH),
                                                   d.displayVendor.empty() ? unknown : d.displayVendor}));
        if (!d.gpuLine1.empty())
            right.push_back(d.gpuLine1);
        if (!d.gpuLine2.empty())
            right.push_back(d.gpuLine2);
        if (d.block.valid)
            Target(d.block, "perlin.debug.target_block", right);
        if (d.fluid.valid)
            Target(d.fluid, "perlin.debug.target_fluid", right);
    }
}
