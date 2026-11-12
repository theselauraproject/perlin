#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include <conduit/device.hpp>

namespace perlin::gpu {

    // Picks the fastest set of gpu-driven rendering features for the machine it runs on.
    //
    // Every feature starts on (occlusion starts off on integrated gpus). Once the player is in a world and chunk
    // streaming has settled, each feature is measured live: short blocks of frames alternate on, off, on, off...
    // so drift in the scene cancels out, and the median of each side is compared. Frame-to-frame time decides;
    // when that ties (vsync or an fps cap hides the difference) gpu time from timestamp queries decides. A feature
    // is only turned off when that is clearly faster. The result is saved per gpu and driver, so the test runs once.
    //
    // Environment: PERLIN_NO_<FEATURE>=1 forces a feature off and PERLIN_<FEATURE>=1 forces it on; forced features
    // are not tested. PERLIN_NO_AUTOTUNE keeps the defaults; PERLIN_AUTOTUNE_RESET measures again.
    class RenderTuner {
    public:
        enum Feature : uint8_t { GpuCull, Occlusion, FaceCull, ShadowCull, EntityCull, GpuUi, kFeatureCount };

        struct Sample {
            double intervalMs = 0; // wall time since the previous frame
            double gpuMs = -1;     // gpu time of the frame's command list, or < 0 when unknown
        };

        // the result of one feature's trial (exposed for the self-test)
        struct Verdict {
            bool decided = false;
            bool on = true;
            double onMs = 0, offMs = 0;
            bool byGpu = false;
        };

        // applies PERLIN_<FEATURE> / PERLIN_NO_<FEATURE> at once, so the switches hold even before Init
        RenderTuner();

        static const char* Name(Feature f);
        static Verdict Decide(const std::vector<Sample>& on, const std::vector<Sample>& off);

        // reads the environment and any saved result for this adapter
        void Init(const conduit::adapter_info& adapter, std::string savePath);
        bool On(Feature f) const { return state[f]; }

        // gpu timestamps around the frame; safe to call when the device has no timestamp queries
        void GpuBegin(conduit::device& dev, conduit::command_list& list, uint32_t slot, uint32_t slots);
        void GpuEnd(conduit::command_list& list);
        void Release(conduit::device& dev);

        // once per presented frame. worldFrame: the frame drew a world (trials only run then)
        void EndFrame(bool worldFrame);
        // the same with a given frame interval (the self-test drives the tuner with this)
        void Advance(bool worldFrame, double intervalMs);

        bool Testing() const { return phase == Phase::Testing; }

    private:
        enum class Phase { Off, Waiting, Testing, Done };
        static constexpr int kWarmup = 6;       // frames ignored after each switch
        static constexpr int kBlockFrames = 30; // measured frames per block
        static constexpr int kRounds = 8;       // on/off block pairs per feature
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

        // everything on until Init has read the environment and any saved result
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

        // gpu timing: one pair of timestamps per frame slot, read back when the slot comes round again
        conduit::query_set_handle queries;
        std::vector<conduit::buffer_handle> readback;
        struct Pending {
            bool valid = false;
            int feature = -1;
            size_t block = 0, sample = 0;
        };
        std::vector<Pending> pending;
        conduit::device* device = nullptr;
        uint32_t curSlot = 0;
        bool timestamps = false;
        bool frameTimed = false;
        double period = 1.0;
    };

} // namespace perlin::gpu
