#include "client/PerlinInternal.hpp"
#include "client/Perlin.hpp"

#include "client/mods/ModManager.hpp"
#include "world/level/block/BlockPalette.hpp"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <cmath>

namespace perlin {
    namespace {
        using Clock = std::chrono::steady_clock;

        Clock::time_point After(int32_t ticks) {
            return Clock::now() + std::chrono::milliseconds(std::clamp(ticks, 1, 200) * 50);
        }

        constexpr size_t kMaxLinesPerPlugin = 20000;

        void AddBoxLines(std::vector<entity::EntityRenderer::FrameIn::ModLine>& out, const abi::WorldBox& b) {
            const glm::dvec3 lo(std::min(b.x0, b.x1), std::min(b.y0, b.y1), std::min(b.z0, b.z1));
            const glm::dvec3 hi(std::max(b.x0, b.x1), std::max(b.y0, b.y1), std::max(b.z0, b.z1));
            const glm::dvec3 c[8] = {{lo.x, lo.y, lo.z}, {hi.x, lo.y, lo.z}, {hi.x, lo.y, hi.z},
                                     {lo.x, lo.y, hi.z}, {lo.x, hi.y, lo.z}, {hi.x, hi.y, lo.z},
                                     {hi.x, hi.y, hi.z}, {lo.x, hi.y, hi.z}};
            static const int edges[12][2] = {{0, 1}, {1, 2}, {2, 3}, {3, 0}, {4, 5}, {5, 6},
                                             {6, 7}, {7, 4}, {0, 4}, {1, 5}, {2, 6}, {3, 7}};
            for (const auto& e : edges)
                out.push_back({c[e[0]], c[e[1]], b.color, b.width});
        }
    }

