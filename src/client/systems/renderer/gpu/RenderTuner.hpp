#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include <conduit/device.hpp>

namespace perlin::gpu {

    class RenderTuner {
    public:
        enum Feature : uint8_t { GpuCull, Occlusion, FaceCull, ShadowCull, EntityCull, GpuUi, kFeatureCount };

        struct Sample {
            double intervalMs = 0;
            double gpuMs = -1;
        };

        struct Verdict {
            bool decided = false;
            bool on = true;
            double onMs = 0, offMs = 0;
            bool byGpu = false;
        };

        RenderTuner();

        static const char* Name(Feature f);
        static Verdict Decide(const std::vector<Sample>& on, const std::vector<Sample>& off);

        void Init(const conduit::adapter_info& adapter, std::string savePath);
        bool On(Feature f) const { return state[f]; }

        void GpuBegin(conduit::device& dev, conduit::command_list& list, uint32_t slot, uint32_t slots);
        void GpuEnd(conduit::command_list& list);
        void Mark(conduit::command_list& list, const char* label);
        void Release(conduit::device& dev);

        void EndFrame(bool worldFrame);
        void Advance(bool worldFrame, double intervalMs);

        bool Testing() const { return phase == Phase::Testing; }
        void TimeEveryFrame(bool on) { everyFrame = on; }
        double LastGpuMs() const { return lastGpuMs; }

    private:
        enum class Phase { Off, Waiting, Testing, Done };
        static constexpr int kWarmup = 6;
        static constexpr int kBlockFrames = 30;
        static constexpr int kRounds = 8;
        static constexpr int kSettleFrames = 600;

        struct Block {
            bool on = true;
            std::vector<Sample> samples;
        };

        void ApplyEnvironment();
        void StartFeature();
        void FinishFeature();
        void Save() const;
        double Now() const;

        std::array<bool, kFeatureCount> state{true, true, true, true, true, true};
        std::array<bool, kFeatureCount> forced{};
        std::array<bool, kFeatureCount> tested{};
        std::string key, path;
        Phase phase = Phase::Off;
        int feature = -1;
        int settle = 0;
        int frameInBlock = 0;
        std::vector<Block> blocks;
        double lastTime = 0;

        conduit::query_set_handle queries;
        std::vector<conduit::buffer_handle> readback;
        struct Pending {
            bool valid = false;
            int feature = -1;
            size_t block = 0, sample = 0;
            uint32_t used = 2;
            std::vector<const char*> labels;
        };
        static constexpr uint32_t kQueriesPerSlot = 48;
        uint32_t used = 0;
        std::vector<const char*> frameLabels;
        std::vector<std::pair<const char*, double>> passTotals;
        int passFrames = 0;
        void AddPass(const char* label, double ms);
        std::vector<Pending> pending;
        conduit::device* device = nullptr;
        uint32_t curSlot = 0;
        bool timestamps = false;
        bool frameTimed = false;
        bool everyFrame = false;
        double lastGpuMs = -1.0;
        double period = 1.0;
    };

}
