#include "client/renderer/gpu/RenderTuner.hpp"

#include "util/FileIo.hpp"
#include "util/Log.hpp"

#include <algorithm>
#include <cstdio>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <map>
#include <sstream>

namespace perlin::gpu {

    namespace {
        // the environment names: PERLIN_<on> forces a feature on, PERLIN_NO_<on> forces it off
        constexpr const char* kEnv[RenderTuner::kFeatureCount] = {
            "GPU_CULL", "OCCLUSION", "FACE_CULL", "GPU_SHADOW_CULL", "GPU_ENTITY_CULL", "GPU_UI"};
        constexpr const char* kNames[RenderTuner::kFeatureCount] = {"gpu_cull",    "occlusion",   "face_cull",
                                                                    "shadow_cull", "entity_cull", "gpu_ui"};

        bool EnvSet(const std::string& name) {
            const char* v = std::getenv(name.c_str());
            return v && *v && std::strcmp(v, "0") != 0;
        }

        double Median(std::vector<double> v) {
            if (v.empty())
                return 0;
            const size_t mid = v.size() / 2;
            std::nth_element(v.begin(), v.begin() + static_cast<std::ptrdiff_t>(mid), v.end());
            return v[mid];
        }

        // the sections of the save file: one per adapter key, each a list of "name 0|1"
        std::map<std::string, std::map<std::string, int>> ReadSaved(const std::string& path) {
            std::map<std::string, std::map<std::string, int>> out;
            std::string text;
            if (!file::Read(path, text))
                return out;
            std::istringstream in(text);
            std::string line, section;
            while (std::getline(in, line)) {
                if (!line.empty() && line.back() == '\r')
                    line.pop_back();
                if (line.empty() || line[0] == '#')
                    continue;
                if (line.rfind("adapter ", 0) == 0) {
                    section = line.substr(8);
                    continue;
                }
                const size_t space = line.find(' ');
                if (section.empty() || space == std::string::npos)
                    continue;
                out[section][line.substr(0, space)] = std::atoi(line.c_str() + space + 1);
            }
            return out;
        }
    } // namespace

    RenderTuner::RenderTuner() {
        ApplyEnvironment();
    }

    void RenderTuner::ApplyEnvironment() {
        for (int f = 0; f < kFeatureCount; f++) {
            if (EnvSet(std::string("PERLIN_NO_") + kEnv[f])) {
                state[f] = false;
                forced[f] = true;
            } else if (EnvSet(std::string("PERLIN_") + kEnv[f])) {
                state[f] = true;
                forced[f] = true;
            }
        }
    }

    const char* RenderTuner::Name(Feature f) {
        return kNames[f];
    }

    RenderTuner::Verdict RenderTuner::Decide(const std::vector<Sample>& on, const std::vector<Sample>& off) {
        Verdict v;
        if (on.size() < kBlockFrames || off.size() < kBlockFrames)
            return v;
        std::vector<double> a, b, ga, gb;
        for (const Sample& s : on) {
            a.push_back(s.intervalMs);
            if (s.gpuMs >= 0)
                ga.push_back(s.gpuMs);
        }
        for (const Sample& s : off) {
            b.push_back(s.intervalMs);
            if (s.gpuMs >= 0)
                gb.push_back(s.gpuMs);
        }
        v.decided = true;
        v.onMs = Median(a);
        v.offMs = Median(b);
        // turning a feature off has to win clearly; a tie keeps it on
        if (v.offMs < v.onMs * 0.97) {
            v.on = false;
            return v;
        }
        if (v.onMs < v.offMs * 0.97 || ga.size() < kBlockFrames || gb.size() < kBlockFrames)
            return v;
        // frame time tied: vsync or a frame cap hides the work, so compare gpu time
        v.byGpu = true;
        v.onMs = Median(ga);
        v.offMs = Median(gb);
        v.on = !(v.offMs < v.onMs * 0.95);
        return v;
    }

