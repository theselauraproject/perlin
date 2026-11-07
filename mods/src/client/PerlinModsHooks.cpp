#include "client/PerlinInternal.hpp"
#include "client/Perlin.hpp"

#include "client/mods/ModManager.hpp"
#include "client/platform/AppPlatform.hpp"
#include "client/resources/TextureLoader.hpp"
#include "client/ui/HudRenderers.hpp"
#include "client/ui/ItemTooltip.hpp"
#include "network/RakNet.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <thread>

namespace perlin {
    namespace {
        using Clock = std::chrono::steady_clock;
        namespace fs = std::filesystem;

        Clock::time_point After(int32_t ticks) {
            return Clock::now() + std::chrono::milliseconds(std::clamp(ticks, 1, 200) * 50);
        }

        constexpr size_t kMaxMeshVertices = 60000;
        constexpr size_t kMaxEntityRules = 512;

        // in abi::HudElement order
        constexpr const char* kHudRenderers[] = {
            "heart_renderer",       "armor_renderer",        "hunger_renderer", "bubbles_renderer",
            "hotbar_renderer",      "progress_bar_renderer", "debug_renderer",  "chat_renderer",
            "mob_effects_renderer", "locator_bar",           "fire_renderer",   "subtitle_renderer",
            "cursor_renderer"};

        void ToRgba(uint32_t argb, float out[4]) {
            out[0] = ((argb >> 16) & 0xFF) / 255.0f;
            out[1] = ((argb >> 8) & 0xFF) / 255.0f;
            out[2] = (argb & 0xFF) / 255.0f;
            out[3] = ((argb >> 24) & 0xFF) / 255.0f;
        }

        uint32_t ArgbToAbgr(uint32_t c) {
            return (c & 0xFF00FF00u) | ((c >> 16) & 0xFFu) | ((c & 0xFFu) << 16);
        }

        std::string ItemKey(const ui::HudItem& i) {
            return i.name + "|" + std::to_string(i.aux) + "|" + std::to_string(i.count) + "|" + i.customName;
        }

        abi::Item ToAbi(const ui::HudItem& s) {
            abi::Item i;
            if (s.Empty())
                return i;
            i.name = s.name;
            i.count = s.count;
            i.aux = s.aux;
            i.damage = s.maxDamage > 0 ? s.aux : 0;
            i.enchanted = s.enchanted;
            i.customName = s.customName;
            for (const auto& e : s.enchants)
                i.enchants.emplace_back(e.first, e.second);
            i.lore = s.lore;
            return i;
        }
    }