    void Perlin::SetupModAdvanced() {
        auto& mm = mods::ModManager::Instance();
        using abi::Op;
        using abi::Status;
        using mods::CallContext;

        mm.Register(Op::CameraControl, [this](CallContext&, abi::Reader& r, abi::Writer&) {
            const abi::CameraControlState s = r.read<abi::CameraControlState>();
            if (!r.Ok())
                return Status::BadArguments;
            std::lock_guard lock(modAdv.mutex);
            modAdv.camera = s;
            modAdv.cameraUntil = After(s.ticks);
            return Status::Ok;
        });

        mm.Register(Op::CameraState, [this](CallContext&, abi::Reader&, abi::Writer& w) {
            abi::CameraInfo info;
            const glm::vec3 eye = camera.EyePosition();
            info.x = eye.x;
            info.y = eye.y;
            info.z = eye.z;
            info.yaw = camera.yaw - 90.0f;
            info.pitch = -camera.pitch;
            info.fov = camera.fov;
            info.aspect = camera.aspect;
            info.nearPlane = camera.nearPlane;
            info.farPlane = camera.farPlane;
            info.perspective = static_cast<int32_t>(camera.mode);
            {
                std::lock_guard lock(modAdv.mutex);
                info.viewProjection = modAdv.viewProjection;
            }
            w.write(info);
            return Status::Ok;
        });

        mm.Register(Op::WorldToScreen, [this](CallContext&, abi::Reader& r, abi::Writer& w) {
            const abi::WorldPoint p = r.read<abi::WorldPoint>();
            if (!r.Ok())
                return Status::BadArguments;
            std::array<float, 16> m;
            {
                std::lock_guard lock(modAdv.mutex);
                m = modAdv.viewProjection;
            }
            const glm::mat4 vp = glm::make_mat4(m.data());
            const glm::vec4 clip = vp * glm::vec4(static_cast<float>(p.x), static_cast<float>(p.y),
                                                  static_cast<float>(p.z), 1.0f);
            abi::ScreenPoint out;
            if (clip.w > 0.0001f) {
                const float nx = clip.x / clip.w, ny = clip.y / clip.w;
                out.x = (nx * 0.5f + 0.5f) * static_cast<float>(modGuiW);
                out.y = (1.0f - (ny * 0.5f + 0.5f)) * static_cast<float>(modGuiH);
                out.depth = clip.w;
                out.visible = std::abs(nx) <= 1.0f && std::abs(ny) <= 1.0f;
            }
            w.write(out);
            return Status::Ok;
        });

        mm.Register(Op::PlayerMotion, [this](CallContext&, abi::Reader& r, abi::Writer&) {
            const abi::MotionRequest m = r.read<abi::MotionRequest>();
            if (!r.Ok() || !std::isfinite(m.x) || !std::isfinite(m.y) || !std::isfinite(m.z))
                return Status::BadArguments;
            if (std::abs(m.x) > 64.0 || std::abs(m.y) > 64.0 || std::abs(m.z) > 64.0)
                return Status::BadArguments;
            player.ModMotion(m.add, m.x, m.y, m.z, m.axisX, m.axisY, m.axisZ);
            return Status::Ok;
        });

        mm.Register(Op::PlayerTeleport, [this](CallContext&, abi::Reader& r, abi::Writer&) {
            const abi::TeleportRequest t = r.read<abi::TeleportRequest>();
            if (!r.Ok() || !std::isfinite(t.x) || !std::isfinite(t.y) || !std::isfinite(t.z))
                return Status::BadArguments;
            player.ModTeleport(t.x, t.y, t.z, t.keepVelocity);
            return Status::Ok;
        });

        mm.Register(Op::PlayerSetLook, [this](CallContext&, abi::Reader& r, abi::Writer&) {
            const abi::LookSet l = r.read<abi::LookSet>();
            if (!r.Ok() || !std::isfinite(l.x) || !std::isfinite(l.y) || !std::isfinite(l.z))
                return Status::BadArguments;
            const glm::dvec3 d(l.x, l.y, l.z);
            if (glm::dot(d, d) < 1e-12)
                return Status::BadArguments;
            std::lock_guard lock(modAdv.mutex);
            modAdv.lookDir = glm::normalize(d);
            modAdv.lookTaken = true;
            return Status::Ok;
        });

        mm.Register(Op::PlayerModelPose, [this](CallContext&, abi::Reader& r, abi::Writer&) {
            const abi::ModelPose p = r.read<abi::ModelPose>();
            if (!r.Ok())
                return Status::BadArguments;
            std::lock_guard lock(modAdv.mutex);
            modAdv.pose = p;
            modAdv.poseUntil = After(p.ticks);
            return Status::Ok;
        });

        mm.Register(Op::WorldLines, [this](CallContext& ctx, abi::Reader& r, abi::Writer&) {
            const abi::WorldLines in = r.read<abi::WorldLines>();
            if (!r.Ok())
                return Status::BadArguments;
            std::lock_guard lock(modAdv.mutex);
            auto& set = modAdv.lines[ctx.plugin];
            set.until = After(in.ticks);
            set.lines.clear();
            for (const abi::WorldLine& l : in.lines) {
                if (set.lines.size() >= kMaxLinesPerPlugin)
                    break;
                set.lines.push_back(
                    {{l.x1, l.y1, l.z1}, {l.x2, l.y2, l.z2}, l.color, std::clamp(l.width, 0.1f, 16.0f)});
            }
            return Status::Ok;
        });

        mm.Register(Op::WorldBoxes, [this](CallContext& ctx, abi::Reader& r, abi::Writer&) {
            const abi::WorldBoxes in = r.read<abi::WorldBoxes>();
            if (!r.Ok())
                return Status::BadArguments;
            std::lock_guard lock(modAdv.mutex);
            auto& set = modAdv.lines[ctx.plugin];
            set.until = After(in.ticks);
            set.lines.clear();
            for (abi::WorldBox b : in.boxes) {
                if (set.lines.size() + 12 > kMaxLinesPerPlugin)
                    break;
                b.width = std::clamp(b.width, 0.1f, 16.0f);
                AddBoxLines(set.lines, b);
            }
            return Status::Ok;
        });

        mm.Register(Op::ClientBlockGet, [](CallContext&, abi::Reader& r, abi::Writer& w) {
            const abi::BlockQuery q = r.read<abi::BlockQuery>();
            if (!r.Ok() || q.name.empty() || q.name.size() > 128 || q.states.size() > 32)
                return Status::BadArguments;
            const std::string name = !q.name.contains(':') ? "minecraft:" + q.name : q.name;
            BlockPalette& palette = BlockPalette::Instance();
            abi::BlockQueryResult out;
            const size_t count = palette.Count();
            for (size_t local = 1; local < count && !out.found; local++) {
                const uint16_t id = static_cast<uint16_t>(local);
                if (palette.Name(id) != name)
                    continue;
                bool match = true;
                for (const auto& [key, value] : q.states) {
                    bool have = false;
                    for (const auto& s : palette.States(id))
                        if (s.first == key) {
                            const std::string text = s.second.kind == BlockPalette::StateValue::String
                                                         ? s.second.s
                                                     : s.second.kind == BlockPalette::StateValue::Bool
                                                         ? (s.second.i ? "true" : "false")
                                                         : std::to_string(s.second.i);
                            have = text == value;
                        }
                    if (!have) {
                        match = false;
                        break;
                    }
                }
                if (match) {
                    out.found = true;
                    out.runtimeId = static_cast<uint32_t>(palette.RuntimeId(id));
                }
            }
            w.write(out);
            return Status::Ok;
        });

        mm.Register(Op::ClientBlockSet, [this](CallContext&, abi::Reader& r, abi::Writer&) {
            const abi::ClientBlockEdits in = r.read<abi::ClientBlockEdits>();
            if (!r.Ok() || in.edits.size() > 100000)
                return Status::BadArguments;
            if (!modLevel)
                return Status::Unavailable;
            auto edits = std::make_shared<std::vector<abi::ClientBlockEdit>>(in.edits);
            mods::ModManager::Instance().PostMain([this, edits] {
                if (!modLevel)
                    return;
                BlockPalette& palette = BlockPalette::Instance();
                for (const abi::ClientBlockEdit& e : *edits)
                    modLevel->SetBlock(e.x, e.y, e.z,
                                       palette.InternRuntime(static_cast<int32_t>(e.runtimeId)));
            });
            return Status::Ok;
        });

        mm.Register(Op::PacketDrop, [this](CallContext& ctx, abi::Reader& r, abi::Writer&) {
            const abi::PacketDropRequest in = r.read<abi::PacketDropRequest>();
            if (!r.Ok() || in.ids.size() > 4096)
                return Status::BadArguments;
            std::lock_guard lock(modAdv.mutex);
            (in.outgoing ? modAdv.outDrops : modAdv.drops)[ctx.plugin] = {After(in.ticks), in.ids};
            return Status::Ok;
        });

        mm.Register(Op::PacketInject, [this](CallContext&, abi::Reader& r, abi::Writer&) {
            const uint32_t id = r.get<uint32_t>();
            std::vector<uint8_t> body = r.bytes();
            if (!r.Ok())
                return Status::BadArguments;
            if (!modLive)
                return Status::Unavailable;
            modLive->InjectIncoming(id, body);
            return Status::Ok;
        });

        mm.Register(Op::PacketCounters, [this](CallContext&, abi::Reader&, abi::Writer& w) {
            if (!modLive)
                return Status::Unavailable;
            std::vector<abi::PacketCount> list;
            for (const auto& [id, n] : modLive->PacketCounts())
                list.push_back({id, n});
            w.write(list);
            return Status::Ok;
        });
    }

