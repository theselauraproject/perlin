#include <perlin/plugin.hpp>

#include <cmath>
#include <string>
#include <vector>

using namespace perlin;

// tour of perlin/hooks.hpp; Ctrl+K toggles the effects
struct HooksDemo : Plugin {
    bool on = true;
    int clicks = 0, chunks = 0, blocks = 0;

    void onEnable(PluginCtx& c) override {
        (void)hooks::bind({.id = "toggle", .label = "Toggle hooks demo", .key = "K", .ctrl = true});
        (void)hooks::add_button(
            {.screen = abi::Screen::Pause, .id = "hello", .label = "Hooks demo", .x = 4, .y = 4});
        (void)hooks::ping("play.cubecraft.net");
        c.every(20_ticks, [this] { apply(); });
        apply();
    }
    void onDisable(PluginCtx&) override {}

    void apply() {
        (void)hooks::hud_element(hooks::HudElement::Hunger, !on);
        (void)hooks::hud_element(hooks::HudElement::Hotbar, true, 0, on ? -6.0f : 0.0f);
        (void)hooks::debug_lines(
            {"hooks demo: " + std::string(on ? "on" : "off"), "chunks loaded " + std::to_string(chunks),
             "blocks changed " + std::to_string(blocks), "button clicks " + std::to_string(clicks)});
        (void)hooks::frame_limit(0, on ? 15 : 0);
    }

    void onKey(const abi::EventKey& e) {
        if (e.id == "toggle" && e.down) {
            on = !on;
            apply();
        }
    }

    void onTick() {
        if (!on)
            return;
        (void)hooks::fog({.color = 0xFF6040A0u, .start = 8, .end = 48, .ticks = 3});
        (void)hooks::entity_render({.type = "minecraft:zombie",
                                    .scaleX = 1.5f,
                                    .scaleY = 0.6f,
                                    .scaleZ = 1.5f,
                                    .tint = 0xFF80FF80u,
                                    .nameTag = 2,
                                    .nameText = "squashed",
                                    .ticks = 3});
        auto p = game::player();
        if (!p)
            return;
        const double x = std::floor(p->x) + 0.5, y = std::floor(p->y) + 0.02, z = std::floor(p->z) + 0.5;
        const uint32_t c = 0x80FFD700u;
        (void)hooks::world_mesh("ring",
                                {{x - 1, y, z - 1, 0, 0, c},
                                 {x + 1, y, z - 1, 0, 0, c},
                                 {x + 1, y, z + 1, 0, 0, c},
                                 {x - 1, y, z - 1, 0, 0, c},
                                 {x + 1, y, z + 1, 0, 0, c},
                                 {x - 1, y, z + 1, 0, 0, c}},
                                0, hooks::RenderStage::AfterEntities, true, 3);
    }

    void onTooltip(const abi::EventTooltip& t) { (void)hooks::tooltip({"id: " + t.item.name}); }
    void onScreenButton(const abi::EventScreenButton&) {
        clicks++;
        (void)act::toast("hooks demo button");
        apply();
    }
    void onChunkLoaded() { chunks++; }
    void onBlockChanged(const abi::EventBlockChanged& e) {
        blocks++;
        if (e.to == "minecraft:air")
            (void)hooks::particle("minecraft:basic_flame_particle", e.x + 0.5, e.y + 0.5, e.z + 0.5, 6, 0.3f);
    }
    Flow onInteract(const abi::EventInteract& e) {
        // example of cancelling: refuse breaks below y -60
        return e.kind == abi::InteractKind::StartBreak && e.y < -60 ? Flow::cancel : Flow::pass;
    }
    void onServerPing(const abi::EventServerPing& e) {
        ctx().print("ping {}: {} players, {} ms", e.ok ? "ok" : "failed", e.players,
                    static_cast<int>(e.latencyMs));
    }
    void onModsLoaded() { ctx().print("all mods loaded"); }
    void onLevelLoaded(const abi::EventLevel& e) { ctx().print("level loaded, dimension {}", e.dimension); }
};
PERLIN_REGISTER(HooksDemo)