    void Perlin::SetupModHooks() {
        auto& mm = mods::ModManager::Instance();
        using abi::Op;
        using abi::Status;
        using mods::CallContext;

        ui::SetHudElementFilter([this](std::string_view renderer, float& dx, float& dy) {
            std::lock_guard lock(modHooks.mutex);
            for (const auto& [plugin, elements] : modHooks.hud)
                for (const auto& [element, state] : elements) {
                    if (renderer != kHudRenderers[static_cast<int>(element)])
                        continue;
                    if (!state.visible)
                        return false;
                    dx += state.dx;
                    dy += state.dy;
                }
            return true;
        });

        // tooltips rebuilt every frame while hovered: ask mods once per item, cache briefly
        ui::SetTooltipExtender([this](const ui::HudItem& item) {
            static std::string lastKey;
            static std::vector<std::string> lastLines;
            static Clock::time_point lastAt{};
            auto& mods = mods::ModManager::Instance();
            if (!mods.Wants(abi::Event::Tooltip))
                return std::vector<std::string>{};
            const std::string key = ItemKey(item);
            if (key == lastKey && Clock::now() - lastAt < std::chrono::milliseconds(250))
                return lastLines;
            {
                std::lock_guard lock(modHooks.mutex);
                modHooks.tooltipLines.clear();
            }
            abi::EventTooltip e;
            e.item = ToAbi(item);
            abi::Writer w;
            w.write(e);
            mods.Dispatch(abi::Event::Tooltip, w, 20.0);
            std::lock_guard lock(modHooks.mutex);
            lastKey = key;
            lastAt = Clock::now();
            lastLines = std::move(modHooks.tooltipLines);
            modHooks.tooltipLines.clear();
            return lastLines;
        });

        mm.Register(Op::HudElementSet, [this](CallContext& ctx, abi::Reader& r, abi::Writer&) {
            const abi::HudElementState s = r.read<abi::HudElementState>();
            const int index = static_cast<int>(s.element);
            if (!r.Ok() || index < 0 || index >= static_cast<int>(std::size(kHudRenderers)) ||
                !std::isfinite(s.dx) || !std::isfinite(s.dy))
                return Status::BadArguments;
            {
                std::lock_guard lock(modHooks.mutex);
                auto& mine = modHooks.hud[ctx.plugin];
                if (s.visible && s.dx == 0 && s.dy == 0)
                    mine.erase(s.element);
                else
                    mine[s.element] = s;
            }
            ui::BumpHudFilter();
            return Status::Ok;
        });

        mm.Register(Op::DebugLines, [this](CallContext& ctx, abi::Reader& r, abi::Writer&) {
            abi::DebugLinesData d = r.read<abi::DebugLinesData>();
            if (!r.Ok() || d.left.size() + d.right.size() > 64)
                return Status::BadArguments;
            std::lock_guard lock(modHooks.mutex);
            if (d.left.empty() && d.right.empty())
                modHooks.debug.erase(ctx.plugin);
            else
                modHooks.debug[ctx.plugin] = std::move(d);
            return Status::Ok;
        });

        mm.Register(Op::FogSet, [this](CallContext&, abi::Reader& r, abi::Writer&) {
            const abi::FogOverride f = r.read<abi::FogOverride>();
            if (!r.Ok() || !std::isfinite(f.start) || !std::isfinite(f.end))
                return Status::BadArguments;
            std::lock_guard lock(modHooks.mutex);
            modHooks.fog = {After(f.ticks), f};
            return Status::Ok;
        });

        mm.Register(Op::ScreenTint, [this](CallContext&, abi::Reader& r, abi::Writer&) {
            abi::ScreenTintData t = r.read<abi::ScreenTintData>();
            if (!r.Ok() || !std::isfinite(t.vignette))
                return Status::BadArguments;
            t.vignette = std::clamp(t.vignette, 0.0f, 1.0f);
            std::lock_guard lock(modHooks.mutex);
            modHooks.tint = {After(t.ticks), t};
            return Status::Ok;
        });

        mm.Register(Op::EntityRenderOverride, [this](CallContext& ctx, abi::Reader& r, abi::Writer&) {
            abi::EntityOverride o = r.read<abi::EntityOverride>();
            if (!r.Ok() || (o.entity == 0 && o.type.empty()) || o.texture.size() > 256 ||
                o.nameText.size() > 256)
                return Status::BadArguments;
            if (!std::isfinite(o.scaleX) || !std::isfinite(o.scaleY) || !std::isfinite(o.scaleZ))
                return Status::BadArguments;
            o.scaleX = std::clamp(o.scaleX, 0.01f, 16.0f);
            o.scaleY = std::clamp(o.scaleY, 0.01f, 16.0f);
            o.scaleZ = std::clamp(o.scaleZ, 0.01f, 16.0f);
            if (!o.type.contains(':') && !o.type.empty())
                o.type = "minecraft:" + o.type;
            const std::string key = o.entity ? "id:" + std::to_string(o.entity) : "type:" + o.type;
            std::lock_guard lock(modHooks.mutex);
            auto& mine = modHooks.entities[ctx.plugin];
            if (!mine.contains(key) && mine.size() >= kMaxEntityRules)
                return Status::Failed;
            mine[key] = {After(o.ticks), std::move(o)};
            return Status::Ok;
        });

        mm.Register(Op::WorldMesh, [this](CallContext& ctx, abi::Reader& r, abi::Writer&) {
            abi::WorldMeshData m = r.read<abi::WorldMeshData>();
            if (!r.Ok() || m.id.empty() || m.id.size() > 64 || m.triangles.size() > kMaxMeshVertices ||
                m.triangles.size() % 3)
                return Status::BadArguments;
            for (const abi::MeshVertex& v : m.triangles)
                if (!std::isfinite(v.x) || !std::isfinite(v.y) || !std::isfinite(v.z))
                    return Status::BadArguments;
            std::lock_guard lock(modHooks.mutex);
            modHooks.meshes[ctx.plugin + "/" + m.id] = {After(m.ticks), std::move(m)};
            return Status::Ok;
        });

        mm.Register(Op::WorldMeshClear, [this](CallContext& ctx, abi::Reader& r, abi::Writer&) {
            const std::string id = r.str();
            if (!r.Ok())
                return Status::BadArguments;
            std::lock_guard lock(modHooks.mutex);
            modHooks.meshes.erase(ctx.plugin + "/" + id);
            return Status::Ok;
        });

        mm.Register(Op::FrameLimit, [this](CallContext& ctx, abi::Reader& r, abi::Writer&) {
            abi::FrameLimitData f = r.read<abi::FrameLimitData>();
            if (!r.Ok() || f.fps < 0 || f.unfocusedFps < 0)
                return Status::BadArguments;
            f.fps = std::min(f.fps, 1000);
            f.unfocusedFps = std::min(f.unfocusedFps, 1000);
            std::lock_guard lock(modHooks.mutex);
            if (f.fps == 0 && f.unfocusedFps == 0)
                modHooks.frame.erase(ctx.plugin);
            else
                modHooks.frame[ctx.plugin] = f;
            return Status::Ok;
        });

        mm.Register(Op::WindowState, [this](CallContext&, abi::Reader&, abi::Writer& w) {
            abi::WindowInfo info;
            {
                std::lock_guard lock(modHooks.mutex);
                info.focused = modHooks.windowFocused;
                info.minimized = modHooks.windowMinimized;
            }
            info.width = modFbW.load();
            info.height = modFbH.load();
            w.write(info);
            return Status::Ok;
        });

        mm.Register(Op::ScreenButtonAdd, [this](CallContext& ctx, abi::Reader& r, abi::Writer&) {
            abi::ScreenButtonDef def = r.read<abi::ScreenButtonDef>();
            if (!r.Ok() || def.id.empty() || def.id.size() > 64 || def.label.size() > 128 || def.width <= 0 ||
                def.height <= 0)
                return Status::BadArguments;
            std::lock_guard lock(modHooks.mutex);
            auto& list = modHooks.buttons;
            std::erase_if(list, [&](const ModHooks::Button& b) {
                return b.plugin == ctx.plugin && b.def.id == def.id;
            });
            if (std::count_if(list.begin(), list.end(),
                              [&](const ModHooks::Button& b) { return b.plugin == ctx.plugin; }) >= 32)
                return Status::Failed;
            list.push_back({ctx.plugin, std::move(def)});
            return Status::Ok;
        });

        mm.Register(Op::ScreenButtonRemove, [this](CallContext& ctx, abi::Reader& r, abi::Writer&) {
            const std::string id = r.str();
            if (!r.Ok())
                return Status::BadArguments;
            std::lock_guard lock(modHooks.mutex);
            std::erase_if(modHooks.buttons, [&](const ModHooks::Button& b) {
                return b.plugin == ctx.plugin && b.def.id == id;
            });
            return Status::Ok;
        });

        mm.Register(Op::ScreenOpenCustom, [this](CallContext& ctx, abi::Reader& r, abi::Writer&) {
            const std::string title = r.str();
            if (!r.Ok() || title.size() > 128)
                return Status::BadArguments;
            std::lock_guard lock(modHooks.mutex);
            modHooks.customOpen = true;
            modHooks.customOwner = ctx.plugin;
            modHooks.customTitle = title;
            return Status::Ok;
        });

        mm.Register(Op::TooltipAppend, [this](CallContext&, abi::Reader& r, abi::Writer&) {
            const std::vector<std::string> lines = r.read<std::vector<std::string>>();
            if (!r.Ok() || lines.size() > 16)
                return Status::BadArguments;
            std::lock_guard lock(modHooks.mutex);
            for (const std::string& l : lines)
                modHooks.tooltipLines.push_back(l.substr(0, 200));
            return Status::Ok;
        });

        mm.Register(Op::LoadingOverlay, [this](CallContext& ctx, abi::Reader& r, abi::Writer&) {
            const abi::LoadingOverlayData d = r.read<abi::LoadingOverlayData>();
            if (!r.Ok())
                return Status::BadArguments;
            std::lock_guard lock(modHooks.mutex);
            if (d.hidden)
                modHooks.loadingHidden.insert(ctx.plugin);
            else
                modHooks.loadingHidden.erase(ctx.plugin);
            return Status::Ok;
        });

        mm.Register(Op::SkinOverride, [this](CallContext&, abi::Reader& r, abi::Writer&) {
            abi::SkinOverrideData d = r.read<abi::SkinOverrideData>();
            if (!r.Ok() || d.player.size() > 64 || d.png.size() > (1u << 20))
                return Status::BadArguments;
            if (!d.png.empty()) {
                Texture t;
                if (!LoadTextureMemory(d.png.data(), d.png.size(), t) || t.width != 64 ||
                    (t.height != 64 && t.height != 32))
                    return Status::BadArguments;
            }
            std::lock_guard lock(modHooks.mutex);
            if (d.png.empty())
                modHooks.skins.erase(d.player);
            else
                modHooks.skins[d.player] = std::move(d);
            modHooks.skinSerial++;
            return Status::Ok;
        });

        mm.Register(Op::AmbientSound, [this](CallContext& ctx, abi::Reader& r, abi::Writer&) {
            abi::AmbientSoundData d = r.read<abi::AmbientSoundData>();
            if (!r.Ok() || d.id.empty() || d.id.size() > 64 || d.sound.empty() || d.sound.size() > 128 ||
                !std::isfinite(d.volume) || !std::isfinite(d.pitch))
                return Status::BadArguments;
            d.volume = std::clamp(d.volume, 0.0f, 4.0f);
            d.pitch = std::clamp(d.pitch, 0.1f, 4.0f);
            const std::string key = ctx.plugin + "/" + d.id;
            std::lock_guard lock(modHooks.mutex);
            if (!d.play) {
                if (const auto it = modHooks.ambient.find(key); it != modHooks.ambient.end()) {
                    sounds.Stop(it->second.sound);
                    modHooks.ambient.erase(it);
                }
            } else {
                modHooks.ambient[key] = std::move(d);
            }
            return Status::Ok;
        });

        mm.Register(Op::ServerPing, [this](CallContext& ctx, abi::Reader& r, abi::Writer& w) {
            const abi::ServerPingRequest q = r.read<abi::ServerPingRequest>();
            if (!r.Ok() || q.host.empty() || q.host.size() > 253 || q.port == 0)
                return Status::BadArguments;
            const uint32_t id = modHooks.nextPing++;
            std::thread([this, plugin = ctx.plugin, q, id] {
                const auto start = Clock::now();
                std::string ad, error;
                abi::EventServerPing e;
                e.id = id;
                e.ok = RakNet::Ping(q.host, q.port, ad, error, 2500);
                e.latencyMs = std::chrono::duration<float, std::milli>(Clock::now() - start).count();
                if (e.ok) {
                    // MCPE;motd;protocol;version;players;max;...
                    std::vector<std::string> f;
                    size_t from = 0;
                    for (size_t at; (at = ad.find(';', from)) != std::string::npos; from = at + 1)
                        f.push_back(ad.substr(from, at - from));
                    f.push_back(ad.substr(from));
                    if (f.size() > 1)
                        e.motd = f[1];
                    if (f.size() > 2)
                        e.protocol = std::atoi(f[2].c_str());
                    if (f.size() > 3)
                        e.version = f[3];
                    if (f.size() > 5) {
                        e.players = std::atoi(f[4].c_str());
                        e.maxPlayers = std::atoi(f[5].c_str());
                    }
                }
                std::lock_guard lock(modHooks.mutex);
                modHooks.pings.emplace_back(plugin, std::move(e));
            }).detach();
            w.write(id);
            return Status::Ok;
        });

        mm.Register(Op::PackRegister, [this](CallContext& ctx, abi::Reader& r, abi::Writer&) {
            const abi::PackRegistration p = r.read<abi::PackRegistration>();
            if (!r.Ok() || p.path.empty() || p.path.size() > 256 || p.path.contains("..") ||
                fs::path(p.path).is_absolute())
                return Status::BadArguments;
            const std::string full = (fs::path(ctx.storageRoot) / p.path).lexically_normal().string();
            std::error_code ec;
            if (p.enabled && !fs::exists(full, ec))
                return Status::NotFound;
            {
                std::lock_guard lock(modHooks.mutex);
                auto& packs = modHooks.packs;
                const auto it = std::find(packs.begin(), packs.end(), std::make_pair(ctx.plugin, full));
                if (p.enabled == (it != packs.end()))
                    return Status::Ok;
                if (p.enabled)
                    packs.emplace_back(ctx.plugin, full);
                else
                    packs.erase(it);
            }
            mods::ModManager::Instance().PostMain([this] { ApplyTexturePacks(); });
            return Status::Ok;
        });

        mm.Register(Op::ShaderPackSet, [this](CallContext&, abi::Reader& r, abi::Writer&) {
            const std::string name = r.str();
            if (!r.Ok() || name.size() > 128 || name.contains("..") || name.contains('/'))
                return Status::BadArguments;
            mods::ModManager::Instance().PostMain([this, name] {
                options.shaderPack = name;
                ApplyShaderPack();
            });
            return Status::Ok;
        });

        mm.Register(Op::ParticleSpawn, [this](CallContext&, abi::Reader& r, abi::Writer&) {
            abi::ParticleRequest p = r.read<abi::ParticleRequest>();
            if (!r.Ok() || p.name.empty() || p.name.size() > 128 || !std::isfinite(p.x) ||
                !std::isfinite(p.y) || !std::isfinite(p.z) || !std::isfinite(p.spread))
                return Status::BadArguments;
            p.count = std::clamp(p.count, 1, 64);
            p.spread = std::clamp(p.spread, 0.0f, 8.0f);
            std::lock_guard lock(modHooks.mutex);
            if (modHooks.particles.size() < 256)
                modHooks.particles.push_back(std::move(p));
            return Status::Ok;
        });
    }

