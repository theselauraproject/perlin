#include "client/features/mods/ModsInternal.hpp"
#include "client/features/mods/ModHost.hpp"

#include "client/systems/attachable/AnimationStyle.hpp"
#include "client/systems/player/Interaction.hpp"
#include "client/systems/renderer/chunk/ChunkMesher.hpp"
#include "world/level/block/BlockPalette.hpp"

#include <algorithm>
#include <cmath>
#include <string_view>
#include <unordered_map>

namespace perlin {
    namespace {
        using Clock = std::chrono::steady_clock;

        Clock::time_point After(int32_t ticks) {
            return Clock::now() + std::chrono::milliseconds(std::clamp(ticks, 1, 200) * 50);
        }

        bool Finite(double v) {
            return std::isfinite(v);
        }

        std::string_view Short(std::string_view name) {
            if (name.starts_with("minecraft:"))
                name.remove_prefix(10);
            return name;
        }

        struct Pattern {
            enum class Kind { Exact, Suffix, Prefix, Contains } kind = Kind::Exact;
            std::string text;
            bool namespaced = false;

            static Pattern From(std::string_view raw) {
                Pattern p;
                const bool lead = raw.starts_with('*');
                const bool tail = raw.size() > 1 && raw.ends_with('*');
                if (lead)
                    raw.remove_prefix(1);
                if (tail)
                    raw.remove_suffix(1);
                p.kind = lead && tail ? Kind::Contains : lead ? Kind::Suffix : tail ? Kind::Prefix : Kind::Exact;
                p.namespaced = raw.contains(':') && !raw.starts_with("minecraft:");
                p.text = std::string(p.namespaced ? raw : Short(raw));
                return p;
            }

            bool Matches(std::string_view full) const {
                const std::string_view name = namespaced ? full : Short(full);
                switch (kind) {
                case Kind::Exact:
                    return name == text;
                case Kind::Suffix:
                    return name.ends_with(text);
                case Kind::Prefix:
                    return name.starts_with(text);
                case Kind::Contains:
                    return name.contains(text);
                }
                return false;
            }
        };

        bool IsLiquidName(std::string_view full) {
            const std::string_view n = Short(full);
            return n == "water" || n == "flowing_water" || n == "lava" || n == "flowing_lava";
        }

        std::string StateText(const BlockPalette::StateValue& v) {
            if (v.kind == BlockPalette::StateValue::String)
                return v.s;
            if (v.kind == BlockPalette::StateValue::Bool)
                return v.i ? "true" : "false";
            return std::to_string(v.i);
        }
    }

    void ModHost::RebuildBlockRemap() {
        modRev2.remapState = {};
        if (modRev2.remaps.empty()) {
            MesherSetBlockRemap(nullptr);
            modRev2.remapPaletteCount = 0;
            return;
        }
        BlockPalette& palette = ModPalette();
        const size_t count = palette.Count();
        modRev2.remapPaletteCount = count;

        struct Rule {
            Pattern match;
            std::string to;
        };
        std::vector<Rule> rules;
        bool onlyListed = false, keepLiquids = true, fullbright = false;
        for (const auto& [key, set] : modRev2.remaps) {
            for (const abi::BlockRemapRule& r : set.rules)
                if (!r.match.empty())
                    rules.push_back({Pattern::From(r.match), r.to});
            if (set.onlyListed) {
                onlyListed = true;
                keepLiquids = keepLiquids && set.keepLiquids;
            }
            fullbright = fullbright || set.fullbright;
        }

        std::vector<std::string> names(count);
        std::unordered_map<std::string, std::vector<uint16_t>> byName;
        for (size_t local = 1; local < count; local++) {
            names[local] = palette.Name(static_cast<uint16_t>(local));
            byName[std::string(Short(names[local]))].push_back(static_cast<uint16_t>(local));
        }
        const auto resolve = [&](const std::string& to, uint16_t from) -> uint16_t {
            const auto it = byName.find(std::string(Short(to)));
            if (it == byName.end() || it->second.empty())
                return from;
            if (it->second.size() == 1)
                return it->second.front();
            const auto source = palette.States(from);
            uint16_t best = it->second.front();
            int bestScore = -1;
            for (const uint16_t candidate : it->second) {
                int score = 0;
                for (const auto& [k, v] : palette.States(candidate))
                    for (const auto& [sk, sv] : source)
                        if (sk == k && StateText(sv) == StateText(v))
                            score++;
                if (score > bestScore) {
                    bestScore = score;
                    best = candidate;
                }
            }
            return best;
        };

        auto table = std::make_shared<BlockRemapTable>();
        table->fullbright = fullbright;
        table->map.resize(count);
        for (size_t local = 0; local < count; local++)
            table->map[local] = static_cast<uint16_t>(local);
        for (size_t local = 1; local < count; local++) {
            const std::string& name = names[local];
            if (name.empty() || Short(name) == "air")
                continue;
            const Rule* hit = nullptr;
            for (const Rule& r : rules)
                if (r.match.Matches(name)) {
                    hit = &r;
                    break;
                }
            if (hit) {
                if (hit->to == "*")
                    continue;
                if (hit->to.empty()) {
                    table->map[local] = 0;
                    modRev2.remapState.hidden++;
                } else {
                    const uint16_t to = resolve(hit->to, static_cast<uint16_t>(local));
                    table->map[local] = to;
                    if (to != local)
                        modRev2.remapState.replaced++;
                }
            } else if (onlyListed && !(keepLiquids && IsLiquidName(name))) {
                table->map[local] = 0;
                modRev2.remapState.hidden++;
            }
        }
        modRev2.remapState.active = true;
        modRev2.remapState.fullbright = fullbright;
        MesherSetBlockRemap(std::move(table));
    }

