#pragma once
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "perlin/rpc.hpp"
#include "perlin/types.hpp"

// hooks into the client's own rendering, screens and systems
namespace perlin::hooks {
    using abi::EntityOverride;
    using abi::FogOverride;
    using abi::HudElement;
    using abi::KeyContext;
    using abi::MeshVertex;
    using abi::RenderStage;
    using abi::WindowInfo;

    namespace detail_hooks {
        template <class T> Result<void> send(Op op, const T& value) {
            abi::Writer w;
            w.write(value);
            return detail::call_void(op, w);
        }
    }

    // hud: hide or move one vanilla element (offset in gui pixels); stays until changed or mod unloads
    inline Result<void> hud_element(HudElement element, bool visible, float dx = 0, float dy = 0) {
        return detail_hooks::send(Op::HudElementSet, abi::HudElementState{element, visible, dx, dy});
    }
    // extra f3 lines under the vanilla ones, until replaced
    inline Result<void> debug_lines(std::vector<std::string> left, std::vector<std::string> right = {}) {
        return detail_hooks::send(Op::DebugLines, abi::DebugLinesData{std::move(left), std::move(right)});
    }

    // camera and world look: call every tick to hold (`ticks` = how long one call lasts)
    inline Result<void> fog(const FogOverride& fog) {
        return detail_hooks::send(Op::FogSet, fog);
    }
    inline Result<void> screen_tint(uint32_t argb, float vignette = 0, int ticks = 2) {
        return detail_hooks::send(Op::ScreenTint, abi::ScreenTintData{argb, vignette, ticks});
    }

    // entities: hide, scale, tint, retexture or rename one (by runtime id) or a whole type
    inline Result<void> entity_render(const EntityOverride& o) {
        return detail_hooks::send(Op::EntityRenderOverride, o);
    }

    // textured or colored world triangles, three vertices each; `texture` is a gfx::Texture id
    inline Result<void> world_mesh(std::string id, std::vector<MeshVertex> triangles, uint32_t texture = 0,
                                   RenderStage stage = RenderStage::AfterEntities, bool depth_test = true,
                                   int ticks = 2) {
        return detail_hooks::send(Op::WorldMesh, abi::WorldMeshData{std::move(id), stage, texture, depth_test,
                                                                    ticks, std::move(triangles)});
    }
    inline Result<void> clear_world_mesh(std::string_view id) {
        abi::Writer w;
        w.str(id);
        return detail::call_void(Op::WorldMeshClear, w);
    }

    inline Result<void> frame_limit(int fps, int unfocused_fps = 0) {
        return detail_hooks::send(Op::FrameLimit, abi::FrameLimitData{fps, unfocused_fps});
    }
    inline Result<WindowInfo> window() {
        return detail::call_as<WindowInfo>(Op::WindowState);
    }

    // screens: add a button to a vanilla screen (raises Event::ScreenButton with the id),
    // or open a blank screen of your own, drawn in Event::ScreenRender (Screen::Custom)
    inline Result<void> add_button(const abi::ScreenButtonDef& def) {
        return detail_hooks::send(Op::ScreenButtonAdd, def);
    }
    inline Result<void> remove_button(std::string_view id) {
        abi::Writer w;
        w.str(id);
        return detail::call_void(Op::ScreenButtonRemove, w);
    }
    inline Result<void> open_custom_screen(std::string_view title) {
        abi::Writer w;
        w.str(title);
        return detail::call_void(Op::ScreenOpenCustom, w);
    }
    // only inside an Event::Tooltip handler: lines added under the item's tooltip
    inline Result<void> tooltip(std::vector<std::string> lines) {
        abi::Writer w;
        w.write(lines);
        return detail::call_void(Op::TooltipAppend, w);
    }
    inline Result<void> loading_overlay(bool hidden) {
        return detail_hooks::send(Op::LoadingOverlay, abi::LoadingOverlayData{hidden});
    }

    // skins: 64x64 or 64x32 png per player (empty name = you); empty png removes the override; client-side only
    inline Result<void> skin(std::string player, std::vector<uint8_t> png, bool slim = false) {
        return detail_hooks::send(Op::SkinOverride,
                                  abi::SkinOverrideData{std::move(player), slim, std::move(png)});
    }

    // audio: looping ambient sound kept alive by id; play = false stops it
    inline Result<void> ambient(std::string id, std::string sound, float volume = 1, float pitch = 1,
                                bool play = true) {
        return detail_hooks::send(
            Op::AmbientSound, abi::AmbientSoundData{std::move(id), std::move(sound), volume, pitch, play});
    }

    // pings a bedrock server; answer arrives as Event::ServerPing with the returned id
    inline Result<uint32_t> ping(std::string host, uint16_t port = 19132) {
        abi::Writer w;
        w.write(abi::ServerPingRequest{std::move(host), port});
        return detail::call_as<uint32_t>(Op::ServerPing, w);
    }

    // mod-to-mod messages: Event::ModMessage in the receiver; empty `to` reaches every other mod
    inline Result<void> send(std::string to, std::string channel, std::span<const uint8_t> body = {}) {
        return detail_hooks::send(Op::ModSend,
                                  abi::ModMessageData{std::move(to), std::move(channel),
                                                      std::vector<uint8_t>(body.begin(), body.end())});
    }

    // resource and shader packs: `path` is a folder or .zip in the mod's own area (needs `files`)
    inline Result<void> register_pack(std::string path, bool enabled = true) {
        return detail_hooks::send(Op::PackRegister, abi::PackRegistration{std::move(path), enabled});
    }
    inline Result<void> shader_pack(std::string_view name) {
        abi::Writer w;
        w.str(name);
        return detail::call_void(Op::ShaderPackSet, w);
    }

    inline Result<void> particle(std::string name, double x, double y, double z, int count = 1,
                                 float spread = 0) {
        return detail_hooks::send(Op::ParticleSpawn,
                                  abi::ParticleRequest{std::move(name), x, y, z, count, spread});
    }

    // key bind with modifiers and a context; raises Event::Key like ctx.bind
    inline Result<void> bind(const abi::KeyBindEx& bind) {
        return detail_hooks::send(Op::KeyRegisterEx, bind);
    }
}
