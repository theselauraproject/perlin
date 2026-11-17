#include <perlin/plugin.hpp>

#include <mod_settings.hpp>

#include <waila/api.hpp>
#include <waila/layout.hpp>
#include <waila/vanilla.hpp>

using namespace perlin;

static void install_addons(waila::Registry& registry) {
    (void)registry;
}

namespace {
    constexpr int64_t kRefreshMs = 50;
    constexpr double kFadeSeconds = 0.12;
    constexpr float kMargin = 4.0f;

    waila::Theme theme_for(settings::Theme theme) {
        switch (theme) {
        case settings::Theme::Waila:
            return waila::Theme::waila();
        case settings::Theme::Create:
            return waila::Theme::create();
        default:
            return waila::Theme::dark();
        }
    }

    struct Overlay {
        waila::Registry registry;
        bool ready = false;

        bool present = false;
        std::string signature;
        waila::Laid laid;
        std::optional<BlockPos> block;

        int64_t last_refresh = -1000, last_frame = 0;
        float alpha = 0.0f;

        void setup() {
            if (ready)
                return;
            ready = true;
            waila::vanilla::install(registry);
            install_addons(registry);
        }

        void clear() { present = false; }

        void refresh(int64_t now) {
            const auto player = game::player();
            if (!settings::display_tooltip || !player || !player->connected || player->spectator) {
                clear();
                return;
            }
            if (settings::show_when == settings::ShowWhen::WhileSneaking && !player->sneaking) {
                clear();
                return;
            }

            waila::Context base;
            base.player = *player;
            if (auto inventory = game::inventory())
                base.inventory = std::move(*inventory);
            base.details = game::input_down("key.keyboard.left.shift");
            base.now_ms = now;

            game::Target target =
                settings::show_blocks || settings::show_entities ? game::target() : game::Target{};
            if (std::holds_alternative<game::BlockTarget>(target) && !settings::show_blocks)
                target = {};
            if (std::holds_alternative<game::EntityTarget>(target) && !settings::show_entities)
                target = {};
            if (std::holds_alternative<std::monostate>(target) && settings::show_fluids &&
                settings::show_blocks)
                target = fluid_under_crosshair(*player);

            waila::Tooltip next;
            block.reset();
            if (const auto* b = std::get_if<game::BlockTarget>(&target)) {
                if (b->block.is_air()) {
                    clear();
                    return;
                }
                registry.run(next, waila::BlockContext(*b, base));
                block = b->pos;
            } else if (const auto* e = std::get_if<game::EntityTarget>(&target)) {
                registry.run(next, waila::EntityContext(*e, base));
            } else {
                clear();
                return;
            }
            if (next.hidden || next.empty()) {
                clear();
                return;
            }

            present = true;
            const std::string sig = waila::signature(next);
            if (sig != signature) {
                signature = sig;
                laid = waila::layout(next, theme_for(settings::theme), settings::show_icon);
            }
        }

        static game::Target fluid_under_crosshair(const PlayerState& player) {
            const auto hit = game::raycast(game::eye_position(player), game::look_direction(player),
                                           {.max_distance = 5.0, .include_liquids = true});
            if (!hit || !hit->hit || hit->block != "liquid")
                return {};
            game::BlockTarget t;
            t.pos = {hit->x, hit->y, hit->z};
            t.face = hit->face;
            t.distance = hit->distance;
            auto info = game::block(t.pos);
            if (!info || !info->loaded)
                return {};
            t.block = std::move(*info);
            if (auto p = game::block_props(t.block.name))
                t.props = std::move(*p);
            t.display_name = t.props.displayName.empty() ? game::display_name(NameKind::Block, t.block.name)
                                                         : t.props.displayName;
            return t;
        }

        void frame(ui::Canvas& g) {
            setup();
            const int64_t now = game::now_ms();
            const double dt = std::clamp(static_cast<double>(now - last_frame) / 1000.0, 0.0, 0.25);
            last_frame = now;
            if (now - last_refresh >= kRefreshMs) {
                last_refresh = now;
                refresh(now);
            }

            const float goal = present ? 1.0f : 0.0f;
            if (!settings::animation)
                alpha = goal;
            else if (alpha < goal)
                alpha = std::min(goal, alpha + static_cast<float>(dt / kFadeSeconds));
            else if (alpha > goal)
                alpha = std::max(goal, alpha - static_cast<float>(dt / kFadeSeconds));
            if (alpha <= 0.01f || laid.prims.empty())
                return;

            float breaking = 0.0f;
            if (present && block && settings::breaking_progress)
                if (const auto state = game::break_state(); state && state->active && state->x == block->x &&
                                                            state->y == block->y && state->z == block->z)
                    breaking = state->progress;

            const float scale = static_cast<float>(std::clamp(settings::scale.get(), 0.25, 4.0));
            const float w = laid.width * scale, h = laid.height * scale;
            const float x = std::round(static_cast<float>(settings::anchor_x.get()) *
                                       (static_cast<float>(g.width()) - w));
            const float y = std::round(kMargin + static_cast<float>(settings::anchor_y.get()) *
                                                     (static_cast<float>(g.height()) - h - 2.0f * kMargin));
            waila::paint(g, laid, x, y, scale, alpha, static_cast<float>(settings::opacity.get()), breaking);
        }
    };
}

struct Waila : Plugin {
    void onEnable(PluginCtx& ctx) override {
        ctx.hud<"waila">(this, &Waila::draw);
        ctx.bind<"toggle">("Toggle WAILA", Key::F8, this, &Waila::toggle);
        ctx.on_press(settings::reset_position, [] {
            (void)settings::anchor_x.set(0.5);
            (void)settings::anchor_y.set(0.0);
        });
        ctx.on_change(settings::theme, [this](settings::Theme) { overlay.signature.clear(); });
        ctx.on_change(settings::show_icon, [this](bool) { overlay.signature.clear(); });
    }

    void draw(ui::Canvas& g) { overlay.frame(g); }
    void toggle() { (void)settings::display_tooltip.set(!settings::display_tooltip); }

private:
    Overlay overlay;
};
PERLIN_REGISTER(Waila)