    void Perlin::DropModHooks(const std::string& plugin) {
        bool hudChanged = false, packsChanged = false;
        {
            std::lock_guard lock(modHooks.mutex);
            hudChanged = modHooks.hud.erase(plugin) > 0;
            modHooks.debug.erase(plugin);
            modHooks.entities.erase(plugin);
            std::erase_if(modHooks.meshes, [&](const auto& m) { return m.first.starts_with(plugin + "/"); });
            modHooks.frame.erase(plugin);
            std::erase_if(modHooks.buttons, [&](const ModHooks::Button& b) { return b.plugin == plugin; });
            if (modHooks.customOwner == plugin)
                modHooks.customOpen = false;
            modHooks.loadingHidden.erase(plugin);
            for (auto it = modHooks.ambient.begin(); it != modHooks.ambient.end();)
                if (it->first.starts_with(plugin + "/")) {
                    sounds.Stop(it->second.sound);
                    it = modHooks.ambient.erase(it);
                } else {
                    ++it;
                }
            packsChanged =
                std::erase_if(modHooks.packs, [&](const auto& p) { return p.first == plugin; }) > 0;
        }
        if (hudChanged)
            ui::BumpHudFilter();
        if (packsChanged)
            ApplyTexturePacks();
    }

    void Perlin::ApplyModDebugLines(std::vector<std::string>& left, std::vector<std::string>& right) {
        std::lock_guard lock(modHooks.mutex);
        for (const auto& [plugin, d] : modHooks.debug) {
            left.insert(left.end(), d.left.begin(), d.left.end());
            right.insert(right.end(), d.right.begin(), d.right.end());
        }
    }