    void Perlin::ApplyModCamera(float& fov, glm::mat4& viewEffect) {
        const auto now = Clock::now();
        std::lock_guard lock(modAdv.mutex);
        if (now < modAdv.cameraUntil) {
            const abi::CameraControlState& c = modAdv.camera;
            if (c.fovSet > 0.0f)
                fov = std::clamp(c.fovSet, 1.0f, 170.0f);
            fov = std::clamp(fov + c.fovAdd, 1.0f, 170.0f);
            glm::mat4 extra(1.0f);
            if (c.roll != 0.0f)
                extra = glm::rotate(extra, glm::radians(c.roll), glm::vec3(0.0f, 0.0f, 1.0f));
            if (c.pitchAdd != 0.0f)
                extra = glm::rotate(extra, glm::radians(-c.pitchAdd), glm::vec3(1.0f, 0.0f, 0.0f));
            if (c.yawAdd != 0.0f)
                extra = glm::rotate(extra, glm::radians(c.yawAdd), glm::vec3(0.0f, 1.0f, 0.0f));
            extra = glm::translate(extra, glm::vec3(-c.offsetX, -c.offsetY, -c.offsetZ));
            viewEffect = extra * viewEffect;
        }
        modAdv.frameLines.clear();
        for (auto it = modAdv.lines.begin(); it != modAdv.lines.end();) {
            if (now >= it->second.until) {
                it = modAdv.lines.erase(it);
                continue;
            }
            modAdv.frameLines.insert(modAdv.frameLines.end(), it->second.lines.begin(),
                                     it->second.lines.end());
            ++it;
        }
        std::vector<uint32_t> drops;
        for (auto it = modAdv.drops.begin(); it != modAdv.drops.end();) {
            if (now >= it->second.first) {
                it = modAdv.drops.erase(it);
                continue;
            }
            drops.insert(drops.end(), it->second.second.begin(), it->second.second.end());
            ++it;
        }
        std::vector<uint32_t> outDrops;
        for (auto it = modAdv.outDrops.begin(); it != modAdv.outDrops.end();) {
            if (now >= it->second.first) {
                it = modAdv.outDrops.erase(it);
                continue;
            }
            outDrops.insert(outDrops.end(), it->second.second.begin(), it->second.second.end());
            ++it;
        }
        if (session) {
            session->SetDroppedPackets(std::move(drops));
            session->SetDroppedOutgoing(std::move(outDrops));
        }
    }
}
