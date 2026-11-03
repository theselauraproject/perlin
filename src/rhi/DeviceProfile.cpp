#include "rhi/DeviceProfile.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <sstream>

namespace rhi {
    const char* TierName(Tier tier) {
        switch (tier) {
        case Tier::Minimal:
            return "minimal";
        case Tier::Low:
            return "low";
        case Tier::Medium:
            return "medium";
        case Tier::High:
            return "high";
        case Tier::Ultra:
            return "ultra";
        }
        return "medium";
    }

    const char* UpscalerName(Upscaler u) {
        switch (u) {
        case Upscaler::Native:
            return "native";
        case Upscaler::Spatial:
            return "spatial";
        case Upscaler::Fsr:
            return "fsr";
        case Upscaler::XeSS:
            return "xess";
        case Upscaler::Dlss:
            return "dlss";
        }
        return "native";
    }

    namespace {
        constexpr uint64_t kGiB = 1ull << 30;

        uint64_t UsableMemory(const AdapterInfo& a) {
            // integrated parts share system memory; only part of it is realistically the gpu's
            return a.type == AdapterType::Discrete ? a.dedicatedVram
                                                   : (std::max)(a.dedicatedVram, a.sharedMemory / 2);
        }

        bool IsSoftware(const AdapterInfo& a) {
            return a.type == AdapterType::Cpu || a.type == AdapterType::Virtual;
        }

        // packed VK_API_VERSION 1.3: a driver this new is recent enough to trust with the whole renderer
        constexpr uint32_t kVulkan13 = (1u << 22) | (3u << 12);