    bool ModHost::DetachTakesLook() {
        std::lock_guard lock(modRev2.mutex);
        return modRev2.detachActive && modRev2.detach.mouseLook && Clock::now() < modRev2.detachUntil;
    }

    bool ModHost::CameraDetachActive() {
        std::lock_guard lock(modRev2.mutex);
        return modRev2.detachActive && Clock::now() < modRev2.detachUntil;
    }

    bool ModHost::DetachedPlayerView(glm::vec3& eye, float& yaw, float& pitch) {
        std::lock_guard lock(modRev2.mutex);
        if (!modRev2.detachActive || Clock::now() >= modRev2.detachUntil)
            return false;
        eye = modRev2.playerEye;
        yaw = modRev2.playerYaw;
        pitch = modRev2.playerPitch;
        return true;
    }

    void ModHost::DetachLook(float dyaw, float dpitch) {
        std::lock_guard lock(modRev2.mutex);
        modRev2.detachYaw += dyaw;
        modRev2.detachPitch = std::clamp(modRev2.detachPitch + dpitch, -89.9f, 89.9f);
    }

    bool ModHost::DetachCamera(float dt, glm::vec3 eye, PerspectiveMode& mode, bool& showSelf) {
        std::lock_guard lock(modRev2.mutex);
        const abi::CameraDetachState& d = modRev2.detach;
        if (Clock::now() >= modRev2.detachUntil || d.mode == abi::CameraMode::Attached) {
            if (modRev2.detachActive) {
                modRev2.detachActive = false;
                deps.inputManager.SetModFreeze(false);
            }
            return false;
        }
        const bool free = d.mode == abi::CameraMode::Free;
        modRev2.playerEye = eye;
        modRev2.playerYaw = deps.camera.yaw;
        modRev2.playerPitch = deps.camera.pitch;
        if (!modRev2.detachActive) {
            modRev2.detachActive = true;
            modRev2.detachYaw = deps.camera.yaw;
            modRev2.detachPitch = deps.camera.pitch;
            const bool given = d.x != 0.0 || d.y != 0.0 || d.z != 0.0;
            modRev2.detachPos = free && given ? glm::dvec3(d.x, d.y, d.z) : glm::dvec3(eye);
        }
        if (!d.mouseLook) {
            modRev2.detachYaw = d.yaw + 90.0f;
            modRev2.detachPitch = std::clamp(-d.pitch, -89.9f, 89.9f);
        }
        deps.inputManager.SetModFreeze(d.freezeMovement || (free && d.keyFly));

        const float yr = glm::radians(modRev2.detachYaw), pr = glm::radians(modRev2.detachPitch);
        const glm::dvec3 forward(std::cos(yr) * std::cos(pr), std::sin(pr), std::sin(yr) * std::cos(pr));
        glm::dvec3 position = eye;
        if (free) {
            if (d.keyFly) {
                const InputManager& im = deps.inputManager;
                const glm::dvec3 flat = glm::normalize(glm::dvec3(std::cos(yr), 0.0, std::sin(yr)));
                const glm::dvec3 right(-flat.z, 0.0, flat.x);
                glm::dvec3 move(0.0);
                if (im.IsDownUnfrozen(Action::Forward))
                    move += forward;
                if (im.IsDownUnfrozen(Action::Back))
                    move -= forward;
                if (im.IsDownUnfrozen(Action::Right))
                    move += right;
                if (im.IsDownUnfrozen(Action::Left))
                    move -= right;
                if (im.IsDownUnfrozen(Action::Jump))
                    move.y += 1.0;
                if (im.IsDownUnfrozen(Action::Sneak))
                    move.y -= 1.0;
                if (glm::dot(move, move) > 1e-9) {
                    const double speed = std::clamp(static_cast<double>(d.speed), 0.0, 200.0) *
                                         (im.IsDownUnfrozen(Action::Sprint) ? 3.0 : 1.0);
                    modRev2.detachPos += glm::normalize(move) * speed * static_cast<double>(std::clamp(dt, 0.0f, 0.25f));
                }
            } else {
                modRev2.detachPos = glm::dvec3(d.x, d.y, d.z);
            }
            position = modRev2.detachPos;
        } else if (d.distance > 0.0f) {
            double distance = std::clamp(static_cast<double>(d.distance), 0.0, 64.0);
            if (d.collide && modLevel && deps.levelRenderer) {
                const AimHit hit = Raycast(
                    *modLevel, &deps.levelRenderer->Blocks(), {},
                    [](const entity::Entity&) { return glm::vec2(0.6f, 1.8f); }, glm::dvec3(eye), -forward,
                    distance);
                if (hit.kind == AimHit::Kind::Block)
                    distance = std::max(0.1, hit.distance - 0.2);
            }
            position = glm::dvec3(eye) - forward * distance;
        }
        modRev2.detachPos = free ? modRev2.detachPos : position;

        deps.camera.x = static_cast<float>(position.x);
        deps.camera.y = static_cast<float>(position.y);
        deps.camera.z = static_cast<float>(position.z);
        deps.camera.yaw = modRev2.detachYaw;
        deps.camera.pitch = modRev2.detachPitch;
        mode = PerspectiveMode::FirstPerson;
        showSelf = d.showSelf && (free || d.distance > 0.0f);
        return true;
    }