    void Perlin::ApplyModFog(FogState& fog) {
        std::lock_guard lock(modHooks.mutex);
        const auto& [until, f] = modHooks.fog;
        if (Clock::now() >= until)
            return;
        if (!f.enabled) {
            // "no fog": fog distances far past render distance
            fog.environmentalStart = fog.renderDistanceStart = 1e6f;
            fog.environmentalEnd = fog.renderDistanceEnd = 1e6f + 1.0f;
            return;
        }
        if (f.start >= 0)
            fog.environmentalStart = f.start;
        if (f.end >= 0)
            fog.environmentalEnd = std::max(f.end, fog.environmentalStart + 0.01f);
        if (f.color) {
            float c[4];
            ToRgba(f.color, c);
            fog.color = glm::vec4(c[0], c[1], c[2], fog.color.w);
        }
    }

    int Perlin::ModFrameLimit(int fps) const {
        auto& hooks = const_cast<ModHooks&>(modHooks);
        std::lock_guard lock(hooks.mutex);
        if (hooks.frame.empty())
            return fps;
        const bool away = !hooks.windowFocused || hooks.windowMinimized;
        int limit = 0;
        for (const auto& [plugin, f] : hooks.frame) {
            const int want = away && f.unfocusedFps > 0 ? f.unfocusedFps : f.fps;
            if (want > 0)
                limit = limit == 0 ? want : std::min(limit, want);
        }
        if (limit == 0)
            return fps;
        return fps > 0 ? std::min(fps, limit) : limit;
    }

