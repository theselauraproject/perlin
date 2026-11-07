# WAILA

What Am I Looking At: a tooltip for the block or entity under your crosshair, styled after Jade.

* Block: icon, name, the tool that mines it (with a check mark when the item in your hand does, cycling through the tiers that would), breaking progress along the bottom edge, crop growth, redstone power and repeater delay, honey, composter and cauldron levels, note block pitch, spawner mob, jukebox record, sign text, waxed state, container contents, and where it comes from in italic blue.
* Entity: name, health as hearts (or a number for bosses), armor, owner, baby/sheared/saddled/charged and other states, horse speed and jump height, dropped items, held items.
* Appearance: three frames (Dark, Waila, Create) taken from Jade's own tooltip sprites, scale, opacity, position, fade in and out.

Install: put `waila.wasm`, `waila.json` and `waila.png` in `<data dir>/mods/`, then press **Allow & Enable** in **Pause → Mods**. WAILA asks for no permissions. Open its settings from the Mods screen; F8 toggles it.

Build: `mods/build.sh --wasm32 waila/waila.cpp` (wasi-sdk), or `mods/build.sh waila/waila.cpp` (Emscripten, 64-bit).

## How it works

```
waila.cpp            asks the game what the crosshair is on, runs the providers, lays the tooltip out, paints it every frame
waila/api.hpp        the extension API: elements, Tooltip, contexts, Registry
waila/layout.hpp     Tooltip -> drawing primitives (measured once per change), and the painter
waila/vanilla.hpp    everything WAILA shows, written with the same API an addon uses
waila.json           manifest: the settings page (a toggle per feature), generates `settings::...`
tests/               waila_test (providers and layout), preview + render_preview.py (draws a scenario to a PNG)
```

## Adding your own information

A provider looks at what is targeted and adds rows to the tooltip.

```cpp
// my_addon.hpp
#include <waila/api.hpp>
#include <mod_settings.hpp>

namespace my_addon {
    inline void install(waila::Registry& r) {
        // Stage: Head (the name), Body, or Tail (where it comes from).
        // Gate: a toggle from waila.json, or `waila::always`.
        // Pattern: which blocks or entity types. `*` matches any run of characters.
        r.block(waila::Stage::Body, settings::my_feature, [](waila::Tooltip& tip, const waila::BlockContext& c) {
            if (c.block.block.state_int("age") == 3)
                tip.line("Ready to harvest", Rgba::green);
        }, "minecraft:nether_wart");

        r.entity(waila::Stage::Body, waila::always, [](waila::Tooltip& tip, const waila::EntityContext& c) {
            tip.row().text("Variant ").text(std::to_string(c.entity.variant), Rgba::gold);
        }, "minecraft:*");
    }
}
```

Add `my_addon::install(registry);` to `install_addons` in `waila.cpp` and a toggle to `waila.json`:

```json
{"id": "my_feature", "type": "toggle", "label": "My feature", "default": true}
```

The build turns that into `settings::my_feature`, which the provider takes as its gate. Providers in one stage run in priority order (lower first, default 0).

### What a provider can do

| | |
| --- | --- |
| `tip.line("text", color)` | a row of text; `§` codes work (`"§9§oMinecraft"`) |
| `tip.row()` | a row you fill: `.text`, `.item(id, size, count)`, `.texture(path, w, h)`, `.bar(fraction, color, label)`, `.hearts(health, max)`, `.armor(points)`, `.mark(ok)`, `.gap(px)`; `.right_aligned()` pins what follows to the right edge |
| `tip.set_title(name)`, `tip.title()` | the name, and the first row, to put things next to it |
| `tip.set_icon({id})`, `tip.clear_icon()` | the picture at the left |
| `tip.hide()` | show nothing for this target |

Everything the game knows is in the context: `c.block.block` (name, states), `c.block.props` (hardness, tool, harvest level), `c.block.entity()` (block entity data, with the whole tag as JSON in `nbt`; read it with `json::parse`), `c.entity` (flags, health, every attribute and property, owner, held items), `c.player`, `c.inventory`, `c.held()`, `c.details` (Shift held), and every `perlin::game::` call.

## Previewing without the game

```
g++ -std=c++23 -Isdk -Iwaila -I<gen> -Iwaila/tests -DPREVIEW_MANIFEST='"waila/waila.json"' waila/tests/preview.cpp -o preview
./preview stone stone.txt            # scenarios: stone wheat cow horse chest bedrock noteblock
python3 waila/tests/render_preview.py stone.txt stone.png --jar <extracted Minecraft jar>
```

`<gen>` is the folder with `mod_settings.hpp` from `tools/perlin_settings.py waila/waila.json`. The preview runs WAILA against a fake game and draws its output with the Java client's font and textures; block icons are approximated.