        bool Contains(const std::string& haystack, const char* needle) {
            std::string h = haystack;
            std::transform(h.begin(), h.end(), h.begin(),
                           [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
            return h.contains(needle);
        }

        // vulkan implemented on top of another api, or in software: works, but never beats the native path
        bool IsTranslationLayer(const AdapterInfo& a) {
            return a.api == Api::Vulkan &&
                   (Contains(a.driverName, "dozen") || Contains(a.driverName, "moltenvk") ||
                    Contains(a.driverName, "gfxstream") || Contains(a.driverName, "venus") ||
                    Contains(a.name, "d3d12") || Contains(a.name, "microsoft direct3d"));
        }

        // what this adapter's driver says about its api: positive favours it, negative steers to another api for the same gpu
        double DriverAdjust(const AdapterInfo& a, bool windows, bool apple, std::string* note) {
            auto say = [&](const char* text) {
                if (note)
                    *note = text;
            };
            if (IsTranslationLayer(a)) {
                say("vulkan here is a translation layer over another api: native api preferred");
                return -300.0;
            }
            if (!windows || apple || a.type == AdapterType::Cpu || a.type == AdapterType::Virtual)
                return 0.0;
            // apiVersion 0 = unknown (synthetic or unreadable): no opinion
            const bool recentVulkan = a.api == Api::Vulkan && a.apiVersion >= kVulkan13;
            const bool oldVulkan = a.api == Api::Vulkan && a.apiVersion != 0 && a.apiVersion < kVulkan13;
            switch (a.vendorId) {
            case vendor_id::kAmd:
                // radeon: the vulkan driver is the better-tested path on windows; an older one that only reaches 1.2 is not
                if (recentVulkan) {
                    say("amd gpu with a current vulkan driver: vulkan preferred");
                    return 35.0;
                }
                if (oldVulkan) {
                    say("amd vulkan driver is old (below 1.3): direct3d 12 preferred");
                    return -40.0;
                }
                return 0.0;
            case vendor_id::kNvidia:
                // geforce: d3d12 stays first; only a very old vulkan driver is held back further
                if (oldVulkan) {
                    say("nvidia vulkan driver is old (below 1.3): direct3d 12 preferred");
                    return -20.0;
                }
                return 0.0;
            case vendor_id::kIntel:
                if (oldVulkan) {
                    say("intel vulkan driver is old (below 1.3): direct3d 12 preferred");
                    return -40.0;
                }
                return 0.0;
            default:
                return 0.0;
            }
        }

        double ApiBase(Api api, bool windows, bool apple) {
            switch (api) {
            case Api::Vulkan:
                return windows ? 100.0 : 110.0;
            case Api::D3D12:
                return windows ? 110.0 : 0.0;
            case Api::Metal:
                return apple ? 120.0 : 0.0;
            case Api::D3D11:
                return 40.0;
            case Api::WebGPU:
                return 30.0;
            default:
                return 0.0;
            }
        }
    }

    double ScoreCandidate(const Candidate& c, bool windows, bool apple) {
        const AdapterInfo& a = c.adapter;
        if (!a.usable)
            return -1e9;
        double s = ApiBase(a.api, windows, apple);
        if (s <= 0)
            return -1e9;
        // the api score only breaks ties between adapters of the same quality; the adapter class dominates
        switch (a.type) {
        case AdapterType::Discrete:
            s += 1000;
            break;
        case AdapterType::Integrated:
            s += 300;
            break;
        case AdapterType::Unknown:
            s += 200;
            break;
        case AdapterType::Virtual:
            s -= 400;
            break;
        case AdapterType::Cpu:
            s -= 800;
            break;
        }
        const double gib = static_cast<double>(UsableMemory(a)) / static_cast<double>(kGiB);
        s += 40.0 * std::log2(1.0 + gib);
        if (c.haveCaps) {
            const DeviceFeatures& f = c.caps.features;
            s += f.drawIndirectCount ? 25 : 0;
            s += f.asyncCompute ? 10 : 0;
            s += f.timestampQueries ? 3 : 0;
            s += f.meshShaders ? 8 : 0;
            s += f.rayTracing ? 8 : 0;
            s += f.bindless ? 6 : 0;
            s += f.enhancedBarriers ? 4 : 0;
            // a backend that cannot cull on the gpu or draw indirectly is a lot slower on a big world
            if (!f.multiDrawIndirect)
                s -= 60;
        }
        // d3d12 on pre-xe intel and old mobile parts is the more mature path
        if (windows && a.vendorId == vendor_id::kIntel && a.type == AdapterType::Integrated &&
            a.api == Api::Vulkan)
            s -= 30;
        s += DriverAdjust(a, windows, apple, nullptr);
        return s;
    }

    Tier TierFor(const Candidate& c) {
        const AdapterInfo& a = c.adapter;
        if (IsSoftware(a))
            return Tier::Minimal;
        const double gib = static_cast<double>(UsableMemory(a)) / static_cast<double>(kGiB);
        int level;
        if (gib < 1.5)
            level = 0;
        else if (gib < 3.0)
            level = 1;
        else if (gib < 6.0)
            level = 2;
        else if (gib < 10.0)
            level = 3;
        else
            level = 4;
        // integrated gpus have the memory but not the throughput
        if (a.type == AdapterType::Integrated)
            level = (std::min)(level, 2);
        if (c.haveCaps) {
            const DeviceFeatures& f = c.caps.features;
            if (!f.multiDrawIndirect)
                level = (std::min)(level, 1);
            // a discrete card with the newest hardware features is a recent one
            if (a.type == AdapterType::Discrete && level >= 3 && (f.meshShaders || f.cooperativeVector))
                level = 4;
        }
        return static_cast<Tier>(level);
    }

    void Probe::Sort() {
        std::stable_sort(candidates.begin(), candidates.end(),
                         [](const Candidate& a, const Candidate& b) { return a.score > b.score; });
    }

    Probe ProbeSystem(const ProbeOptions& options) {
        Probe probe;
#if defined(_WIN32)
        constexpr bool windows = true;
#else
        constexpr bool windows = false;
#endif
#if defined(__APPLE__)
        constexpr bool apple = true;
#else
        constexpr bool apple = false;
#endif
        std::vector<Api> apis = options.apis;
        if (apis.empty())
            apis = CompiledApis();
        for (Api api : apis) {
            if (api == Api::None)
                continue;
            InstanceOptions io;
            io.validation = options.validation;
            std::unique_ptr<Instance> instance = CreateInstance(api, io);
            if (!instance)
                continue;
            const size_t first = probe.candidates.size();
            for (const AdapterInfo& adapter : instance->Adapters()) {
                if (!options.allowSoftware && IsSoftware(adapter))
                    continue;
                Candidate c;
                c.adapter = adapter;
                c.score = ScoreCandidate(c, windows, apple);
                probe.candidates.push_back(std::move(c));
            }
            if (!options.readCaps || probe.candidates.size() == first)
                continue;
            // exact features only for the best adapter of this api: a device per adapter would be slow
            size_t best = first;
            for (size_t i = first; i < probe.candidates.size(); i++)
                if (probe.candidates[i].score > probe.candidates[best].score)
                    best = i;
            if (probe.candidates[best].score < -1e8)
                continue;
            DeviceOptions dopts;
            dopts.adapterIndex = probe.candidates[best].adapter.index;
            dopts.validation = false;
            if (std::unique_ptr<Device> device = instance->CreateDevice(dopts)) {
                probe.candidates[best].caps = device->Caps();
                probe.candidates[best].haveCaps = true;
                probe.candidates[best].score = ScoreCandidate(probe.candidates[best], windows, apple);
            } else {
                probe.candidates[best].notes.push_back("device creation failed during probe");
                probe.candidates[best].adapter.usable = false;
                probe.candidates[best].score = -1e9;
            }
        }
        probe.Sort();
        return probe;
    }

    namespace {
        bool Failed(const std::vector<KnownFailure>& failures, const AdapterInfo& a) {
            for (const KnownFailure& f : failures)
                if (f.api == a.api && f.vendorId == a.vendorId && f.deviceId == a.deviceId &&
                    f.driverVersion == a.driverVersion && f.count >= 2)
                    return true;
            return false;
        }
    }

    RenderProfile ChooseProfile(const Probe& probe, const UserChoice& user,
                                const std::vector<KnownFailure>& failures, bool windows, bool apple) {
        RenderProfile p;
        std::vector<Candidate> pool;
        for (Candidate c : probe.candidates) {
            c.score = ScoreCandidate(c, windows, apple);
            if (c.score < -1e8)
                continue;
            std::string driverNote;
            if (DriverAdjust(c.adapter, windows, apple, &driverNote) != 0.0 && !driverNote.empty())
                c.notes.push_back(driverNote);
            if (Failed(failures, c.adapter)) {
                c.score -= 5000;
                c.notes.push_back("failed on a previous launch with this driver");
            }
            pool.push_back(std::move(c));
        }
        std::stable_sort(pool.begin(), pool.end(),
                         [](const Candidate& a, const Candidate& b) { return a.score > b.score; });

        // the user's pick goes first, the rest stay behind it as fallbacks
        if (user.api != Api::None || user.adapterIndex >= 0) {
            auto matches = [&](const Candidate& c) {
                return (user.api == Api::None || c.adapter.api == user.api) &&
                       (user.adapterIndex < 0 || static_cast<int>(c.adapter.index) == user.adapterIndex);
            };
            auto it = std::stable_partition(pool.begin(), pool.end(), matches);
            if (it != pool.begin())
                p.reasons.push_back("renderer chosen by the user");
            else
                p.reasons.push_back("the requested renderer is not available here; picked automatically");
        }
        if (pool.empty()) {
            p.reasons.push_back("no usable graphics adapter found");
            return p;
        }

        const Candidate& best = pool.front();
        p.api = best.adapter.api;
        p.adapterIndex = best.adapter.index;
        p.adapterName = best.adapter.name;
        p.fallbacks = pool;
        p.reasons.push_back(std::string("using ") + ApiName(p.api) + " on " + best.adapter.name);
        for (const std::string& n : best.notes)
            p.reasons.push_back(n);

        p.tier = user.autoTier ? TierFor(best) : user.tier;
        if (IsSoftware(best.adapter))
            p.reasons.push_back("software rasteriser: expect low frame rates");

        const bool haveCaps = best.haveCaps;
        const DeviceFeatures f = haveCaps ? best.caps.features : DeviceFeatures{};
        const uint64_t mem = UsableMemory(best.adapter);

        static const struct {
            uint32_t render, lod, builds;
            float anisotropy;
            uint32_t msaa;
        } kTiers[] = {
            {6, 12, 2, 1.0f, 1},   // minimal
            {8, 24, 3, 2.0f, 1},   // low
            {12, 48, 4, 4.0f, 1},  // medium
            {20, 96, 6, 8.0f, 1},  // high
            {32, 160, 8, 16.0f, 1} // ultra
        };
        const auto& t = kTiers[static_cast<size_t>(p.tier)];
        p.renderDistance = t.render;
        p.lodDistance = t.lod;
        p.maxMeshBuildsPerFrame = t.builds;
        p.maxAnisotropy = haveCaps
                              ? (std::min)(t.anisotropy, (std::max)(1.0f, best.caps.limits.maxAnisotropy))
                              : t.anisotropy;
        p.msaa = 1;
        p.lodMaxLevel = p.tier >= Tier::High ? 4 : 3;

        // meshes live in gpu memory: a share of it, never so much that the frame buffers and textures starve
        // geometry only needs what the view distance fills; the rest of video memory is better left free for the driver and other apps
        const uint64_t share = mem ? mem / 6 : (192ull << 20);
        p.meshBudgetBytes = std::clamp<uint64_t>(share, 96ull << 20, 2ull << 30);
        p.retainBudgetBytes = p.meshBudgetBytes / 6;

        p.framesInFlight = (p.tier >= Tier::Medium && best.adapter.type == AdapterType::Discrete) ? 3 : 2;
        p.gpuCulling = !haveCaps ? p.tier >= Tier::Medium : (f.drawIndirectCount || f.multiDrawIndirect);
        p.asyncCompute = haveCaps && f.asyncCompute && best.adapter.type == AdapterType::Discrete;
        p.tearing = haveCaps && f.tearing;
        // the game's shaders encode sdr gamma themselves and there is no hdr tonemap yet, so a float/linear swapchain would
        // show oversaturated colours; stays off until that pass exists
        p.hdr = false;

        // vendor upscalers first when the hardware vendor has one; fsr runs anywhere so it is always the fallback
        if (user.allowUpscaling && p.tier >= Tier::Low) {
            if (best.adapter.vendorId == vendor_id::kNvidia && p.api != Api::D3D11)
                p.upscalers.push_back(Upscaler::Dlss);
            if (best.adapter.vendorId == vendor_id::kIntel && best.adapter.type == AdapterType::Discrete)
                p.upscalers.push_back(Upscaler::XeSS);
            if (p.api == Api::Vulkan || p.api == Api::D3D12)
                p.upscalers.push_back(Upscaler::Fsr);
            p.upscalers.push_back(Upscaler::Spatial);
        }
        p.upscalers.push_back(Upscaler::Native);

        // frame generation needs headroom and an explicit-api swapchain; it is offered, the vendor layer confirms it
        p.frameGeneration = user.allowFrameGeneration && p.tier >= Tier::High &&
                            best.adapter.type == AdapterType::Discrete &&
                            (p.api == Api::D3D12 || p.api == Api::Vulkan);
        p.neuralFeatures = haveCaps && (f.cooperativeVector || f.cooperativeMatrix);
        p.renderScale = 1.0f;
        if (p.tier <= Tier::Low && !p.upscalers.empty() && p.upscalers.front() != Upscaler::Native) {
            p.renderScale = 0.75f;
            p.reasons.push_back("modest gpu: an upscaler at 75% would help (off until turned on in the options)");
        }
        p.reasons.push_back(std::string("tier ") + TierName(p.tier));
        return p;
    }

    std::string SerializeFailures(const std::vector<KnownFailure>& failures) {
        std::ostringstream out;
        for (const KnownFailure& f : failures)
            out << ApiId(f.api) << ' ' << f.vendorId << ' ' << f.deviceId << ' ' << f.driverVersion << ' '
                << f.count << '\n';
        return out.str();
    }

    std::vector<KnownFailure> ParseFailures(const std::string& text) {
        std::vector<KnownFailure> out;
        std::istringstream in(text);
        std::string id;
        KnownFailure f;
        while (in >> id >> f.vendorId >> f.deviceId >> f.driverVersion >> f.count) {
            f.api = ApiFromId(id);
            if (f.api != Api::None)
                out.push_back(f);
        }
        return out;
    }

    void RecordFailure(std::vector<KnownFailure>& failures, const AdapterInfo& a) {
        for (KnownFailure& f : failures)
            if (f.api == a.api && f.vendorId == a.vendorId && f.deviceId == a.deviceId &&
                f.driverVersion == a.driverVersion) {
                f.count++;
                return;
            }
        failures.push_back({a.api, a.vendorId, a.deviceId, a.driverVersion, 1});
    }
}