    bool Perlin::DispatchModInteract(abi::InteractKind kind, int x, int y, int z, int face, uint64_t entity) {
        auto& mm = mods::ModManager::Instance();
        if (!mm.Wants(abi::Event::Interact))
            return false;
        abi::Writer w;
        w.write(abi::EventInteract{kind, x, y, z, static_cast<abi::Face>(std::clamp(face, 0, 5)), entity});
        return mm.Dispatch(abi::Event::Interact, w, 20.0);
    }

    bool Perlin::DispatchModSlotClick(int slot, bool right, bool shift) {
        auto& mm = mods::ModManager::Instance();
        if (!mm.Wants(abi::Event::SlotClick))
            return false;
        abi::Writer w;
        w.write(abi::EventSlotClick{slot, right, shift});
        return mm.Dispatch(abi::Event::SlotClick, w, 20.0);
    }

    bool Perlin::DispatchModToast(const std::string& title, const std::string& body) {
        auto& mm = mods::ModManager::Instance();
        if (!mm.Wants(abi::Event::Toast))
            return false;
        abi::Writer w;
        w.write(abi::EventToast{title, body});
        return mm.Dispatch(abi::Event::Toast, w, 20.0);
    }

    void Perlin::ApplyModSkins(std::vector<entity::Entity>& entities) {
        std::lock_guard lock(modHooks.mutex);
        if (modHooks.skins.empty())
            return;
        if (modSkinCacheSerial != modHooks.skinSerial) {
            modSkinCacheSerial = modHooks.skinSerial;
            modSkinCache.clear();
            for (const auto& [player, d] : modHooks.skins) {
                Texture t;
                if (!LoadTextureMemory(d.png.data(), d.png.size(), t))
                    continue;
                auto skin = std::make_shared<entity::SkinData>();
                skin->serial = 0x4D4F440000000000ull + modHooks.skinSerial;
                skin->skinId = "mod:" + player;
                skin->skinWidth = t.width;
                skin->skinHeight = t.height;
                skin->skinRgba = std::move(t.pixels);
                skin->slim = d.slim;
                modSkinCache[player] = std::move(skin);
            }
        }
        for (entity::Entity& e : entities) {
            if (!e.isPlayer)
                continue;
            const auto it = modSkinCache.find(e.isSelf ? std::string() : e.name);
            if (it == modSkinCache.end())
                continue;
            auto skin = std::make_shared<entity::SkinData>(*it->second);
            if (e.skin) {
                skin->capeWidth = e.skin->capeWidth;
                skin->capeHeight = e.skin->capeHeight;
                skin->capeRgba = e.skin->capeRgba;
            }
            e.skin = std::move(skin);
        }
    }

