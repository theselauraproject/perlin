#pragma once
#include <chrono>
#include <format>
#include <string>
#include <string_view>

#include "perlin/game.hpp"
#include "perlin/rpc.hpp"
#include "perlin/types.hpp"

namespace perlin::act {
    inline Result<void> say(std::string_view text) {
        abi::Writer w;
        w.str(text);
        return detail::call_void(Op::Chat, w);
    }

    template <class... Args> Result<void> say(std::format_string<Args...> fmt, Args&&... args) {
        return say(std::format(fmt, std::forward<Args>(args)...));
    }

    inline Result<void> run_command(std::string_view command) {
        abi::Writer w;
        w.str(command);
        return detail::call_void(Op::Command, w);
    }

    inline Result<void> message(std::string_view text) {
        abi::Writer w;
        w.str(text);
        return detail::call_void(Op::LocalMessage, w);
    }

    template <class... Args> Result<void> message(std::format_string<Args...> fmt, Args&&... args) {
        return message(std::format(fmt, std::forward<Args>(args)...));
    }

    inline Result<void> toast(std::string_view text,
                              std::chrono::milliseconds duration = std::chrono::milliseconds(3000)) {
        abi::Writer w;
        w.str(text).put<int32_t>(static_cast<int32_t>(duration.count()));
        return detail::call_void(Op::Toast, w);
    }

    struct TitleOptions {
        std::string subtitle;
        std::chrono::milliseconds fade_in{500}, stay{3500}, fade_out{1000};
    };

    inline Result<void> title(std::string_view text, const TitleOptions& options = {}) {
        abi::Writer w;
        w.str(text)
            .str(options.subtitle)
            .put<int32_t>(static_cast<int32_t>(options.fade_in.count()))
            .put<int32_t>(static_cast<int32_t>(options.stay.count()))
            .put<int32_t>(static_cast<int32_t>(options.fade_out.count()));
        return detail::call_void(Op::Title, w);
    }

    inline Result<void> select_slot(int slot) {
        abi::Writer w;
        w.put<int32_t>(slot);
        return detail::call_void(Op::InventorySelect, w);
    }

    inline Result<void> swing() {
        return detail::call_void(Op::SwingArm);
    }

    inline Result<void> use_item() {
        return detail::call_void(Op::UseItem);
    }

    inline Result<void> use_on(const BlockPos& pos, Face face = Face::Up) {
        abi::Writer w;
        w.put(pos.x).put(pos.y).put(pos.z).put<int32_t>(static_cast<int32_t>(face));
        return detail::call_void(Op::UseOnBlock, w);
    }

    inline Result<void> attack(uint64_t runtimeId) {
        abi::Writer w;
        w.put(runtimeId);
        return detail::call_void(Op::AttackEntity, w);
    }

    inline Result<void> interact(uint64_t runtimeId) {
        abi::Writer w;
        w.put(runtimeId);
        return detail::call_void(Op::InteractEntity, w);
    }

    inline Result<void> break_block(const BlockPos& pos, Face face = Face::Up) {
        abi::Writer w;
        w.put(pos.x).put(pos.y).put(pos.z).put<int32_t>(static_cast<int32_t>(face));
        return detail::call_void(Op::BreakBlock, w);
    }

    inline Result<void> look(float yaw, float pitch) {
        abi::Writer w;
        w.put(yaw).put(pitch);
        return detail::call_void(Op::Look, w);
    }

    inline Result<void> look_at(const Vec3& from, const Vec3& target) {
        const Vec3 d = target - from;
        const double yaw = -std::atan2(d.x, d.z) * 180.0 / std::numbers::pi;
        const double pitch = -std::atan2(d.y, d.horizontal_length()) * 180.0 / std::numbers::pi;
        return look(static_cast<float>(yaw), static_cast<float>(pitch));
    }

    inline Result<void> jump() {
        return detail::call_void(Op::Jump);
    }

    inline Result<void> respawn() {
        return detail::call_void(Op::Respawn);
    }

    inline Result<void> disconnect() {
        return detail::call_void(Op::Disconnect);
    }

    inline Result<void> drop(int count = 1) {
        abi::Writer w;
        w.put<int32_t>(count);
        return detail::call_void(Op::InventoryDrop, w);
    }

    inline Result<void> swap_slots(int a, int b) {
        abi::Writer w;
        w.put<int32_t>(a).put<int32_t>(b);
        return detail::call_void(Op::InventorySwap, w);
    }

    inline Result<void> click_slot(int slot, bool right = false, bool shift = false) {
        abi::Writer w;
        w.put<int32_t>(slot).boolean(right).boolean(shift);
        return detail::call_void(Op::InventoryClick, w);
    }

    inline Result<void> open_inventory() {
        return detail::call_void(Op::InventoryOpen);
    }

    inline Result<void> close_inventory() {
        return detail::call_void(Op::InventoryClose);
    }

    struct Sound {
        std::string name;
        Vec3 at;
        bool positional = false;
        float volume = 1.0f, pitch = 1.0f;
    };

    inline Result<void> play(const Sound& sound) {
        abi::Writer w;
        w.str(sound.name)
            .boolean(sound.positional)
            .put(sound.at.x)
            .put(sound.at.y)
            .put(sound.at.z)
            .put(sound.volume)
            .put(sound.pitch);
        return detail::call_void(Op::PlaySound, w);
    }

    using Inputs = abi::InputOverrideState;

    inline Result<void> drive(const Inputs& inputs) {
        abi::Writer w;
        w.write(inputs);
        return detail::call_void(Op::InputOverride, w);
    }

    inline Result<void> set_option(std::string_view key, std::string_view value) {
        abi::Writer w;
        w.str(key).str(value);
        return detail::call_void(Op::OptionSet, w);
    }

    template <game::Option O> Result<void> set_option(game::option_t<O> value) {
        using T = game::option_t<O>;
        if constexpr (std::is_same_v<T, bool>)
            return set_option(meta::enum_name(O), value ? "true" : "false");
        else if constexpr (std::is_same_v<T, std::string>)
            return set_option(meta::enum_name(O), value);
        else
            return set_option(meta::enum_name(O), std::format("{}", value));
    }

    inline Result<void> set_perspective(abi::Perspective mode) {
        abi::Writer w;
        w.write(mode);
        return detail::call_void(Op::PerspectiveSet, w);
    }

    inline Result<void> reload_resources() {
        return detail::call_void(Op::ReloadResources);
    }

    inline Result<void> open_screen(Screen screen) {
        abi::Writer w;
        w.write(screen);
        return detail::call_void(Op::OpenScreen, w);
    }

    inline Result<void> open_settings(std::string_view mod = {}) {
        abi::Writer w;
        w.str(mod);
        return detail::call_void(Op::OpenModSettings, w);
    }

    inline Result<void> screenshot() {
        return detail::call_void(Op::TakeScreenshot);
    }

    inline Result<void> cycle_perspective() {
        return detail::call_void(Op::CyclePerspective);
    }

    inline Result<void> toggle_hud() {
        return detail::call_void(Op::ToggleHud);
    }

    inline Result<void> copy_to_clipboard(std::string_view text) {
        abi::Writer w;
        w.str(text);
        return detail::call_void(Op::CopyToClipboard, w);
    }

    inline Result<void> open_url(std::string_view url) {
        abi::Writer w;
        w.str(url);
        return detail::call_void(Op::OpenUrl, w);
    }
}
