#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include <conduit/device.hpp>

namespace perlin::gpu {

    class GpuFeatures {
    public:
        enum Feature : uint8_t { GpuCull, Occlusion, FaceCull, ShadowCull, EntityCull, GpuUi, kFeatureCount };
        enum class Level : uint8_t { Basic, MultiDraw, GpuDriven, GpuDrivenOcclusion };

        struct Profile {
            conduit::adapter_type type = conduit::adapter_type::unknown;
            conduit::api api = conduit::api::none;
            uint32_t vendor = 0;
            std::string name;
            bool gpuDriven = false;
            bool multiDraw = false;
            bool firstInstance = false;
            bool compute = true;
            int safeMode = 0;
        };

        static const char* Name(Feature f);
        static const char* LevelName(Level level);
        static bool Software(const Profile& p);
        static bool TileBased(const Profile& p);
        static Level Choose(const Profile& p, std::array<bool, kFeatureCount>& out);

        void Init(const conduit::adapter_info& adapter, const conduit::device_features& features,
                  const std::string& dataDir);
        bool On(Feature f) const { return state[f]; }
        Level CurrentLevel() const { return level; }

        void GpuBegin(conduit::device& dev, conduit::command_list& list, uint32_t slot, uint32_t slots);
        void GpuEnd(conduit::command_list& list);
        void Mark(conduit::command_list& list, const char* label);
        void Release(conduit::device& dev);
        void TimeEveryFrame(bool on) { everyFrame = on; }
        double LastGpuMs() const { return lastGpuMs; }

    private:
        void ApplyEnvironment();

        std::array<bool, kFeatureCount> state{};
        Level level = Level::Basic;

        conduit::query_set_handle queries;
        std::vector<conduit::buffer_handle> readback;
        struct Pending {
            bool valid = false;
            uint32_t used = 2;
            std::vector<const char*> labels;
        };
        static constexpr uint32_t kQueriesPerSlot = 256;
        uint32_t used = 0;
        std::vector<const char*> frameLabels;
        std::vector<std::pair<const char*, double>> passTotals;
        int passFrames = 0;
        void AddPass(const char* label, double ms);
        std::vector<Pending> pending;
        uint32_t curSlot = 0;
        bool frameTimed = false;
        bool everyFrame = false;
        double lastGpuMs = -1.0;
        double period = 1.0;
    };

}