    double RenderTuner::Now() const {
        return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch())
            .count();
    }

    void RenderTuner::Init(const conduit::adapter_info& adapter, std::string savePath) {
        path = std::move(savePath);
        // std::format is avoided: on apple it needs macos 13.3 even for integers
        key = adapter.name + "|" + std::to_string(adapter.driver_version) + "|" +
              std::to_string(static_cast<int>(adapter.api));
        for (int f = 0; f < kFeatureCount; f++) {
            state[f] = true;
            forced[f] = false;
            tested[f] = false;
        }
        // until measured, occlusion starts off where it usually costs more than it saves
        state[Occlusion] = adapter.type != conduit::adapter_type::integrated;
        ApplyEnvironment();
        if (EnvSet("PERLIN_NO_AUTOTUNE")) {
            phase = Phase::Off;
            Log("tune", "auto-tune off (PERLIN_NO_AUTOTUNE)");
            return;
        }
        if (!EnvSet("PERLIN_AUTOTUNE_RESET")) {
            const auto saved = ReadSaved(path);
            if (const auto it = saved.find(key); it != saved.end()) {
                std::string summary;
                for (int f = 0; f < kFeatureCount; f++) {
                    const auto v = it->second.find(kNames[f]);
                    if (v == it->second.end())
                        continue;
                    tested[f] = true;
                    if (!forced[f])
                        state[f] = v->second != 0;
                    summary += std::string(" ") + kNames[f] + "=" + (state[f] ? "on" : "off");
                }
                Log("tune", "using the saved tuning for {}:{}", adapter.name, summary);
            }
        }
        phase = Phase::Done;
        for (int f = 0; f < kFeatureCount; f++)
            if (!forced[f] && !tested[f])
                phase = Phase::Waiting;
        settle = 0;
        if (phase == Phase::Waiting)
            Log("tune", "will measure which gpu-driven features are fastest on {} once a world has loaded",
                adapter.name);
    }

    void RenderTuner::GpuBegin(conduit::device& dev, conduit::command_list& list, uint32_t slot,
                               uint32_t slots) {
        frameTimed = false;
        if ((phase != Phase::Testing && !everyFrame) || !dev.caps().features.timestamp_queries)
            return;
        device = &dev;
        slots = std::max(1u, slots);
        if (!queries) {
            conduit::query_set_desc qd;
            qd.count = kQueriesPerSlot * slots;
            qd.name = "auto-tune times";
            queries = dev.create_query_set(qd);
            if (!queries)
                return;
            readback.assign(slots, {});
            pending.assign(slots, {});
            for (auto& b : readback) {
                conduit::buffer_desc bd;
                bd.size = kQueriesPerSlot * sizeof(uint64_t);
                bd.usage = conduit::buffer_usage::copy_dst;
                bd.memory = conduit::memory_class::readback;
                bd.name = "auto-tune times";
                b = dev.create_buffer(bd);
            }
            period = dev.timestamp_period_ns();
        }
        curSlot = slot % static_cast<uint32_t>(readback.size());
        // this slot's previous frame has finished (the frame fence was waited on): credit its gpu time
        Pending& p = pending[curSlot];
        if (p.valid && readback[curSlot]) {
            dev.invalidate(readback[curSlot], 0, kQueriesPerSlot * sizeof(uint64_t));
            if (const uint8_t* data = dev.map(readback[curSlot])) {
                uint64_t t[kQueriesPerSlot];
                std::memcpy(t, data, p.used * sizeof(uint64_t));
                const uint32_t last = p.used - 1;
                if (t[last] > t[0]) {
                    const double ms = static_cast<double>(t[last] - t[0]) * period * 1e-6;
                    lastGpuMs = ms;
                    if (p.feature >= 0 && p.feature == feature && p.block < blocks.size() &&
                        p.sample < blocks[p.block].samples.size())
                        blocks[p.block].samples[p.sample].gpuMs = ms;
                    if (everyFrame && p.used > 2) {
                        // t[0] frame start, t[1..used-2] marks, t[used-1] frame end
                        AddPass("setup", static_cast<double>(t[1] - t[0]) * period * 1e-6);
                        for (uint32_t i = 1; i < last; i++)
                            if (t[i + 1] >= t[i])
                                AddPass(p.labels[i - 1],
                                        static_cast<double>(t[i + 1] - t[i]) * period * 1e-6);
                        if (++passFrames >= 240) {
                            std::string line = "gpu ms:";
                            char buf[64];
                            for (const auto& [label, total] : passTotals) {
                                std::snprintf(buf, sizeof(buf), " %s=%.3f", label, total / passFrames);
                                line += buf;
                            }
                            Log("profile", "{}", line);
                            passTotals.clear();
                            passFrames = 0;
                        }
                    }
                }
            }
        }
        p.valid = false;
        list.write_timestamp(queries, curSlot * kQueriesPerSlot);
        used = 1;
        frameLabels.clear();
        frameTimed = true;
    }

    void RenderTuner::AddPass(const char* label, double ms) {
        for (auto& [l, total] : passTotals)
            if (l == label || std::strcmp(l, label) == 0) {
                total += ms;
                return;
            }
        passTotals.emplace_back(label, ms);
    }

    void RenderTuner::Mark(conduit::command_list& list, const char* label) {
        if (!frameTimed || !everyFrame || used + 1 >= kQueriesPerSlot)
            return;
        list.write_timestamp(queries, curSlot * kQueriesPerSlot + used);
        used++;
        frameLabels.push_back(label);
    }

    void RenderTuner::GpuEnd(conduit::command_list& list) {
        if (!frameTimed)
            return;
        list.write_timestamp(queries, curSlot * kQueriesPerSlot + used);
        used++;
        list.resolve_queries(queries, curSlot * kQueriesPerSlot, used, readback[curSlot], 0);
        // read back when this slot comes round again; a trial frame also says which sample it belongs to
        Pending& p = pending[curSlot];
        p.valid = true;
        p.feature = -1;
        p.used = used;
        p.labels = frameLabels;
    }

    void RenderTuner::Release(conduit::device& dev) {
        for (auto& b : readback)
            if (b)
                dev.destroy(b);
        readback.clear();
        pending.clear();
        if (queries)
            dev.destroy(queries);
        queries = {};
    }

    void RenderTuner::StartFeature() {
        // the terrain sub-features first, with gpu culling on, then gpu culling itself with its best settings:
        // otherwise a costly sub-feature would make gpu culling as a whole look slow
        static constexpr Feature kOrder[] = {Occlusion, FaceCull, ShadowCull, GpuCull, EntityCull, GpuUi};
        feature = -1;
        for (const Feature f : kOrder) {
            if (forced[f] || tested[f])
                continue;
            if (f != GpuCull && f <= ShadowCull && !state[GpuCull]) {
                // gpu culling is forced off: these do nothing
                tested[f] = true;
                continue;
            }
            feature = f;
            break;
        }
        if (feature < 0) {
            phase = Phase::Done;
            std::string summary;
            for (int f = 0; f < kFeatureCount; f++)
                summary += std::string(" ") + kNames[f] + "=" + (state[f] ? "on" : "off");
            Log("tune", "done:{} (saved; PERLIN_AUTOTUNE_RESET=1 measures again)", summary);
            Save();
            return;
        }
        blocks.clear();
        blocks.push_back({true, {}});
        state[feature] = true;
        frameInBlock = 0;
    }

    void RenderTuner::FinishFeature() {
        std::vector<Sample> on, off;
        for (const Block& b : blocks)
            (b.on ? on : off).insert((b.on ? on : off).end(), b.samples.begin(), b.samples.end());
        const Verdict v = Decide(on, off);
        const Feature f = static_cast<Feature>(feature);
        state[f] = v.on;
        tested[f] = true;
        // snprintf, not std::format: apple's libc++ formats floats only from macos 13.3
        char line[160];
        std::snprintf(line, sizeof(line), "%s: on %.2f ms, off %.2f ms (%s) -> %s", kNames[f], v.onMs,
                      v.offMs, v.byGpu ? "gpu time" : "frame time", v.on ? "on" : "off");
        Log("tune", "{}", line);
        StartFeature();
    }

    void RenderTuner::EndFrame(bool worldFrame) {
        const double now = Now();
        const double interval = lastTime > 0 ? now - lastTime : 0;
        lastTime = now;
        Advance(worldFrame, interval);
    }

    void RenderTuner::Advance(bool worldFrame, double interval) {
        if (phase == Phase::Waiting) {
            // wait for chunk streaming and pipeline compiles to settle after joining
            settle = worldFrame ? settle + 1 : 0;
            if (settle >= kSettleFrames) {
                phase = Phase::Testing;
                Log("tune", "measuring the gpu-driven features (a few seconds; the picture does not change)");
                StartFeature();
            }
            return;
        }
        if (phase != Phase::Testing)
            return;
        if (!worldFrame) {
            // left the world mid-trial: start this feature over when back
            phase = Phase::Waiting;
            settle = 0;
            if (feature >= 0)
                state[feature] = true;
            return;
        }
        Block& block = blocks.back();
        if (frameInBlock++ >= kWarmup && interval > 0 && interval < 250) {
            if (frameTimed && curSlot < pending.size()) {
                pending[curSlot].feature = feature;
                pending[curSlot].block = blocks.size() - 1;
                pending[curSlot].sample = block.samples.size();
            }
            block.samples.push_back({interval, -1});
        }
        if (static_cast<int>(block.samples.size()) < kBlockFrames)
            return;
        if (static_cast<int>(blocks.size()) >= kRounds * 2) {
            FinishFeature();
            return;
        }
        const bool next = !block.on;
        blocks.push_back({next, {}});
        state[feature] = next;
        frameInBlock = 0;
    }

    void RenderTuner::Save() const {
        auto saved = ReadSaved(path);
        auto& mine = saved[key];
        for (int f = 0; f < kFeatureCount; f++)
            if (tested[f] && !forced[f])
                mine[kNames[f]] = state[f] ? 1 : 0;
        std::string text = "# perlin render auto-tune: the fastest gpu-driven features per gpu and driver\n"
                           "# delete this file or set PERLIN_AUTOTUNE_RESET=1 to measure again\n";
        for (const auto& [k, values] : saved) {
            text += "adapter " + k + "\n";
            for (const auto& [n, v] : values)
                text += n + " " + std::to_string(v) + "\n";
        }
        if (!file::Write(path, text))
            Log("tune", "could not save {}", path);
    }

} // namespace perlin::gpu