    void ModHost::PrepareModBones(entity::EntityRenderer::FrameIn& in) {
        const auto now = Clock::now();
        std::lock_guard lock(modRev2.mutex);
        modRev2.frameBones.clear();
        std::erase_if(modRev2.bones, [&](const auto& p) { return now >= p.second.until; });
        for (const auto& [key, set] : modRev2.bones) {
            auto& out = modRev2.frameBones[key.second];
            out.insert(out.end(), set.bones.begin(), set.bones.end());
        }
        in.modBones = modRev2.frameBones.empty() ? nullptr : &modRev2.frameBones;
        if (modRev2.detachActive && now < modRev2.detachUntil) {
            const abi::CameraDetachState& d = modRev2.detach;
            const bool showSelf = d.showSelf && (d.mode == abi::CameraMode::Free || d.distance > 0.0f);
            if (!showSelf && modLive) {
                entity::EntityRenderer::FrameIn::ModStyle style;
                style.hidden = true;
                modFrameStyles[modLive->GetRuntimeId()] = std::move(style);
            }
        }
    }

    void ModHost::PumpModRev2() {
        const auto now = Clock::now();
        std::lock_guard lock(modRev2.mutex);
        int first = 0, third = 0;
        std::erase_if(modRev2.styles, [&](const auto& p) { return now >= p.second.first; });
        for (const auto& [plugin, entry] : modRev2.styles) {
            if (entry.second.firstPerson != abi::AnimationStyle::Default)
                first = static_cast<int>(entry.second.firstPerson);
            if (entry.second.thirdPerson != abi::AnimationStyle::Default)
                third = static_cast<int>(entry.second.thirdPerson);
        }
        anim::gFirstPersonStyle.store(first, std::memory_order_relaxed);
        anim::gThirdPersonStyle.store(third, std::memory_order_relaxed);
        if (!modRev2.remaps.empty() && ModPalette().Count() != modRev2.remapPaletteCount)
            RebuildBlockRemap();
    }