    void Perlin::PrepareModRender(entity::EntityRenderer::FrameIn& in) {
        const auto now = Clock::now();
        modFrameMeshes.clear();
        modFrameStyles.clear();
        modFrameTypeStyles.clear();
        std::lock_guard lock(modHooks.mutex);
        for (auto& [plugin, rules] : modHooks.entities) {
            std::erase_if(rules, [&](const auto& r) { return now >= r.second.until; });
            for (const auto& [key, rule] : rules) {
                const abi::EntityOverride& o = rule.rule;
                entity::EntityRenderer::FrameIn::ModStyle style;
                style.hidden = o.hidden;
                style.scale = glm::vec3(o.scaleX, o.scaleY, o.scaleZ);
                ToRgba(o.tint, style.tint);
                style.texture = o.texture;
                style.nameTag = o.nameTag;
                style.nameText = o.nameText;
                if (o.entity)
                    modFrameStyles[o.entity] = std::move(style);
                else
                    modFrameTypeStyles[o.type] = std::move(style);
            }
        }
        std::erase_if(modHooks.entities, [](const auto& p) { return p.second.empty(); });
        if (!modFrameStyles.empty() || !modFrameTypeStyles.empty())
            in.modStyle =
                [this](const entity::Entity& e) -> const entity::EntityRenderer::FrameIn::ModStyle* {
                if (const auto it = modFrameStyles.find(e.runtimeId); it != modFrameStyles.end())
                    return &it->second;
                if (const auto it = modFrameTypeStyles.find(e.type); it != modFrameTypeStyles.end())
                    return &it->second;
                return nullptr;
            };

        std::erase_if(modHooks.meshes, [&](const auto& m) { return now >= m.second.until; });
        for (const auto& [key, mesh] : modHooks.meshes) {
            const abi::WorldMeshData& m = mesh.data;
            if (m.triangles.empty())
                continue;
            entity::EntityRenderer::FrameIn::ModMesh out;
            out.origin = glm::dvec3(m.triangles.front().x, m.triangles.front().y, m.triangles.front().z);
            out.depthTest = m.depthTest && m.stage != abi::RenderStage::Overlay;
            if (m.texture) {
                const std::string plugin = key.substr(0, key.find('/'));
                const auto tex = modGfxTextures.find(plugin + "/" + std::to_string(m.texture));
                out.texture = tex != modGfxTextures.end() ? tex->second.gpu : 0;
            }
            out.vertices.reserve(m.triangles.size());
            for (const abi::MeshVertex& v : m.triangles)
                out.vertices.push_back(
                    {static_cast<float>(v.x - out.origin.x), static_cast<float>(v.y - out.origin.y),
                     static_cast<float>(v.z - out.origin.z), ArgbToAbgr(v.color), v.u, v.v});
            modFrameMeshes.push_back(std::move(out));
        }
        if (!modFrameMeshes.empty())
            in.modMeshes = &modFrameMeshes;
    }

