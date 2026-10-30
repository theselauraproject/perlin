#include "client/hand/HandRenderer.hpp"
#include "util/Env.hpp"

#include "client/hand/ItemDisplay.hpp"
#include "client/resources/TextureLoader.hpp"

#include "client/ui/Glint.hpp"
#include "client/ui/UiAssets.hpp"
#include <chrono>
#include <iostream>

#include <glm/gtc/matrix_transform.hpp>

#include "util/Resources.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace perlin {
    namespace {
        glm::mat4 AttachAttach() {
            if (const char* e = PERLIN_ENV("PERLIN_ATTACH_K")) {
                float v[7] = {0, 0, 0, 0, 0, 0, 1};
                std::sscanf(e, "%f,%f,%f,%f,%f,%f,%f", v, v + 1, v + 2, v + 3, v + 4, v + 5, v + 6);
                return glm::translate(glm::mat4(1.0f), glm::vec3(v[0], v[1], v[2])) *
                       glm::rotate(glm::mat4(1.0f), glm::radians(v[3]), glm::vec3(0, 1, 0)) *
                       glm::rotate(glm::mat4(1.0f), glm::radians(v[4]), glm::vec3(1, 0, 0)) *
                       glm::rotate(glm::mat4(1.0f), glm::radians(v[5]), glm::vec3(0, 0, 1)) *
                       glm::scale(glm::mat4(1.0f), glm::vec3(v[6]));
            }
            return glm::translate(glm::mat4(1.0f), glm::vec3(0.2f, 0.5f, -0.1f)) *
                   glm::rotate(glm::mat4(1.0f), glm::radians(90.0f), glm::vec3(0, 0, 1)) *
                   glm::scale(glm::mat4(1.0f), glm::vec3(0.65f));
        }
    }
    static const glm::mat4 kAttachableAttach = AttachAttach();
    static glm::vec4 EntityPlace() {
        glm::vec4 v(0.0f, 0.1f, -0.5f, 0.0f);
        if (const char* e = PERLIN_ENV("PERLIN_ENTITY_K"))
            std::sscanf(e, "%f,%f,%f,%f", &v.x, &v.y, &v.z, &v.w);
        return v;
    }
    static const glm::vec4 kEntityPlace = EntityPlace();

    namespace {
        constexpr float kPi = 3.14159265358979323846f;
        constexpr float kDeg = kPi / 180.0f;

        glm::mat4 T(float x, float y, float z) { return glm::translate(glm::mat4(1.0f), {x, y, z}); }
        glm::mat4 Rx(float deg) { return glm::rotate(glm::mat4(1.0f), deg * kDeg, {1, 0, 0}); }
        glm::mat4 Ry(float deg) { return glm::rotate(glm::mat4(1.0f), deg * kDeg, {0, 1, 0}); }
        glm::mat4 Rz(float deg) { return glm::rotate(glm::mat4(1.0f), deg * kDeg, {0, 0, 1}); }
        glm::mat4 S(float s) { return glm::scale(glm::mat4(1.0f), {s, s, s}); }
        glm::mat4 Custom(float x, float y, float z, float rx, float ry, float rz, float scale, float side) {
            return T(side * x, y, z) * Rx(rx) * Ry(side * ry) * Rz(side * rz) * S(scale);
        }

        float Lerp(float t, float a, float b) { return a + (b - a) * t; }

        float AttackSpeed(const std::string& fullName) {
            std::string n = fullName.rfind("minecraft:", 0) == 0 ? fullName.substr(10) : fullName;
            auto ends = [&](const char* suffix) {
                const size_t l = std::char_traits<char>::length(suffix);
                return n.size() >= l && n.compare(n.size() - l, l, suffix) == 0;
            };
            auto starts = [&](const char* prefix) { return n.rfind(prefix, 0) == 0; };
            if (ends("_sword")) return 1.6f;
            if (ends("_pickaxe")) return 1.2f;
            if (ends("_shovel")) return 1.0f;
            if (ends("_axe")) return starts("wooden") || starts("stone") ? 0.8f : starts("iron") ? 0.9f : 1.0f;
            if (ends("_hoe")) return starts("wooden") || starts("golden") ? 1.0f : starts("stone") ? 2.0f
                                       : starts("iron") ? 3.0f : 4.0f;
            if (n == "trident") return 1.1f;
            if (n == "mace") return 0.6f;
            return 4.0f;
        }

        bool SameItem(const ui::HudItem& a, const ui::HudItem& b) {
            if (a.Empty() || b.Empty())
                return a.Empty() && b.Empty();
            return a.name == b.name && (a.maxDamage > 0 || a.aux == b.aux);
        }

        uint32_t PackColor(const glm::vec3& c) {
            auto q = [](float x) { return static_cast<uint32_t>(std::clamp(x, 0.0f, 1.0f) * 255.0f + 0.5f); };
            return 0xff000000u | (q(c.b) << 16) | (q(c.g) << 8) | q(c.r);
        }
    }

    void WalkBob::Tick(double moved, bool onGround, bool swimming) {
        walkDistO = walkDist;
        oBob = bob;
        walkDist += static_cast<float>(moved) * 0.6f;
        const float speed = onGround && !swimming ? std::min(0.1f, static_cast<float>(moved) * 0.546f) : 0.0f;
        bob += (speed - bob) * 0.4f;
    }

    glm::mat4 BobViewMatrix(const WalkBob& b, float partial, float strength) {
        const float delta = b.walkDist - b.walkDistO;
        const float f1 = -(b.walkDist + delta * partial);
        const float f2 = Lerp(partial, b.oBob, b.bob) * strength;
        const float s = std::sin(f1 * kPi), c = std::cos(f1 * kPi);
        glm::mat4 m = T(s * f2 * 0.5f, -std::fabs(c * f2), 0.0f);
        m = m * Rz(s * f2 * 3.0f);
        m = m * Rx(std::fabs(std::cos(f1 * kPi - 0.2f) * f2) * 5.0f);
        return m;
    }

    glm::mat4 HandRenderer::Projection(float fovDegrees, float aspect, float farPlane) {
        return glm::perspective(glm::radians(fovDegrees), aspect, 0.05f, farPlane);
    }

    void HandRenderer::Initialize(RenderContext& r) {
        renderer = &r;
    }

    void HandRenderer::ClearItems() {
        if (renderer)
            for (auto& [key, model] : items)
                if (model.texture)
                    renderer->UiDestroyTexture(model.texture);
        items.clear();
        if (renderer)
            for (const auto& [path, id] : attachableTextures)
                if (id)
                    renderer->UiDestroyTexture(id);
        attachableTextures.clear();
        shieldTexture = 0;
        attachables.Clear();
    }

    void HandRenderer::Shutdown() {
        if (renderer) {
            for (uint32_t mesh : pendingMeshes)
                renderer->UiDestroyMesh(mesh);
            pendingMeshes.clear();
            ClearItems();
            if (skinTexture)
                renderer->UiDestroyTexture(skinTexture);
            if (glintTexture)
                renderer->UiDestroyTexture(glintTexture);
            if (scopeTexture)
                renderer->UiDestroyTexture(scopeTexture);
            if (spyglassTexture)
                renderer->UiDestroyTexture(spyglassTexture);
        }
        scopeTexture = 0;
        spyglassTexture = 0;
        spyglassModel = Model();
        skinTexture = 0;
        glintTexture = 0;
        renderer = nullptr;
    }

    float HandRenderer::Unwrap(float yaw) {
        if (!haveYaw) {
            haveYaw = true;
            yawUnwrapped = yaw;
        } else {
            float d = std::fmod(yaw - lastYawRaw, 360.0f);
            if (d > 180.0f) d -= 360.0f;
            if (d < -180.0f) d += 360.0f;
            yawUnwrapped += d;
        }
        lastYawRaw = yaw;
        return yawUnwrapped;
    }

    void HandRenderer::Swing(const HandSettings& s) {
        const int duration = std::max(1, static_cast<int>(std::lround(6.0f / std::max(0.1f, s.swingSpeed))));
        if (!s.swing)
            return;
        if (!swinging || swingTime >= duration / 2 || swingTime < 0) {
            swingTime = -1;
            swinging = true;
        }
    }

    void HandRenderer::Attack(const HandSettings& s, bool resetsCooldown) {
        Swing(s);
        if (resetsCooldown)
            attackStrengthTicker = 0;
    }

    float HandRenderer::SwingProgress(float partial) const {
        if (forcedSwing >= 0.0f)
            return forcedSwing;
        float d = attackAnim - oAttackAnim;
        if (d < 0.0f)
            d += 1.0f;
        return oAttackAnim + d * partial;
    }

    bool HandRenderer::IsFood(const std::string& full) {
        static const char* kFood[] = {
            "apple", "golden_apple", "enchanted_golden_apple", "bread", "cooked_beef", "beef", "cooked_porkchop", "porkchop",
            "cooked_chicken", "chicken", "cooked_mutton", "mutton", "cooked_rabbit", "rabbit", "cooked_cod", "cod",
            "cooked_salmon", "salmon", "tropical_fish", "pufferfish", "carrot", "golden_carrot", "potato", "baked_potato",
            "poisonous_potato", "beetroot", "beetroot_soup", "mushroom_stew", "rabbit_stew", "suspicious_stew", "melon_slice",
            "sweet_berries", "glow_berries", "cookie", "pumpkin_pie", "cake_slice", "dried_kelp", "rotten_flesh", "spider_eye",
            "chorus_fruit", "honey_bottle", "milk_bucket", "potion", "ominous_bottle"};
        const std::string n = full.rfind("minecraft:", 0) == 0 ? full.substr(10) : full;
        for (const char* f : kFood)
            if (n == f)
                return true;
        return false;
    }

    bool HandRenderer::IsThrowable(const std::string& full) {
        const std::string n = full.rfind("minecraft:", 0) == 0 ? full.substr(10) : full;
        return n == "experience_bottle" || n == "splash_potion" || n == "lingering_potion" || n == "snowball" ||
               n == "egg" || n == "ender_pearl";
    }

    void HandRenderer::Tick(const TickIn& in, const HandSettings& s) {
        const int duration = std::max(1, static_cast<int>(std::lround(6.0f / std::max(0.1f, s.swingSpeed))));
        oAttackAnim = attackAnim;
        if (swinging) {
            ++swingTime;
            if (swingTime >= duration) {
                swingTime = 0;
                swinging = false;
            }
        } else {
            swingTime = 0;
        }
        attackAnim = static_cast<float>(swingTime) / static_cast<float>(duration);
        attackStrengthTicker++;

        if (!in.eating)
            eatLatched = false;
        if (in.eating && !eatLatched) {
            if (++eatTicks > 32) {
                eatTicks = 0;
                eatLatched = true;
            }
        } else {
            eatTicks = 0;
        }
        {
            const auto is = [](const ui::HudItem& i, const char* n) { return i.name == n; };
            const bool drawable = is(in.main, "minecraft:bow") || is(in.main, "minecraft:crossbow") || is(in.main, "minecraft:trident");
            const bool shield = is(in.main, "minecraft:shield") || is(in.off, "minecraft:shield");
            useHeld = in.useHeld && drawable;
            usedTicks = useHeld ? usedTicks + 1 : 0;
            blocking = shield && (in.sneaking || (in.useHeld && (is(in.main, "minecraft:shield") || !drawable)));
            scoping = in.useHeld && is(in.main, "minecraft:spyglass");
            oScopeScale = scopeScale;
            scopeScale = scoping ? scopeScale + (1.125f - scopeScale) * 0.5f : 0.5f;
            if (!scoping)
                oScopeScale = 0.5f;
        }

        const float yaw = Unwrap(in.yaw);
        if (firstTick) {
            xBob = xBobO = in.pitch;
            yBob = yBobO = yaw;
        }
        xBobO = xBob;
        yBobO = yBob;
        xBob += (in.pitch - xBob) * 0.5f;
        yBob += (yaw - yBob) * 0.5f;

        oMainHandHeight = mainHandHeight;
        oOffHandHeight = offHandHeight;
        if (firstTick) {
            mainShown = in.main;
            offShown = in.off;
            mainHandHeight = oMainHandHeight = 1.0f;
            offHandHeight = oOffHandHeight = 1.0f;
            firstTick = false;
        }
        if (SameItem(mainShown, in.main))
            mainShown = in.main;
        if (SameItem(offShown, in.off))
            offShown = in.off;

        // no attack cooldown in bedrock: no dip after swing (java 1.9+ only); lowers only on item switch
        const float mainTarget = SameItem(mainShown, in.main) ? 1.0f : 0.0f;
        const float offTarget = SameItem(offShown, in.off) ? 1.0f : 0.0f;
        if (!s.equipAnimation) {
            mainShown = in.main;
            offShown = in.off;
            mainHandHeight = 1.0f;
            offHandHeight = 1.0f;
        } else {
            mainHandHeight += std::clamp(mainTarget - mainHandHeight, -0.4f, 0.4f);
            offHandHeight += std::clamp(offTarget - offHandHeight, -0.4f, 0.4f);
        }
        if (mainHandHeight < 0.1f)
            mainShown = in.main;
        if (offHandHeight < 0.1f)
            offShown = in.off;
    }

    namespace {
        template <typename Q>
        void AddBox(std::vector<Q>& out, glm::vec3 mn, glm::vec3 mx, float u0, float v0, float w, float h, float d,
                    float texW, float texH) {
            auto uv = [&](float u, float v) { return glm::vec2(u / texW, v / texH); };
            auto push = [&](glm::vec3 a, glm::vec2 ta, glm::vec3 b, glm::vec2 tb, glm::vec3 c, glm::vec2 tc,
                            glm::vec3 e, glm::vec2 te, glm::vec3 n) {
                Q q;
                q.p[0] = a; q.p[1] = b; q.p[2] = c; q.p[3] = e;
                q.uv[0] = ta; q.uv[1] = tb; q.uv[2] = tc; q.uv[3] = te;
                q.n = n;
                out.push_back(q);
            };
            const float x0 = mn.x, x1 = mx.x, y0 = mn.y, y1 = mx.y, z0 = mn.z, z1 = mx.z;
            {
                const float U0 = u0 + d, U1 = u0 + d + w, V0 = v0 + d, V1 = v0 + d + h;
                push({x0, y0, z0}, uv(U0, V0), {x1, y0, z0}, uv(U1, V0), {x1, y1, z0}, uv(U1, V1), {x0, y1, z0}, uv(U0, V1),
                     {0, 0, -1});
            }
            {
                const float U0 = u0 + d + w + d, U1 = u0 + d + w + d + w, V0 = v0 + d, V1 = v0 + d + h;
                push({x1, y0, z1}, uv(U0, V0), {x0, y0, z1}, uv(U1, V0), {x0, y1, z1}, uv(U1, V1), {x1, y1, z1}, uv(U0, V1),
                     {0, 0, 1});
            }
            {
                const float U0 = u0, U1 = u0 + d, V0 = v0 + d, V1 = v0 + d + h;
                push({x0, y0, z1}, uv(U0, V0), {x0, y0, z0}, uv(U1, V0), {x0, y1, z0}, uv(U1, V1), {x0, y1, z1}, uv(U0, V1),
                     {-1, 0, 0});
            }
            {
                const float U0 = u0 + d + w, U1 = u0 + d + w + d, V0 = v0 + d, V1 = v0 + d + h;
                push({x1, y0, z0}, uv(U0, V0), {x1, y0, z1}, uv(U1, V0), {x1, y1, z1}, uv(U1, V1), {x1, y1, z0}, uv(U0, V1),
                     {1, 0, 0});
            }
            {
                const float U0 = u0 + d, U1 = u0 + d + w, V0 = v0, V1 = v0 + d;
                push({x0, y0, z1}, uv(U0, V0), {x1, y0, z1}, uv(U1, V0), {x1, y0, z0}, uv(U1, V1), {x0, y0, z0}, uv(U0, V1),
                     {0, -1, 0});
            }
            {
                const float U0 = u0 + d + w, U1 = u0 + d + w + w, V0 = v0, V1 = v0 + d;
                push({x0, y1, z0}, uv(U0, V0), {x1, y1, z0}, uv(U1, V0), {x1, y1, z1}, uv(U1, V1), {x0, y1, z1}, uv(U0, V1),
                     {0, 1, 0});
            }
        }
    }

    void HandRenderer::BuildArms(bool slim) {
        if (armsBuilt && (slim ? !armSlim[0].base.empty() : !armClassic[0].base.empty()))
            return;
        armsBuilt = true;
        for (int side = 0; side < 2; side++) {
            const bool right = side == 0;
            ArmModel& m = slim ? armSlim[side] : armClassic[side];
            m.base.clear();
            m.sleeve.clear();
            const float w = slim ? 3.0f : 4.0f;
            const float minX = right ? (slim ? -2.0f : -3.0f) : -1.0f;
            const glm::vec3 mn(minX, -2.0f, -2.0f), mx(minX + w, 10.0f, 2.0f);
            const float u0 = right ? 40.0f : 32.0f, v0 = right ? 16.0f : 48.0f;
            const float su = right ? 40.0f : 48.0f, sv = right ? 32.0f : 48.0f;
            AddBox(m.base, mn, mx, u0, v0, w, 12.0f, 4.0f, 64.0f, 64.0f);
            const float g = 0.25f;
            AddBox(m.sleeve, mn - glm::vec3(g), mx + glm::vec3(g), su, sv, w, 12.0f, 4.0f, 64.0f, 64.0f);
        }
    }

    void HandRenderer::EnsureSkin(const FrameIn& in) {
        if (!renderer || (skinTexture && skinSerialUploaded == in.skinSerial))
            return;
        if (skinTexture)
            renderer->UiDestroyTexture(skinTexture);
        skinTexture = 0;
        skinSerialUploaded = in.skinSerial;

        const LoadedSkin* skin = in.skin;
        bool usable = skin && skin->hasSkin() && skin->skinWidth >= 64 && skin->skinHeight >= 64;
        if (usable) {
            size_t opaque = 0;
            for (size_t i = 3; i < skin->skinRgba.size(); i += 4)
                opaque += skin->skinRgba[i] > 16 ? 1 : 0;
            usable = opaque > 64;
        }
        if (usable) {
            skinTexture = renderer->UiCreateTexture(skin->skinWidth, skin->skinHeight, skin->skinRgba.data(), false);
            skinFallback = false;
            return;
        }
        {
            Texture tex;
            if (ui::UiAssets::Instance().Image(in.slimArm ? "textures/entity/alex.png" : "textures/entity/steve.png", tex) &&
                tex.width >= 64 && tex.height >= 64) {
                skinTexture = renderer->UiCreateTexture(tex.width, tex.height, tex.pixels.data(), false);
                skinFallback = true;
                return;
            }
        }
        std::vector<uint8_t> px(64 * 64 * 4);
        auto fill = [&](int x0, int y0, int x1, int y1, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
            for (int y = y0; y < y1; y++)
                for (int x = x0; x < x1; x++) {
                    uint8_t* p = &px[(static_cast<size_t>(y) * 64 + x) * 4];
                    p[0] = r; p[1] = g; p[2] = b; p[3] = a;
                }
        };
        fill(0, 0, 64, 64, 0xb2, 0x84, 0x65, 255);
        fill(40, 16, 56, 24, 0x00, 0xa8, 0xa8, 255);
        fill(32, 48, 48, 56, 0x00, 0xa8, 0xa8, 255);
        fill(40, 32, 56, 48, 0, 0, 0, 0);
        fill(48, 48, 64, 64, 0, 0, 0, 0);
        skinTexture = renderer->UiCreateTexture(64, 64, px.data(), false);
        skinFallback = true;
    }

    const HandRenderer::Model& HandRenderer::ItemModel(const ui::HudItem& item) {
        auto& assets = ui::UiAssets::Instance();
        if (assets.Generation() != itemGeneration) {
            itemGeneration = assets.Generation();
            ClearItems();
        }
        const std::string key = item.Key();
        auto it = items.find(key);
        if (it != items.end())
            return it->second;
        Model model;
        const ui::ItemSprite* sprite = assets.Item(key);
        ui::ItemSprite baked;
        if (!sprite || sprite->generic || sprite->fromBlock) {
            Texture icon;
            if (assets.ItemIcon(key, icon) && icon.width > 0 && !icon.pixels.empty() && (!sprite || sprite->generic || !sprite->block)) {
                baked.ok = true;
                baked.w = icon.width;
                baked.h = icon.height;
                baked.rgba = std::move(icon.pixels);
                sprite = &baked;
            }
        }
        if (sprite && sprite->ok) {
            if (sprite->block && sprite->tile > 0) {
                const int t = sprite->tile;
                std::vector<uint8_t> atlas(static_cast<size_t>(t) * 3 * t * 4, 0);
                auto blit = [&](const std::vector<uint8_t>& face, int slot) {
                    for (int y = 0; y < t; y++)
                        std::copy_n(face.data() + static_cast<size_t>(y) * t * 4, static_cast<size_t>(t) * 4,
                                    atlas.data() + (static_cast<size_t>(y) * t * 3 + static_cast<size_t>(slot) * t) * 4);
                };
                blit(sprite->top, 0);
                blit(sprite->left, 1);
                blit(sprite->right, 2);
                model.texture = renderer->UiCreateTexture(t * 3, t, atlas.data(), false);
                model.block = true;
                const float e = 0.02f / static_cast<float>(t);
                auto rect = [&](int slot, float& a, float& b, float& c, float& d) {
                    a = (static_cast<float>(slot) + e) / 3.0f;
                    c = (static_cast<float>(slot) + 1.0f - e) / 3.0f;
                    b = e;
                    d = 1.0f - e;
                };
                int clearRows = 0;
                while (clearRows < t / 2) {
                    bool empty = true;
                    for (int x = 0; x < t && empty; x++)
                        empty = sprite->left[(static_cast<size_t>(clearRows) * t + x) * 4 + 3] == 0;
                    if (!empty)
                        break;
                    clearRows++;
                }
                const float top = 1.0f - static_cast<float>(clearRows) / static_cast<float>(t);
                struct Box { glm::vec3 lo, hi; };
                std::vector<Box> boxes;
                const std::string& nm = item.name;
                auto has = [&](const char* k) { return nm.find(k) != std::string::npos; };
                if (has("_stairs"))
                    boxes = {{{0, 0, 0}, {1, 0.5f, 1}}, {{0.5f, 0.5f, 0}, {1, 1, 1}}};
                else if (has("_slab") && !has("double"))
                    boxes = {{{0, 0, 0}, {1, 0.5f, 1}}};
                else if (has("_fence_gate"))
                    boxes = {{{0, 5.0f / 16, 7.0f / 16}, {2.0f / 16, 1, 9.0f / 16}}, {{14.0f / 16, 5.0f / 16, 7.0f / 16}, {1, 1, 9.0f / 16}},
                             {{6.0f / 16, 6.0f / 16, 7.0f / 16}, {10.0f / 16, 15.0f / 16, 9.0f / 16}}};
                else if (has("_fence") || has("_wall"))
                    boxes = {{{6.0f / 16, 0, 6.0f / 16}, {10.0f / 16, has("_wall") ? 14.0f / 16 : 1.0f, 10.0f / 16}}};
                else if (has("_trapdoor"))
                    boxes = {{{0, 0, 0}, {1, 3.0f / 16, 1}}};
                else if (has("carpet") || has("pressure_plate"))
                    boxes = {{{0, 0, 0}, {1, has("carpet") ? 1.0f / 16 : 1.0f / 16, 1}}};
                else if (has("daylight_detector"))
                    boxes = {{{0, 0, 0}, {1, 6.0f / 16, 1}}};
                else if (has("_bed") && false)
                    boxes = {};
                else
                    boxes = {{{0, 0, 0}, {1, top, 1}}};
                auto face = [&](glm::vec3 a, glm::vec3 b, glm::vec3 c, glm::vec3 d, glm::vec3 n, int slot, const Box& bx) {
                    auto uvOf = [&](glm::vec3 p) -> glm::vec2 {
                        float fu, fv;
                        if (slot == 0) { fu = p.x; fv = p.z; }
                        else if (slot == 1) { fu = (n.z > 0) ? p.x : (n.z < 0 ? 1.0f - p.x : p.x); fv = 1.0f - p.y; }
                        else { fu = (n.x > 0) ? 1.0f - p.z : p.z; fv = 1.0f - p.y; }
                        float u0, v0, u1, v1;
                        rect(slot, u0, v0, u1, v1);
                        return {u0 + (u1 - u0) * fu, v0 + (v1 - v0) * fv};
                    };
                    (void)bx;
                    Quad q;
                    q.p[0] = a; q.p[1] = b; q.p[2] = c; q.p[3] = d;
                    q.uv[0] = uvOf(a); q.uv[1] = uvOf(b); q.uv[2] = uvOf(c); q.uv[3] = uvOf(d);
                    q.n = n;
                    model.quads.push_back(q);
                };
                for (Box bx : boxes) {
                    const glm::vec3 l = bx.lo, h = bx.hi;
                    face({l.x, h.y, l.z}, {h.x, h.y, l.z}, {h.x, h.y, h.z}, {l.x, h.y, h.z}, {0, 1, 0}, 0, bx);
                    face({l.x, l.y, h.z}, {h.x, l.y, h.z}, {h.x, l.y, l.z}, {l.x, l.y, l.z}, {0, -1, 0}, 1, bx);
                    face({h.x, h.y, l.z}, {l.x, h.y, l.z}, {l.x, l.y, l.z}, {h.x, l.y, l.z}, {0, 0, -1}, 1, bx);
                    face({l.x, h.y, h.z}, {h.x, h.y, h.z}, {h.x, l.y, h.z}, {l.x, l.y, h.z}, {0, 0, 1}, 1, bx);
                    face({l.x, h.y, l.z}, {l.x, h.y, h.z}, {l.x, l.y, h.z}, {l.x, l.y, l.z}, {-1, 0, 0}, 2, bx);
                    face({h.x, h.y, h.z}, {h.x, h.y, l.z}, {h.x, l.y, l.z}, {h.x, l.y, h.z}, {1, 0, 0}, 2, bx);
                }
                model.valid = true;
            } else if (sprite->w > 0 && sprite->h > 0 && !sprite->rgba.empty()) {
                const int w = sprite->w, h = sprite->h;
                model.texture = renderer->UiCreateTexture(w, h, sprite->rgba.data(), false);
                const float sx = 16.0f / static_cast<float>(w), sy = 16.0f / static_cast<float>(h);
                // pixels under shader alpha cutoff (0.1) count as empty: sides follow what's drawn;
                // else soft-glow packs get walls on the invisible glow edge, blade looks flat
                auto alpha = [&](int x, int y) -> int {
                    if (x < 0 || y < 0 || x >= w || y >= h) return 0;
                    const int a = sprite->rgba[(static_cast<size_t>(y) * w + x) * 4 + 3];
                    return a < 26 ? 0 : a;
                };
                const float zf = 7.5f / 16.0f, zb = 8.5f / 16.0f;
                auto P = [&](float px, float py, float z) { return glm::vec3(px * sx / 16.0f, (16.0f - py * sy) / 16.0f, z); };
                Quad front;
                front.p[0] = P(0, 0, zb); front.p[1] = P(static_cast<float>(w), 0, zb);
                front.p[2] = P(static_cast<float>(w), static_cast<float>(h), zb); front.p[3] = P(0, static_cast<float>(h), zb);
                front.uv[0] = {0, 0}; front.uv[1] = {1, 0}; front.uv[2] = {1, 1}; front.uv[3] = {0, 1};
                front.n = {0, 0, 1};
                model.quads.push_back(front);
                Quad back;
                back.p[0] = P(static_cast<float>(w), 0, zf); back.p[1] = P(0, 0, zf);
                back.p[2] = P(0, static_cast<float>(h), zf); back.p[3] = P(static_cast<float>(w), static_cast<float>(h), zf);
                back.uv[0] = {1, 0}; back.uv[1] = {0, 0}; back.uv[2] = {0, 1}; back.uv[3] = {1, 1};
                back.n = {0, 0, -1};
                model.quads.push_back(back);
                for (int y = 0; y < h; y++)
                    for (int x = 0; x < w; x++) {
                        if (alpha(x, y) == 0)
                            continue;
                        const glm::vec2 c((static_cast<float>(x) + 0.5f) / static_cast<float>(w),
                                          (static_cast<float>(y) + 0.5f) / static_cast<float>(h));
                        const float fx = static_cast<float>(x), fy = static_cast<float>(y);
                        auto side = [&](glm::vec3 a, glm::vec3 b, glm::vec3 cc, glm::vec3 d, glm::vec3 n) {
                            Quad q;
                            q.p[0] = a; q.p[1] = b; q.p[2] = cc; q.p[3] = d;
                            for (auto& t : q.uv) t = c;
                            q.n = n;
                            model.quads.push_back(q);
                        };
                        if (alpha(x - 1, y) == 0)
                            side(P(fx, fy, zf), P(fx, fy, zb), P(fx, fy + 1, zb), P(fx, fy + 1, zf), {-1, 0, 0});
                        if (alpha(x + 1, y) == 0)
                            side(P(fx + 1, fy, zb), P(fx + 1, fy, zf), P(fx + 1, fy + 1, zf), P(fx + 1, fy + 1, zb), {1, 0, 0});
                        if (alpha(x, y - 1) == 0)
                            side(P(fx, fy, zb), P(fx + 1, fy, zb), P(fx + 1, fy, zf), P(fx, fy, zf), {0, 1, 0});
                        if (alpha(x, y + 1) == 0)
                            side(P(fx, fy + 1, zf), P(fx + 1, fy + 1, zf), P(fx + 1, fy + 1, zb), P(fx, fy + 1, zb), {0, -1, 0});
                    }
                model.valid = true;
            }
        }
        return items.emplace(key, std::move(model)).first->second;
    }

    const HandRenderer::Model& HandRenderer::SpyglassModel() {
        if (spyglassModel.valid)
            return spyglassModel;
        std::string bytes;
        Texture tex;
        if (!resources::Read("spyglass_model.png", bytes) ||
            !LoadTextureMemory(reinterpret_cast<const unsigned char*>(bytes.data()), bytes.size(), tex))
            return spyglassModel;
        if (spyglassTexture)
            renderer->UiDestroyTexture(spyglassTexture);
        spyglassTexture = renderer->UiCreateTexture(tex.width, tex.height, tex.pixels.data(), false);
        spyglassModel.texture = spyglassTexture;
        auto box = [&](glm::vec3 from, glm::vec3 to, bool down, glm::vec2 s0, float v0, float v1, glm::vec2 up, glm::vec2 dn) {
            const glm::vec3 l = from / 16.0f, h = to / 16.0f;
            auto uv = [](float u, float v) { return glm::vec2(u / 16.0f, v / 16.0f); };
            auto face = [&](glm::vec3 a, glm::vec3 b, glm::vec3 c, glm::vec3 d, glm::vec3 n, glm::vec2 lo, glm::vec2 hi) {
                Quad q;
                q.p[0] = a; q.p[1] = b; q.p[2] = c; q.p[3] = d;
                q.uv[0] = uv(lo.x, lo.y); q.uv[1] = uv(hi.x, lo.y); q.uv[2] = uv(hi.x, hi.y); q.uv[3] = uv(lo.x, hi.y);
                q.n = n;
                spyglassModel.quads.push_back(q);
            };
            const glm::vec2 sideLo(s0.x, v0), sideHi(s0.y, v1);
            face({l.x, h.y, l.z}, {h.x, h.y, l.z}, {h.x, h.y, h.z}, {l.x, h.y, h.z}, {0, 1, 0}, up, up + glm::vec2(2, 2));
            if (down)
                face({l.x, l.y, h.z}, {h.x, l.y, h.z}, {h.x, l.y, l.z}, {l.x, l.y, l.z}, {0, -1, 0}, dn, dn + glm::vec2(2, 2));
            face({h.x, h.y, l.z}, {l.x, h.y, l.z}, {l.x, l.y, l.z}, {h.x, l.y, l.z}, {0, 0, -1}, sideLo, sideHi);
            face({l.x, h.y, h.z}, {h.x, h.y, h.z}, {h.x, l.y, h.z}, {l.x, l.y, h.z}, {0, 0, 1}, sideLo, sideHi);
            face({l.x, h.y, l.z}, {l.x, h.y, h.z}, {l.x, l.y, h.z}, {l.x, l.y, l.z}, {-1, 0, 0}, sideLo, sideHi);
            face({h.x, h.y, h.z}, {h.x, h.y, l.z}, {h.x, l.y, l.z}, {h.x, l.y, h.z}, {1, 0, 0}, sideLo, sideHi);
        };
        box({7, 8.5f, 7}, {9, 13.5f, 9}, false, {0, 2}, 2, 7, {0, 0}, {0, 0});
        box({6.9f, 2.4f, 6.9f}, {9.1f, 8.6f, 9.1f}, true, {0, 2}, 7, 13, {0, 5}, {0, 13});
        spyglassModel.valid = true;
        return spyglassModel;
    }

    void HandRenderer::RenderScope(int width, int height, float partial) {
        if (!renderer || !scoping || width <= 0 || height <= 0)
            return;
        for (uint32_t mesh : scopeMeshes)
            renderer->UiDestroyMesh(mesh);
        scopeMeshes.clear();
        if (!scopeTexture) {
            std::string bytes;
            Texture tex;
            if (resources::Read("spyglass_scope.png", bytes) &&
                LoadTextureMemory(reinterpret_cast<const unsigned char*>(bytes.data()), bytes.size(), tex))
                scopeTexture = renderer->UiCreateTexture(tex.width, tex.height, tex.pixels.data(), false);
        }
        const float scale = oScopeScale + (scopeScale - oScopeScale) * partial;
        const float side = std::floor(static_cast<float>(std::min(width, height)) * scale);
        const float x0 = std::floor((static_cast<float>(width) - side) * 0.5f), y0 = std::floor((static_cast<float>(height) - side) * 0.5f);
        const float x1 = x0 + side, y1 = y0 + side;
        const float W = static_cast<float>(width), H = static_cast<float>(height);
        std::vector<UiVertex> v;
        std::vector<uint32_t> idx;
        auto quad = [&](float ax, float ay, float bx, float by, uint32_t color, float u0, float v0, float u1, float v1) {
            if (bx <= ax || by <= ay)
                return;
            const uint32_t base = static_cast<uint32_t>(v.size());
            v.push_back({ax, ay, 0, color, u0, v0});
            v.push_back({bx, ay, 0, color, u1, v0});
            v.push_back({bx, by, 0, color, u1, v1});
            v.push_back({ax, by, 0, color, u0, v1});
            for (uint32_t i : {0u, 1u, 2u, 0u, 2u, 3u})
                idx.push_back(base + i);
        };
        const uint32_t black = 0xff000000u, white = 0xffffffffu;
        const uint32_t mesh0 = [&] {
            quad(0, 0, W, std::max(0.0f, y0), black, 0, 0, 0, 0);
            quad(0, y1, W, H, black, 0, 0, 0, 0);
            quad(0, std::max(0.0f, y0), std::max(0.0f, x0), std::min(H, y1), black, 0, 0, 0, 0);
            quad(x1, std::max(0.0f, y0), W, std::min(H, y1), black, 0, 0, 0, 0);
            return renderer->UiCreateFrameMesh(v.data(), static_cast<uint32_t>(v.size()), idx.data(), static_cast<uint32_t>(idx.size()));
        }();
        if (mesh0) {
            scopeMeshes.push_back(mesh0);
            UiDraw d;
            d.mesh = mesh0;
            renderer->UiSubmit(UiLayer::Gui, d);
        }
        if (scopeTexture) {
            v.clear();
            idx.clear();
            quad(x0, y0, x1, y1, white, 0, 0, 1, 1);
            const uint32_t mesh1 = renderer->UiCreateFrameMesh(v.data(), static_cast<uint32_t>(v.size()), idx.data(), static_cast<uint32_t>(idx.size()));
            if (mesh1) {
                scopeMeshes.push_back(mesh1);
                UiDraw d;
                d.mesh = mesh1;
                d.texture = scopeTexture;
                renderer->UiSubmit(UiLayer::Gui, d);
            }
        }
    }

    void HandRenderer::DrawModel(const std::vector<Quad>& quads, uint32_t texture, const glm::mat4& model,
                                 const glm::vec3& light, float alphaCutoff, bool glint, float redTint) {
        if (!renderer || quads.empty())
            return;
        static const glm::vec3 l0 = glm::normalize(glm::vec3(0.2f, 1.0f, -0.7f));
        static const glm::vec3 l1 = glm::normalize(glm::vec3(-0.2f, 1.0f, 0.7f));
        const glm::mat3 normalMatrix(model);
        std::vector<UiVertex> vertices;
        vertices.reserve(quads.size() * 4);
        std::vector<uint32_t> indices;
        indices.reserve(quads.size() * 6);
        for (const Quad& q : quads) {
            glm::vec3 n = normalMatrix * q.n;
            const float len = glm::length(n);
            if (len > 1e-6f)
                n /= len;
            const float lit = std::min(1.0f, (std::max(0.0f, glm::dot(n, l0)) + std::max(0.0f, glm::dot(n, l1))) * 0.6f + 0.4f);
            const uint32_t color = PackColor(light * lit);
            const uint32_t base = static_cast<uint32_t>(vertices.size());
            for (int i = 0; i < 4; i++)
                vertices.push_back({q.p[i].x, q.p[i].y, q.p[i].z, color, q.uv[i].x, q.uv[i].y});
            for (uint32_t i : {0u, 1u, 2u, 0u, 2u, 3u})
                indices.push_back(base + i);
        }
        const uint32_t mesh = renderer->UiCreateFrameMesh(vertices.data(), static_cast<uint32_t>(vertices.size()),
                                                     indices.data(), static_cast<uint32_t>(indices.size()));
        if (!mesh)
            return;
        (worldPass ? worldMeshes : pendingMeshes).push_back(mesh);
        UiDraw draw;
        draw.mesh = mesh;
        draw.texture = texture;
        std::copy_n(&model[0][0], 16, draw.model);
        draw.premultiplied = false;
        draw.alphaCutoff = alphaCutoff;
        draw.blend = 1;
        draw.depth = true;
        if (redTint > 0.0f) {
            draw.overlay[0] = 1.0f; draw.overlay[1] = 0.0f; draw.overlay[2] = 0.0f; draw.overlay[3] = 0.3f;
        }
        draw.shadowOnly = worldPass && worldShadowOnly && !dollClip;
        if (worldPass && dollClip) {
            draw.scissor = true;
            std::copy_n(dollClip, 4, draw.scissorRect);
        }
        const UiLayer layer = !worldPass ? UiLayer::Hand : dollClip ? UiLayer::Doll : UiLayer::World;
        renderer->UiSubmit(layer, draw);
        if (glint) {
            if (glintTexture == 0)
                glintTexture = ui::CreateGlintTexture(*renderer, &glintWidth);
            if (glintTexture != 0) {
                const auto ms = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                                          std::chrono::steady_clock::now().time_since_epoch())
                                                          .count());
                const ui::GlintTransform g = ui::MakeGlintTransform(ms);
                draw.glintTexture = glintTexture;
                draw.glintStrength = PERLIN_ENV("PERLIN_GLINT_STRENGTH") ? static_cast<float>(std::atof(PERLIN_ENV("PERLIN_GLINT_STRENGTH"))) : 1.0f;
                std::copy(g.matrix, g.matrix + 4, draw.glintMatrix);
                std::copy(g.offset, g.offset + 2, draw.glintOffset);
                renderer->UiSubmit(layer, draw);
            }
        }
    }

    void HandRenderer::BeginWorldItems() {
        if (!renderer)
            return;
        for (uint32_t mesh : worldMeshes)
            renderer->UiDestroyMesh(mesh);
        worldMeshes.clear();
    }

    void HandRenderer::DrawWorldItem(const ui::HudItem& item, const glm::dvec3& feet, float age, uint64_t seed, const glm::vec3& light) {
        if (!renderer || item.Empty())
            return;
        const Model& model = ItemModel(item);
        if (!model.valid)
            return;
        const auto hash = [&](uint64_t salt) {
            uint64_t x = (seed + salt) * 0x9E3779B97F4A7C15ull;
            x ^= x >> 29;
            x *= 0xBF58476D1CE4E5B9ull;
            x ^= x >> 32;
            return static_cast<float>(x & 0xFFFFFF) / static_cast<float>(0x1000000);
        };
        const float bobOffs = hash(1) * 2.0f * kPi;
        const itemdisplay::Entry& groundDisplay = itemdisplay::Lookup(item.name, true);
        const bool tabled = groundDisplay.found && groundDisplay.ground.present;
        const float scale = tabled ? groundDisplay.ground.scale.y : (model.block ? 0.25f : 0.5f);
        const float display = model.block ? 3.0f / 16.0f : 2.0f / 16.0f;
        const float yOff = std::sin(age / 10.0f + bobOffs) * 0.1f + 0.1f;
        const float spin = (age / 20.0f + bobOffs) / kDeg;
        const int copies = item.count <= 1 ? 1 : item.count <= 16 ? 2 : item.count <= 32 ? 3 : item.count <= 48 ? 4 : 5;
        worldPass = true;
        for (int i = 0; i < copies; i++) {
            glm::mat4 m = T(static_cast<float>(feet.x), static_cast<float>(feet.y), static_cast<float>(feet.z));
            m = m * T(0.0f, yOff + 0.25f * scale, 0.0f) * Ry(spin);
            if (i > 0)
                // spread stacked copies so higher counts stay readable
                m = m * T((hash(10 + i) * 2.0f - 1.0f) * 0.15f, (hash(20 + i) * 2.0f - 1.0f) * 0.15f,
                          (hash(30 + i) * 2.0f - 1.0f) * 0.15f);
            if (tabled)
                m = m * itemdisplay::Apply(groundDisplay.ground, false) * T(-0.5f, -0.5f, -0.5f);
            else
                m = m * T(0.0f, display, 0.0f) * S(scale) * T(-0.5f, -0.5f, -0.5f);
            DrawModel(model.quads, model.texture, m, light, 0.1f, false);
        }
        worldPass = false;
    }

    void HandRenderer::DrawHeldItem(const ui::HudItem& item, const glm::mat4& arm, bool left, const glm::vec3& light, float bowUseTicks) {
        if (!renderer || item.Empty())
            return;
        ui::HudItem renderedItem = item;
        if (item.name == "minecraft:bow" && bowUseTicks >= 0.0f) {
            const int frame = bowUseTicks >= 18.0f ? 2 : bowUseTicks >= 13.0f ? 1 : 0;
            renderedItem.name = "minecraft:bow_pulling_" + std::to_string(frame);
        }
        const Model& model = ItemModel(renderedItem);
        if (!model.valid)
            return;
        const float side = left ? -1.0f : 1.0f;
        std::string n = item.name.rfind("minecraft:", 0) == 0 ? item.name.substr(10) : item.name;
        const auto ends = [&](const char* suffix) {
            const size_t l = std::char_traits<char>::length(suffix);
            return n.size() >= l && n.compare(n.size() - l, l, suffix) == 0;
        };
        const bool handheld = ends("_sword") || ends("_pickaxe") || ends("_axe") || ends("_shovel") || ends("_hoe") || n == "stick" ||
                              n == "blaze_rod" || n == "bone" || n == "mace" || n == "breeze_rod" || ends("_on_a_stick") || n == "fishing_rod";
        const itemdisplay::Entry& display = itemdisplay::Lookup(item.name, false);
        const itemdisplay::Transform& third = left ? display.thirdLeft : display.thirdRight;
        if (item.name == "minecraft:trident") {
            const glm::mat4 base = arm * Rx(-90.0f) * Ry(180.0f) * T(side / 16.0f, 2.0f / 16.0f, -10.0f / 16.0f);
            worldPass = true;
            const bool drawn = DrawGeometryItem(item, base, left, false, light);
            worldPass = false;
            if (drawn)
                return;
        }
        if (display.found && third.present) {
            glm::mat4 held = arm * Rx(-90.0f) * Ry(180.0f) * T(side / 16.0f, 2.0f / 16.0f, -10.0f / 16.0f);
            held = held * itemdisplay::Apply(third, left) * T(-0.5f, -0.5f, -0.5f);
            worldPass = true;
            DrawModel(model.quads, model.texture, held, light, 0.1f, item.enchanted);
            worldPass = false;
            return;
        }
        glm::vec3 rot(0.0f), tr(0.0f);
        float scale = 1.0f;
        if (model.block) {
            rot = {75.0f, 45.0f, 0.0f};
            tr = {0.0f, 2.5f, 0.0f};
            scale = 0.375f;
        } else if (handheld) {
            rot = {0.0f, -90.0f, 55.0f};
            tr = {0.0f, 4.0f, 0.5f};
            scale = 0.85f;
        } else {
            tr = {0.0f, 3.0f, 1.0f};
            scale = 0.55f;
        }
        glm::mat4 m = arm * Rx(-90.0f) * Ry(180.0f) * T(side / 16.0f, 2.0f / 16.0f, -10.0f / 16.0f);
        m = m * T(side * tr.x / 16.0f, tr.y / 16.0f, tr.z / 16.0f) * (Rx(rot.x) * Ry(side * rot.y) * Rz(side * rot.z)) * S(scale) *
            T(-0.5f, -0.5f, -0.5f);
        worldPass = true;
        DrawModel(model.quads, model.texture, m, light, 0.1f, item.enchanted);
        worldPass = false;
    }

    // ItemInHandRenderer third person (main hand 0x14597CAF0, offhand 0x14597EF30, blocks 0x145981B60)
    void HandRenderer::DrawBedrockHeldItem(const ui::HudItem& item, const glm::mat4& bone, bool left, const glm::vec3& light,
                                           float bowUseTicks) {
        if (!renderer || item.Empty())
            return;
        ui::HudItem renderedItem = item;
        if (item.name == "minecraft:bow" && bowUseTicks >= 0.0f) {
            const int frame = bowUseTicks >= 18.0f ? 2 : bowUseTicks >= 13.0f ? 1 : 0;
            renderedItem.name = "minecraft:bow_pulling_" + std::to_string(frame);
        }
        const Model& model = ItemModel(renderedItem);
        if (!model.valid)
            return;
        const std::string n = item.name.rfind("minecraft:", 0) == 0 ? item.name.substr(10) : item.name;
        const auto ends = [&](const char* suffix) {
            const size_t l = std::char_traits<char>::length(suffix);
            return n.size() >= l && n.compare(n.size() - l, l, suffix) == 0;
        };
        // Item::isHandEquipped
        const bool handheld = ends("_sword") || ends("_pickaxe") || ends("_axe") || ends("_shovel") || ends("_hoe") || n == "stick" ||
                              n == "blaze_rod" || n == "bone" || n == "mace" || n == "breeze_rod";
        const glm::mat4 flat = S(0.375f) * Rz(60.0f) * Rx(-90.0f) * Rz(20.0f);
        glm::mat4 m = bone;
        if (n == "fishing_rod" || n == "carrot_on_a_stick" || n == "warped_fungus_on_a_stick")
            m = m * Rx(180.0f) * Rx(-15.0f) * (left ? T(0.1f, 0.265f, 0.0f) : T(0.1f, 0.065f, -0.0625f)) * S(0.625f) * Rx(80.0f) * Ry(45.0f);
        else if (n == "bow")
            m = m * Ry(180.0f) * T(-0.025f, 0.265f, -0.375f) * S(0.625f) * Rx(80.0f) * Ry(45.0f);
        // x: 0.4 + 0.3 * actor scale
        else if (n == "crossbow")
            m = m * Ry(102.5f) * Rz(37.5f) * Rx(65.0f) * T(0.7f, -0.2f, -0.325f) * S(0.625f);
        else if (n == "glow_stick")
            m = m * T(0.0f, 0.1875f, -0.3125f) * Rx(200.0f) * Ry(45.0f) * S(0.375f) * T(0.0f, 0.0f, -0.625f) * S(1.5f) * Rx(40.0f) * Rz(40.0f);
        else if (n == "arrow")
            m = m * (left ? T(0.375f, 0.1875f, -0.125f) : T(0.25f, 0.1875f, -0.1875f)) * flat;
        else if (handheld)
            m = m * (left ? T(0.0f, 0.265f, 0.0f) : Ry(180.0f) * T(0.1f, 0.265f, 0.0f)) * S(0.625f) * Rx(80.0f) * Ry(45.0f);
        else if (model.block) {
            m = m * T(0.0f, n == "snow_layer" ? 0.03125f : 0.1875f, -0.3125f) * Rx(200.0f) * Ry(225.0f) * S(0.375f);
            // main-hand only: shapes 10 (stairs), 22 (chest) and the carved pumpkin turn to face out
            if (!left && (ends("_stairs") || n == "chest" || n == "trapped_chest" || n == "carved_pumpkin"))
                m = m * Ry(180.0f);
        } else
            m = m * T(0.3125f, 0.1875f, -0.1875f) * flat;
        // blocks are tessellated centred; flat sprites start at their corner, grip at (0,0)
        m = m * (model.block ? T(-0.5f, -0.5f, -0.5f) : T(0.0f, 0.0f, -0.5f));
        worldPass = true;
        DrawModel(model.quads, model.texture, m, light, 0.1f, item.enchanted);
        worldPass = false;
    }

    void HandRenderer::RenderArm(const FrameIn& in, const HandSettings& s, bool rightArm, float swing, float equip) {
        BuildArms(in.slimArm);
        EnsureSkin(in);
        const float f = rightArm ? 1.0f : -1.0f;
        const float f1 = std::sqrt(swing);
        const float f2 = -0.3f * std::sin(f1 * kPi);
        const float f3 = 0.4f * std::sin(f1 * kPi * 2.0f);
        const float f4 = -0.4f * std::sin(swing * kPi);
        glm::mat4 m = in.viewEffect;
        m = m * Custom(s.armX, s.armY, s.armZ, s.armRotX, s.armRotY, s.armRotZ, s.armScale, f);
        m = m * T(f * (f2 + 0.64000005f), f3 - 0.6f + equip * -0.6f, f4 - 0.71999997f);
        m = m * Ry(f * 45.0f);
        const float f5 = std::sin(swing * swing * kPi);
        const float f6 = std::sin(f1 * kPi);
        m = m * Ry(f * f6 * 70.0f);
        m = m * Rz(f * f5 * -20.0f);
        static const float au = PERLIN_ENV("PERLIN_ARMU") ? static_cast<float>(std::atof(PERLIN_ENV("PERLIN_ARMU"))) : 1.0f;
        m = m * T(f * -1.0f * au, 3.6f * au, 3.5f * au);
        m = m * Rz(f * 120.0f);
        m = m * Rx(200.0f);
        m = m * Ry(f * -135.0f);
        m = m * T(f * 5.6f * au, 0.0f, 0.0f);
        const float pivotY = in.slimArm ? 2.5f : 2.0f;
        const glm::mat4 part = (rightArm ? T(-5.0f / 16.0f, pivotY / 16.0f, 0.0f) * Rz(0.1f / kDeg)
                                         : T(5.0f / 16.0f, pivotY / 16.0f, 0.0f) * Rz(-0.1f / kDeg)) *
                               S(1.0f / 16.0f) * S(s.scale);
        m = m * part;
        const ArmModel& arm = (in.slimArm ? armSlim : armClassic)[rightArm ? 0 : 1];
        DrawModel(arm.base, skinTexture, m, in.light, 0.1f, false, in.hurt);
        if (rightArm ? in.rightSleeve : in.leftSleeve) {
            EnsureSleeves3d(in);
            const std::vector<Quad>& voxels = sleeve3d[rightArm ? 0 : 1];
            if (sleeve3dValid && !voxels.empty())
                DrawModel(voxels, skinTexture, m, in.light, 0.1f, false, in.hurt);
            else
                DrawModel(arm.sleeve, skinTexture, m, in.light, 0.1f, false, in.hurt);
        }
    }

    void HandRenderer::EnsureSleeves3d(const FrameIn& in) {
        if (sleeve3dSerial == in.skinSerial && sleeve3dFlags == in.skins3d && sleeve3dFallback == skinFallback)
            return;
        sleeve3dSerial = in.skinSerial;
        sleeve3dFlags = in.skins3d;
        sleeve3dFallback = skinFallback;
        sleeve3d[0].clear();
        sleeve3d[1].clear();
        sleeve3dValid = false;
        const LoadedSkin* skin = in.skin;
        if (!in.skins3d || skinFallback || !skin || !skin->hasSkin() || skin->skinWidth < 64 || skin->skinHeight < 64)
            return;
        attachable::Geometry source;
        source.textureWidth = 64.0f;
        source.textureHeight = 64.0f;
        const float w = in.slimArm ? 3.0f : 4.0f;
        for (int side = 0; side < 2; side++) {
            const bool right = side == 0;
            attachable::Bone bone;
            bone.name = right ? "rightsleeve" : "leftsleeve";
            bone.inflate = 0.25f;
            attachable::Cube cube;
            cube.size = {w, 12.0f, 4.0f};
            cube.origin = {right ? (in.slimArm ? -7.0f : -8.0f) : 4.0f, 0.0f, -2.0f};
            cube.uv = right ? glm::vec2(40.0f, 32.0f) : glm::vec2(48.0f, 48.0f);
            cube.inflate = 0.25f;
            cube.boxUv = true;
            cube.mirror = false;
            bone.cubes.push_back(cube);
            source.bones.push_back(std::move(bone));
        }
        const attachable::Geometry voxel =
            attachable::VoxelizeSkinLayers(source, skin->skinRgba.data(), skin->skinWidth, skin->skinHeight);
        for (size_t i = 0; i < voxel.bones.size() && i < 2; i++) {
            const float dx = i == 0 ? 5.0f : -5.0f;
            for (Quad q : voxel.bones[i].voxels) {
                for (glm::vec3& p : q.p) {
                    p.x += dx;
                    p.y = 10.0f - p.y;
                }
                q.n.y = -q.n.y;
                if (glm::dot(glm::cross(q.p[1] - q.p[0], q.p[2] - q.p[0]), q.n) > 0.0f)
                    std::swap(q.p[1], q.p[3]);
                sleeve3d[i].push_back(q);
            }
        }
        sleeve3dValid = true;
    }

    void HandRenderer::RenderItem(const FrameIn& in, const HandSettings& s, const ui::HudItem& item, bool rightArm,
                                  float swing, float equip) {
        const float side = rightArm ? 1.0f : -1.0f;
        glm::mat4 m = in.viewEffect;
        m = m * Custom(s.itemX, s.itemY, s.itemZ, s.itemRotX, s.itemRotY, s.itemRotZ, s.itemScale, side);
        const bool isMain = &item == &mainShown;
        const bool isBow = item.name == "minecraft:bow", isTrident = item.name == "minecraft:trident",
                   isCrossbow = item.name == "minecraft:crossbow";
        const bool useAnim = isMain && useHeld && !blocking && (isBow || isTrident || isCrossbow);
        const auto applyArm = [&](glm::mat4 b) { return b * T(side * 0.56f, -0.52f + equip * -0.6f, -0.72f); };
        if (useAnim) {
            m = applyArm(m);
            const float t = static_cast<float>(usedTicks) + in.partial;
            float f9;
            if (isBow) {
                m = m * T(side * -0.2785682f, 0.18344387f, 0.15731531f) * Rx(-13.935f) * Ry(side * 35.3f) * Rz(side * -9.785f);
                f9 = t / 20.0f;
                f9 = (f9 * f9 + f9 * 2.0f) / 3.0f;
            } else if (isTrident) {
                m = m * T(side * -0.5f, 0.7f, 0.1f) * Rx(-55.0f) * Ry(side * 35.3f) * Rz(side * -9.785f);
                f9 = t / 10.0f;
            } else {
                m = m * T(side * -0.4785682f, -0.094387f, 0.05731531f) * Rx(-11.935f) * Ry(side * 65.3f) * Rz(side * -9.785f);
                f9 = t / 25.0f;
            }
            f9 = std::min(f9, 1.0f);
            if (f9 > 0.1f) {
                const float shake = std::sin((t - 0.1f) * 1.3f) * (f9 - 0.1f);
                m = m * T(0.0f, shake * 0.004f, 0.0f);
            }
            m = m * T(0.0f, 0.0f, isTrident ? f9 * 0.2f : f9 * 0.04f) * glm::scale(glm::mat4(1.0f), glm::vec3(1.0f, 1.0f, 1.0f + f9 * 0.2f)) *
                Ry(side * -45.0f);
        }
        const float sq = std::sqrt(swing);
        if (!useAnim) {
        if (eatTicks > 0) {
            const float remaining = 32.0f - (static_cast<float>(eatTicks) + in.partial) + 1.0f;
            const float f0 = remaining / 32.0f;
            if (f0 < 0.8f)
                m = m * T(0.0f, std::fabs(std::cos(remaining / 4.0f * kPi) * 0.1f), 0.0f);
            const float f3 = 1.0f - std::pow(f0, 27.0f);
            m = m * T(side * f3 * 0.6f, f3 * -0.5f, 0.0f) * Ry(side * f3 * 90.0f) * Rx(f3 * 10.0f) * Rz(side * f3 * 30.0f);
        } else if (IsThrowable(item.name) && swing > 0.0f) {
            // overhead throw, arm rises then snaps forward
            const float lift = std::sin(std::min(swing * 1.4f, 1.0f) * kPi * 0.5f);
            const float snap = std::sin(swing * kPi);
            m = m * T(side * -0.15f * lift, 0.35f * lift, -0.25f * lift) * Rx(-38.0f * lift + 42.0f * snap);
        } else {
            m = m * T(side * (-0.4f * std::sin(sq * kPi)), 0.2f * std::sin(sq * kPi * 2.0f), -0.2f * std::sin(swing * kPi));
        }
        m = m * T(side * 0.56f, -0.52f + equip * -0.6f, -0.72f);
        const float f = std::sin(swing * swing * kPi);
        m = m * Ry(side * (45.0f + f * -20.0f));
        const float f1 = std::sin(sq * kPi);
        m = m * Rz(side * f1 * -20.0f);
        m = m * Rx(f1 * -80.0f);
        m = m * Ry(side * -45.0f);
        }
        if (item.name == "minecraft:shield") {
            RenderShield(in, s, item, rightArm, m);
            return;
        }

        if (item.name == "minecraft:trident" && s.attachables && DrawGeometryItem(item, m, !rightArm, true, in.light))
            return;
        if (s.attachables && RenderAttachable(in, s, item, rightArm, m))
            return;
        if (item.name == "minecraft:spyglass") {
            const Model& spy = SpyglassModel();
            if (spy.valid) {
                m = m * S(s.scale) * T(-0.5f, -0.5f, -0.5f);
                DrawModel(spy.quads, spy.texture, m, in.light, 0.1f, item.enchanted);
                return;
            }
        }
        ui::HudItem pulled;
        if (useAnim && (isBow || isCrossbow)) {
            const float pull = (static_cast<float>(usedTicks) + in.partial) / (isBow ? 20.0f : 25.0f);
            const int frame = isBow ? (pull >= 0.9f ? 2 : pull >= 0.65f ? 1 : 0) : (pull >= 1.0f ? 2 : pull >= 0.58f ? 1 : 0);
            pulled = item;
            pulled.name = std::string(isBow ? "minecraft:bow_pulling_" : "minecraft:crossbow_pulling_") + std::to_string(frame);
        }
        const Model& model = ItemModel(pulled.name.empty() ? item : pulled);
        if (!model.valid)
            return;
        const itemdisplay::Entry& display = itemdisplay::Lookup(item.name, false);
        const itemdisplay::Transform& first = rightArm ? display.firstRight : display.firstLeft;
        if (display.found && first.present) {
            m = m * itemdisplay::Apply(first, !rightArm);
        } else if (model.block) {
            m = m * Ry(rightArm ? 45.0f : -45.0f) * S(0.4f);
        } else {
            m = m * T(side * (1.13f / 16.0f), 3.2f / 16.0f, 1.13f / 16.0f) * (Ry(-90.0f) * Rz(25.0f)) * S(0.68f);
        }
        m = m * S(s.scale) * T(-0.5f, -0.5f, -0.5f);
        DrawModel(model.quads, model.texture, m, in.light, 0.1f, item.enchanted);
    }

    namespace {
        void JavaBox(std::vector<attachable::Quad>& out, glm::vec3 o, glm::vec3 sz, float u, float v) {
            const float w = sz.x, h = sz.y, d = sz.z;
            const glm::vec3 lo = o, hi = o + sz;
            auto face = [&](glm::vec3 origin, glm::vec3 du, glm::vec3 dv, float u0, float v0, float uw, float vh, glm::vec3 n) {
                attachable::Quad q;
                q.p[0] = origin;
                q.p[1] = origin + du;
                q.p[2] = origin + du + dv;
                q.p[3] = origin + dv;
                const float k = 1.0f / 64.0f;
                q.uv[0] = {u0 * k, v0 * k};
                q.uv[1] = {(u0 + uw) * k, v0 * k};
                q.uv[2] = {(u0 + uw) * k, (v0 + vh) * k};
                q.uv[3] = {u0 * k, (v0 + vh) * k};
                q.n = n;
                out.push_back(q);
            };
            face({lo.x, lo.y, lo.z}, {w, 0, 0}, {0, h, 0}, u + d, v + d, w, h, {0, 0, -1});
            face({hi.x, lo.y, hi.z}, {-w, 0, 0}, {0, h, 0}, u + d + w + d, v + d, w, h, {0, 0, 1});
            face({lo.x, lo.y, hi.z}, {0, 0, -d}, {0, h, 0}, u, v + d, d, h, {-1, 0, 0});
            face({hi.x, lo.y, lo.z}, {0, 0, d}, {0, h, 0}, u + d + w, v + d, d, h, {1, 0, 0});
            face({lo.x, lo.y, lo.z}, {w, 0, 0}, {0, 0, d}, u + d, v, w, d, {0, -1, 0});
            face({lo.x, hi.y, hi.z}, {w, 0, 0}, {0, 0, -d}, u + d + w, v, w, d, {0, 1, 0});
        }
    }

    void HandRenderer::RenderShield(const FrameIn& in, const HandSettings& s, const ui::HudItem& item, bool rightArm,
                                    const glm::mat4& hand) {
        if (!shieldTexture)
            shieldTexture = AttachableTexture("textures/entity/shield");
        if (!shieldTexture)
            return;
        if (shieldQuads.empty()) {
            JavaBox(shieldQuads, {-6, -11, -2}, {12, 22, 1}, 0, 0);
            JavaBox(shieldQuads, {-1, -3, -1}, {2, 6, 6}, 26, 0);
        }
        struct Display { glm::vec3 rot, tr; float scale; };
        static const Display idle[2] = {{{0, 180, 5}, {-10, 1.75f, -10}, 1.25f}, {{0, 180, 5}, {10, 0, -10}, 1.25f}};
        static const Display block[2] = {{{0, 180, -5}, {-15, 3.25f, -11}, 1.25f}, {{0, 180, -5}, {5, 5, -11}, 1.25f}};
        const bool use = blocking && (rightArm == !s.leftHanded);
        const Display& d = (use ? block : idle)[rightArm ? 0 : 1];
        const float f = rightArm ? 1.0f : -1.0f;
        glm::mat4 m = hand * T(f * d.tr.x / 16.0f, d.tr.y / 16.0f, d.tr.z / 16.0f) * Rx(d.rot.x) * Ry(d.rot.y * f) * Rz(d.rot.z * f) *
                      S(d.scale) * S(s.scale);
        m = m * Ry(180.0f) * glm::scale(glm::mat4(1.0f), glm::vec3(1.0f / 16.0f, -1.0f / 16.0f, -1.0f / 16.0f));
        DrawModel(shieldQuads, shieldTexture, m, in.light, 0.1f, item.enchanted);
    }

    uint32_t HandRenderer::AttachableTexture(const std::string& path) {
        const auto it = attachableTextures.find(path);
        if (it != attachableTextures.end())
            return it->second;
        Texture t;
        uint32_t id = 0;
        if (ui::UiAssets::Instance().Image(path, t) && t.width > 0)
            id = renderer->UiCreateTexture(t.width, t.height, t.pixels.data(), false);
        attachableTextures[path] = id;
        return id;
    }

    bool HandRenderer::DrawGeometryItem(const ui::HudItem& item, const glm::mat4& base, bool leftHand, bool firstPerson,
                                        const glm::vec3& light) {
        const itemdisplay::Entry& display = itemdisplay::Lookup(item.name, false);
        const itemdisplay::Transform& t = firstPerson ? (leftHand ? display.firstLeft : display.firstRight)
                                                       : (leftHand ? display.thirdLeft : display.thirdRight);
        if (!display.found || !t.present)
            return false;
        attachables.Configure(ui::UiAssets::Instance().VanillaRoot());
        if (!attachables.Has(item.name))
            return false;
        attachable::ItemState st;
        st.identifier = item.name;
        st.mainHand = true;
        st.firstPerson = firstPerson;
        st.enchanted = item.enchanted;
        st.bindPose = true;
        const attachable::Frame frame = attachables.Evaluate(st, firstPerson ? 2 : 3);
        if (!frame.valid || frame.parts.empty())
            return false;
        const glm::mat4 m = base * itemdisplay::Apply(t, leftHand) * T(-0.5f, -0.5f, -0.5f) * S(1.0f / 16.0f) * T(0.0f, -24.0f, 0.0f);
        for (const attachable::Part& part : frame.parts) {
            const uint32_t texture = AttachableTexture(part.texture);
            if (texture)
                DrawModel(part.quads, texture, m, light, 0.1f, frame.glint || item.enchanted);
        }
        return true;
    }

    HandRenderer::RigState HandRenderer::Rig(float partial) const {
        RigState r;
        r.attackTime = SwingProgress(partial);
        r.armHeight = oMainHandHeight + (mainHandHeight - oMainHandHeight) * partial;
        r.offArmHeight = oOffHandHeight + (offHandHeight - oOffHandHeight) * partial;
        r.main = mainShown.Empty() ? std::string() : mainShown.name;
        r.off = offShown.Empty() ? std::string() : offShown.name;
        r.using_ = useHeld;
        r.blocking = blocking;
        r.usedTicks = static_cast<float>(usedTicks) + (useHeld ? partial : 0.0f);
        return r;
    }

    bool HandRenderer::RigItem(const std::string& name) {
        if (name != "minecraft:bow")
            return false;
        attachables.Configure(ui::UiAssets::Instance().VanillaRoot());
        return attachables.Has(name);
    }

    bool HandRenderer::ThirdPersonAttachable(const std::string& item, bool left, bool firstPerson, bool sneaking, bool using_,
                                             float usedTicks, bool blocking, double life, const std::string& mainItem,
                                             const std::string& offItem, attachable::Frame& out) {
        attachables.Configure(ui::UiAssets::Instance().VanillaRoot());
        if (!attachables.Has(item))
            return false;
        attachable::ItemState st;
        st.identifier = item;
        st.mainHand = !left;
        st.firstPerson = firstPerson;
        st.lifeSeconds = life;
        st.using_ = using_;
        st.usedTicks = usedTicks;
        st.blocking = blocking;
        st.mainHandItem = mainItem;
        st.offHandItem = offItem;
        (void)sneaking;
        if (using_) {
            if (item == "minecraft:bow") {
                const float pull = usedTicks / 20.0f;
                st.animationFrame = pull >= 0.9f ? 3 : pull >= 0.65f ? 2 : 1;
            } else if (item == "minecraft:crossbow") {
                const float charge = usedTicks / 25.0f;
                st.animationFrame = charge >= 1.0f ? 3 : charge >= 0.58f ? 2 : 1;
            }
        }
        out = attachables.Evaluate(st, left ? 11 : 10);
        return out.valid && !out.parts.empty();
    }

    bool HandRenderer::ArmorAttachable(const std::string& identifier, std::string& geometry, std::string& texture) {
        attachables.Configure(ui::UiAssets::Instance().VanillaRoot());
        return attachables.ArmorInfo(identifier, geometry, texture);
    }

    bool HandRenderer::RenderAttachable(const FrameIn& in, const HandSettings& s, const ui::HudItem& item, bool rightArm,
                                        const glm::mat4& hand) {
        attachables.Configure(ui::UiAssets::Instance().VanillaRoot());
        static const bool debug = PERLIN_ENV("PERLIN_ATTACH_DEBUG") != nullptr;
        if (!attachables.Has(item.name)) {
            if (debug)
                std::cout << "[attach] no attachable for " << item.name << " root=" << ui::UiAssets::Instance().VanillaRoot() << std::endl;
            return false;
        }
        if (!(item.name == "minecraft:bow" && PERLIN_ENV("PERLIN_BOW_ATTACHABLE")) && !(item.name == "minecraft:crossbow" && PERLIN_ENV("PERLIN_CROSSBOW_ATTACHABLE")))
            return false;
        attachable::ItemState st;
        st.identifier = item.name;
        st.mainHand = rightArm == !s.leftHanded;
        st.firstPerson = true;
        st.enchanted = item.enchanted;
        st.partial = in.partial;
        st.lifeSeconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - startedAt).count();
        st.mainHandItem = mainShown.name;
        st.offHandItem = offShown.name;
        st.using_ = st.mainHand && useHeld;
        st.usedTicks = static_cast<float>(usedTicks) + (useHeld ? in.partial : 0.0f);
        st.blocking = blocking;
        if (st.using_) {
            const float pull = st.usedTicks / 20.0f;
            if (item.name == "minecraft:bow")
                st.animationFrame = pull >= 0.9f ? 3 : pull >= 0.65f ? 2 : 1;
            else if (item.name == "minecraft:crossbow") {
                const float charge = st.usedTicks / 25.0f;
                st.animationFrame = charge >= 1.0f ? 3 : charge >= 0.58f ? 2 : 1;
            }
        }
        const attachable::Frame frame = attachables.Evaluate(st, st.mainHand ? 0 : 1);
        if (debug) {
            std::cout << "[attach] " << item.name << " valid=" << frame.valid << " parts=" << frame.parts.size();
            for (const auto& p : frame.parts) {
                glm::vec3 lo(1e9f), hi(-1e9f);
                for (const auto& q : p.quads)
                    for (const auto& v : q.p) { lo = glm::min(lo, v); hi = glm::max(hi, v); }
                std::cout << " [" << p.texture << " quads=" << p.quads.size() << " lo=" << lo.x << "," << lo.y << "," << lo.z
                          << " hi=" << hi.x << "," << hi.y << "," << hi.z << "]";
            }
            std::cout << std::endl;
        }
        if (!frame.valid)
            return false;
        static const glm::mat4 kCrossbow = T(0.2f, 0.3f, -0.3f) * Ry(90.0f) * S(0.5f);
        glm::mat4 m = hand * (item.name == "minecraft:crossbow" && !PERLIN_ENV("PERLIN_ATTACH_K") ? kCrossbow : kAttachableAttach) * glm::scale(glm::mat4(1.0f), glm::vec3(s.scale / 16.0f));
        if (!frame.parts.empty() && frame.parts[0].texture.rfind("textures/entity/", 0) == 0) {
            glm::vec3 lo(1e9f), hi(-1e9f);
            for (const auto& p : frame.parts)
                for (const auto& q : p.quads)
                    for (const auto& v : q.p) { lo = glm::min(lo, v); hi = glm::max(hi, v); }
            const glm::vec3 c = (lo + hi) * 0.5f;
            m = hand * T(kEntityPlace.x, kEntityPlace.y, kEntityPlace.z) * Rz(kEntityPlace.w) *
                glm::scale(glm::mat4(1.0f), glm::vec3(s.scale / 16.0f * 0.6f)) * T(-c.x, -c.y, -c.z);
        }
        for (const attachable::Part& part : frame.parts) {
            const uint32_t texture = AttachableTexture(part.texture);
            if (texture)
                DrawModel(part.quads, texture, m, in.light, 0.1f, frame.glint);
        }
        return true;
    }

    void HandRenderer::Render(const FrameIn& in, const HandSettings& s) {
        if (!renderer)
            return;
        for (uint32_t mesh : pendingMeshes)
            renderer->UiDestroyMesh(mesh);
        pendingMeshes.clear();
        if (!s.show || firstTick || scoping)
            return;

        const float yaw = Unwrap(in.yaw);
        const float xb = Lerp(in.partial, xBobO, xBob), yb = Lerp(in.partial, yBobO, yBob);
        FrameIn local = in;
        if (s.sway)
            local.viewEffect = in.viewEffect * Rx((in.pitch - xb) * 0.1f) * Ry((yaw - yb) * 0.1f);

        const float swing = SwingProgress(in.partial);
        const float mainEquip = 1.0f - Lerp(in.partial, oMainHandHeight, mainHandHeight);
        const float offEquip = 1.0f - Lerp(in.partial, oOffHandHeight, offHandHeight);
        const bool mainRight = !s.leftHanded;
        const bool rig = s.bedrockRig && PERLIN_ENV("PERLIN_BEDROCK_RIG");
        if (mainShown.Empty()) {
            if (!rig)
                RenderArm(local, s, mainRight, swing, mainEquip);
        } else if (!(rig && RigItem(mainShown.name))) {
            RenderItem(local, s, mainShown, mainRight, swing, mainEquip);
        }
        if (s.showOffhand && !offShown.Empty() && !(rig && RigItem(offShown.name)))
            RenderItem(local, s, offShown, !mainRight, 0.0f, offEquip);
    }
}