    void ModHost::DropModRev2(const std::string& plugin) {
        std::lock_guard lock(modRev2.mutex);
        if (modRev2.detachPlugin == plugin) {
            modRev2.detachPlugin.clear();
            modRev2.detachUntil = {};
        }
        const std::string prefix = plugin + "/";
        const size_t removed = std::erase_if(modRev2.remaps, [&](const auto& p) { return p.first.starts_with(prefix); });
        if (removed)
            RebuildBlockRemap();
        std::erase_if(modRev2.bones, [&](const auto& p) { return p.first.first == plugin; });
        modRev2.styles.erase(plugin);
    }

    void ModHost::SetupModRev2() {
        using abi::Op;
        using abi::Status;
        using mods::CallContext;

        scope.Register(Op::CameraDetach, [this](CallContext& ctx, abi::Reader& r, abi::Writer&) {
            abi::CameraDetachState s = r.read<abi::CameraDetachState>();
            if (!r.Ok() || !Finite(s.x) || !Finite(s.y) || !Finite(s.z) || !std::isfinite(s.yaw) ||
                !std::isfinite(s.pitch) || !std::isfinite(s.roll) || !std::isfinite(s.distance) ||
                !std::isfinite(s.fov) || !std::isfinite(s.speed))
                return Status::BadArguments;
            if (std::abs(s.x) > 3.0e7 || std::abs(s.z) > 3.0e7 || std::abs(s.y) > 1.0e5)
                return Status::BadArguments;
            s.distance = std::clamp(s.distance, 0.0f, 64.0f);
            s.fov = s.fov <= 0.0f ? 0.0f : std::clamp(s.fov, 1.0f, 170.0f);
            s.speed = std::clamp(s.speed, 0.0f, 200.0f);
            std::lock_guard lock(modRev2.mutex);
            if (modRev2.detachActive && modRev2.detach.mode != s.mode)
                modRev2.detachActive = false;
            modRev2.detachPlugin = ctx.plugin;
            modRev2.detach = s;
            modRev2.detachUntil = s.mode == abi::CameraMode::Attached ? Clock::time_point{} : After(s.ticks);
            return Status::Ok;
        });

        scope.Register(Op::CameraDetachGet, [this](CallContext&, abi::Reader&, abi::Writer& w) {
            std::lock_guard lock(modRev2.mutex);
            abi::CameraDetachState out;
            if (modRev2.detachActive && Clock::now() < modRev2.detachUntil) {
                out = modRev2.detach;
                out.x = modRev2.detachPos.x;
                out.y = modRev2.detachPos.y;
                out.z = modRev2.detachPos.z;
                out.yaw = modRev2.detachYaw - 90.0f;
                out.pitch = -modRev2.detachPitch;
            }
            w.write(out);
            return Status::Ok;
        });

        scope.Register(Op::BlockRemapSet, [this](CallContext& ctx, abi::Reader& r, abi::Writer&) {
            abi::BlockRemapData d = r.read<abi::BlockRemapData>();
            if (!r.Ok() || d.id.empty() || d.id.size() > 64 || d.rules.size() > 4096)
                return Status::BadArguments;
            for (const abi::BlockRemapRule& rule : d.rules)
                if (rule.match.empty() || rule.match.size() > 128 || rule.to.size() > 128)
                    return Status::BadArguments;
            const std::string key = ctx.plugin + "/" + d.id;
            std::lock_guard lock(modRev2.mutex);
            if (!modRev2.remaps.contains(key) && modRev2.remaps.size() >= 64)
                return Status::Failed;
            modRev2.remaps[key] = std::move(d);
            RebuildBlockRemap();
            return Status::Ok;
        });

        scope.Register(Op::BlockRemapClear, [this](CallContext& ctx, abi::Reader& r, abi::Writer&) {
            const std::string id = r.str();
            if (!r.Ok())
                return Status::BadArguments;
            std::lock_guard lock(modRev2.mutex);
            const std::string prefix = ctx.plugin + "/";
            const size_t removed =
                id.empty() ? std::erase_if(modRev2.remaps, [&](const auto& p) { return p.first.starts_with(prefix); })
                           : modRev2.remaps.erase(prefix + id);
            if (removed)
                RebuildBlockRemap();
            return Status::Ok;
        });

        scope.Register(Op::BlockRemapState, [this](CallContext&, abi::Reader&, abi::Writer& w) {
            std::lock_guard lock(modRev2.mutex);
            w.write(modRev2.remapState);
            return Status::Ok;
        });

        scope.Register(Op::EntityBonePose, [this](CallContext& ctx, abi::Reader& r, abi::Writer&) {
            const abi::BonePoseData d = r.read<abi::BonePoseData>();
            if (!r.Ok() || d.bones.size() > 128)
                return Status::BadArguments;
            ModRev2::BoneSet set;
            set.until = After(d.ticks);
            set.bones.reserve(d.bones.size());
            for (const abi::BonePoseEntry& b : d.bones) {
                for (const float v : {b.rx, b.ry, b.rz, b.px, b.py, b.pz, b.sx, b.sy, b.sz})
                    if (!std::isfinite(v) || std::abs(v) > 100000.0f)
                        return Status::BadArguments;
                if (b.bone.empty() || b.bone.size() > 64)
                    return Status::BadArguments;
                entity::EntityRenderer::FrameIn::ModBone m;
                m.bone = b.bone;
                for (char& c : m.bone)
                    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                m.replace = b.blend == abi::BoneBlend::Replace;
                m.rotation = {b.rx, b.ry, b.rz};
                m.position = {b.px, b.py, b.pz};
                m.scale = {b.sx, b.sy, b.sz};
                set.bones.push_back(std::move(m));
            }
            uint64_t entity = d.entity;
            if (modLive && entity == modLive->GetRuntimeId())
                entity = 0;
            std::lock_guard lock(modRev2.mutex);
            const auto key = std::make_pair(ctx.plugin, entity);
            if (!modRev2.bones.contains(key) && modRev2.bones.size() >= 512)
                return Status::Failed;
            modRev2.bones[key] = std::move(set);
            return Status::Ok;
        });

        scope.Register(Op::EntityBonePoseClear, [this](CallContext& ctx, abi::Reader& r, abi::Writer&) {
            uint64_t entity = r.get<uint64_t>();
            if (!r.Ok())
                return Status::BadArguments;
            if (modLive && entity == modLive->GetRuntimeId())
                entity = 0;
            std::lock_guard lock(modRev2.mutex);
            modRev2.bones.erase(std::make_pair(ctx.plugin, entity));
            return Status::Ok;
        });

        scope.Register(Op::AnimationStyleSet, [this](CallContext& ctx, abi::Reader& r, abi::Writer&) {
            const abi::AnimationStyleData d = r.read<abi::AnimationStyleData>();
            if (!r.Ok())
                return Status::BadArguments;
            const auto valid = [](abi::AnimationStyle s) {
                return s == abi::AnimationStyle::Default || s == abi::AnimationStyle::Java ||
                       s == abi::AnimationStyle::Bedrock;
            };
            if (!valid(d.firstPerson) || !valid(d.thirdPerson))
                return Status::BadArguments;
            {
                std::lock_guard lock(modRev2.mutex);
                if (d.firstPerson == abi::AnimationStyle::Default && d.thirdPerson == abi::AnimationStyle::Default)
                    modRev2.styles.erase(ctx.plugin);
                else
                    modRev2.styles[ctx.plugin] = {d.ticks > 0 ? After(d.ticks) : Clock::time_point::max(), d};
            }
            PumpModRev2();
            return Status::Ok;
        });

        scope.Register(Op::HandState, [this](CallContext&, abi::Reader&, abi::Writer& w) {
            const float partial = deps.player.PartialTick();
            const HandRenderer::RigState rig = deps.handRenderer.Rig(partial);
            abi::HandStateData out;
            out.swing = rig.attackTime;
            out.offSwing = deps.handRenderer.OffSwingProgress(partial);
            out.equip = std::clamp(rig.armHeight, 0.0f, 1.0f);
            out.usingItem = rig.using_;
            out.useTicks = static_cast<int32_t>(rig.usedTicks);
            out.useOffhand = deps.handRenderer.UseOffhandAnim();
            out.blocking = rig.blocking;
            out.mainItem = rig.main;
            out.offItem = rig.off;
            out.leftHanded = ModHand().leftHanded;
            out.firstPerson = deps.camera.mode == PerspectiveMode::FirstPerson;
            out.sneaking = deps.player.Sneaking();
            out.sprinting = deps.player.Sprinting();
            out.style = anim::ClassicFirstPerson() ? abi::AnimationStyle::Java : abi::AnimationStyle::Bedrock;
            w.write(out);
            return Status::Ok;
        });
    }
}