    void Perlin::DrawModTint(int fbW, int fbH) {
        abi::ScreenTintData t;
        {
            std::lock_guard lock(modHooks.mutex);
            if (Clock::now() >= modHooks.tint.first)
                return;
            t = modHooks.tint.second;
        }
        if (!renderer || ((t.color >> 24) == 0 && t.vignette <= 0.0f))
            return;
        const float w = static_cast<float>(fbW), h = static_cast<float>(fbH);
        std::vector<UiVertex> v;
        std::vector<uint32_t> idx;
        const auto quad = [&](float x0, float y0, float x1, float y1, uint32_t c00, uint32_t c10,
                              uint32_t c11, uint32_t c01) {
            const uint32_t base = static_cast<uint32_t>(v.size());
            v.push_back({x0, y0, 0, c00, 0, 0});
            v.push_back({x1, y0, 0, c10, 0, 0});
            v.push_back({x1, y1, 0, c11, 0, 0});
            v.push_back({x0, y1, 0, c01, 0, 0});
            for (uint32_t i : {0u, 1u, 2u, 0u, 2u, 3u})
                idx.push_back(base + i);
        };
        if (t.color >> 24) {
            const uint32_t c = ArgbToAbgr(t.color);
            quad(0, 0, w, h, c, c, c, c);
        }
        if (t.vignette > 0.0f) {
            const uint32_t dark = static_cast<uint32_t>(std::lround(t.vignette * 220.0f)) << 24, clear = 0;
            const float ew = w * 0.25f, eh = h * 0.25f;
            quad(0, 0, ew, h, dark, clear, clear, dark);
            quad(w - ew, 0, w, h, clear, dark, dark, clear);
            quad(0, 0, w, eh, dark, dark, clear, clear);
            quad(0, h - eh, w, h, clear, clear, dark, dark);
        }
        const uint32_t mesh = renderer->UiCreateMesh(v.data(), static_cast<uint32_t>(v.size()), idx.data(),
                                                     static_cast<uint32_t>(idx.size()));
        if (!mesh)
            return;
        UiDraw draw;
        draw.mesh = mesh;
        draw.premultiplied = false;
        draw.blend = 1;
        renderer->UiSubmit(UiLayer::Gui, draw);
        renderer->UiDestroyMesh(mesh);
    }

    void Perlin::PumpModHooks(BedrockSession* live) {
        auto& mm = mods::ModManager::Instance();
        const auto now = Clock::now();

        static Clock::time_point lastSweep{};
        if (now - lastSweep > std::chrono::seconds(1)) {
            lastSweep = now;
            std::set<std::string> running, owners;
            for (const mods::ModInfo& info : mm.List())
                if (info.state == "running")
                    running.insert(info.name);
            {
                std::lock_guard lock(modHooks.mutex);
                for (const auto& p : modHooks.hud)
                    owners.insert(p.first);
                for (const auto& p : modHooks.debug)
                    owners.insert(p.first);
                for (const auto& p : modHooks.entities)
                    owners.insert(p.first);
                for (const auto& p : modHooks.frame)
                    owners.insert(p.first);
                for (const auto& b : modHooks.buttons)
                    owners.insert(b.plugin);
                for (const auto& p : modHooks.packs)
                    owners.insert(p.first);
                for (const auto& p : modHooks.loadingHidden)
                    owners.insert(p);
                for (const auto& m : modHooks.meshes)
                    owners.insert(m.first.substr(0, m.first.find('/')));
                for (const auto& a : modHooks.ambient)
                    owners.insert(a.first.substr(0, a.first.find('/')));
                if (modHooks.customOpen)
                    owners.insert(modHooks.customOwner);
            }
            for (const std::string& owner : owners)
                if (!running.contains(owner))
                    DropModHooks(owner);
        }

        if (platform) {
            const bool focused = platform->WindowFocused(), minimized = platform->WindowMinimized();
            bool changed = false;
            {
                std::lock_guard lock(modHooks.mutex);
                changed = focused != modHooks.windowFocused || minimized != modHooks.windowMinimized;
                modHooks.windowFocused = focused;
                modHooks.windowMinimized = minimized;
            }
            if (changed && mm.Wants(abi::Event::WindowFocus)) {
                abi::Writer w;
                w.write(abi::EventWindow{focused, minimized});
                mm.Dispatch(abi::Event::WindowFocus, w);
            }
        }

        // level load: on join and each dimension change; unload: on leave and before dimension change
        static bool wasIn = false;
        static int wasDim = -1;
        const bool in = live && live->GetState() == BedrockSession::State::InGame;
        const int dim = in ? live->DimensionId() : -1;
        if (wasIn && (!in || dim != wasDim))
            mm.Dispatch(abi::Event::LevelUnloaded);
        if (in && (!wasIn || dim != wasDim)) {
            abi::Writer w;
            w.write(abi::EventLevel{static_cast<abi::Dimension>(dim), options.server});
            mm.Dispatch(abi::Event::LevelLoaded, w);
        }
        wasIn = in;
        wasDim = dim;

        std::vector<std::pair<std::string, abi::EventServerPing>> pings;
        std::vector<abi::ParticleRequest> particleQueue;
        std::vector<abi::AmbientSoundData> ambient;
        {
            std::lock_guard lock(modHooks.mutex);
            pings.swap(modHooks.pings);
            particleQueue.swap(modHooks.particles);
            for (const auto& a : modHooks.ambient)
                ambient.push_back(a.second);
        }
        for (auto& [plugin, e] : pings) {
            abi::Writer w;
            w.write(e);
            mm.DispatchTo(plugin, abi::Event::ServerPing, w);
        }

        static uint32_t seed = 0x9E3779B9u;
        const auto jitter = [&](float spread) {
            seed = seed * 1664525u + 1013904223u;
            return (static_cast<float>(seed >> 8) / 16777216.0f * 2.0f - 1.0f) * spread;
        };
        for (const abi::ParticleRequest& p : particleQueue)
            for (int i = 0; i < p.count; i++)
                particles.SpawnNamed(p.name, glm::dvec3(p.x + jitter(p.spread), p.y + jitter(p.spread),
                                                        p.z + jitter(p.spread)));
        for (const abi::AmbientSoundData& a : ambient)
            if (!audioEngine.Playing(a.sound))
                sounds.PlayNamed(a.sound, glm::dvec3(camera.x, camera.y, camera.z), a.volume, a.pitch, false);

        bool customOpen = false;
        std::string owner;
        {
            std::lock_guard lock(modHooks.mutex);
            customOpen = modHooks.customOpen;
            owner = modHooks.customOwner;
        }
        if (customOpen) {
            input.SetTextMode(true);
            if (input.IsKeyDown(Key::Escape)) {
                std::lock_guard lock(modHooks.mutex);
                modHooks.customOpen = false;
                customOpen = false;
            }
            if (!customOpen && !chat.IsOpen() && !pause.shown && !inventory.shown)
                input.SetTextMode(false);
        }

        const abi::Screen screen = CurrentScreen();
        if (screen == abi::Screen::None)
            return;
        const float scale = static_cast<float>(modHud ? std::max(1, modHud->Scale()) : 1);
        const float mx = input.MouseX() / scale, my = input.MouseY() / scale;
        if (mm.Wants(abi::Event::ScreenRender)) {
            abi::Writer w;
            w.write(abi::EventScreenRender{screen, modGuiW, modGuiH, mx, my});
            if (screen == abi::Screen::Custom)
                mm.DispatchTo(owner, abi::Event::ScreenRender, w);
            else
                mm.Dispatch(abi::Event::ScreenRender, w);
        }
        std::vector<ModHooks::Button> buttons;
        {
            std::lock_guard lock(modHooks.mutex);
            for (const ModHooks::Button& b : modHooks.buttons)
                if (b.def.screen == screen)
                    buttons.push_back(b);
        }
        static bool wasDown = false;
        const bool down = input.IsMouseDown(0);
        const bool clicked = down && !wasDown;
        wasDown = down;
        modScreenButtons.clear();
        for (const ModHooks::Button& b : buttons) {
            const bool hover =
                mx >= b.def.x && mx < b.def.x + b.def.width && my >= b.def.y && my < b.def.y + b.def.height;
            modScreenButtons.push_back({b.def, hover});
            if (hover && clicked) {
                abi::Writer w;
                w.write(abi::EventScreenButton{b.def.id});
                mm.DispatchTo(b.plugin, abi::Event::ScreenButton, w);
            }
        }
    }
}
