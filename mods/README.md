# Perlin plugins

Plugins are WebAssembly modules written in modern C++ (C++23). They run inside a sandbox in the client, can see almost everything the game knows, and can draw on the HUD, add commands and key binds, and react to game events.

Drop `name.wasm` (and optionally `name.json` and `name.png`) into `<data dir>/mods/`. Plugins hot-reload when the file changes (Options → Hot Reload On Save) and can be enabled, disabled and reloaded from **Pause → Mods**.

## Quick start

A plugin is one class. Override what you need; every `onXxx` method named after an event is subscribed for you.

```cpp
#include <perlin/perlin.hpp>

using namespace perlin;

class Hello : public Plugin {
public:
    void registerCommands(Context& ctx) override {
        // The parameters are the command's arguments: `/here`, `/tp 10 64 -3`, `/mode creative`.
        ctx.command<"here">("prints your block position", [] {
            if (auto p = game::player())
                (void)act::message("you are at {}", BlockPos(game::position(*p)));
        });
    }

    void onEnable(Context& ctx) override {
        ctx.hud<"clock">(this, &Hello::drawClock);
    }

    void onChatReceived(const abi::EventChat& e) {
        if (e.message.contains("creeper"))
            (void)act::toast("someone said creeper");
    }

private:
    void drawClock(ui::Canvas& g) { g.text(4, 4, "hello from a plugin", {.color = Rgba::gold}); }
};
PERLIN_REGISTER(Hello)
```

Plugins are 64-bit WebAssembly (memory64) by default. Build one with Emscripten (<https://emscripten.org>, activate emsdk first):

```
source /path/to/emsdk/emsdk_env.sh
mods/build.sh my_plugin.cpp
```

`build.sh` compiles with `-std=c++23 -fno-exceptions -fno-rtti -sMEMORY64` as a standalone WASI reactor (no JavaScript) and writes `prebuilt/my_plugin.wasm` (the second argument picks another output directory).

Classic 32-bit plugins still work everywhere. `mods/build.sh --wasm32 my_plugin.cpp` builds one with a wasi-sdk install (<https://github.com/WebAssembly/wasi-sdk>, version 25 or newer, `WASI_SDK_PATH`); wasi-sdk has no 64-bit target. Any compiler that targets `wasm64` or `wasm32-wasip1` and exports `perlin_alloc`, `perlin_free`, `perlin_event` and `perlin_subscriptions` works; the headers in `mods/sdk/perlin` provide those exports and compile unchanged for both.

Perlin runs plugins on [WasmEdge](https://wasmedge.org) (desktop and Android) and on wasm3 in the browser and iOS builds; both take 32-bit and 64-bit modules, and the Mods screen's log line says which one loaded (`waila: wasm64 on WasmEdge 0.18.0`).

## Modern C++ API

The SDK leans on the compiler instead of strings:

* **Compile-time reflection.** Every wire struct is an aggregate; its encoding is derived from the member list (`perlin/meta.hpp`: enumerator names, aggregate arity and field visitation, `fixed_string`). With `-DPERLIN_USE_STD_META` on a compiler that implements P2996 it uses real `std::meta`; otherwise a portable fallback gives the same results on GCC 13+ and Clang 18+.
* **Typed events.** `ctx.on<event::Tick>(...)` and `ctx.on<abi::Event::Tick>(...)` hand the handler the payload type from the ABI table (`abi::payload_t<E>`). Handlers may also take no argument and may return `Flow`.
* **Typed commands.** `ctx.command<"tp">("teleport", [](int x, int y, int z) {...})`. Parameter types are the grammar: text, integers, floats, bool, enumerations (matched by name, case-insensitive) and `std::optional<T>` for optional trailing arguments. The usage line (`/tp <integer> <integer> <integer>`, `/mode [Fast|Slow]`) is generated and shown on a bad call. `(const CommandCall&)` gives raw access.
* **Typed options and controls.** `game::option<game::Option::fov>()` returns `Result<double>`; `act::set_option<game::Option::vsync>(true)` is checked at compile time. `game::input_down(game::Control::Jump)`.
* **`Result<T>`** is `std::expected<T, Error>` (a small stand-in is used on standard libraries that lack it); `Error::message()` and `Error::name()` come from the `Status` enumerators.
* **Chrono timers.** `ctx.after(2s, fn)`, `ctx.every(20_ticks, fn)` return a `TimerId`.
* **Flags and sets.** `Permissions` is `meta::Flags<Permission>`; entity flags are a `meta::IndexSet<EntityFlag>`. `std::format("{}", abi::Screen::Mods)` prints `Mods`.

## Plugins are classes

`perlin::Plugin` is the only entry point (the old `PERLIN_PLUGIN(ctx)` function macro is gone). The manifest (name, description, permissions, settings) stays in `name.json`; the class is only the code.

```cpp
#include <perlin/perlin.hpp>
#include <mod_settings.hpp>

struct Hello : perlin::Plugin {
    void registerCommands(perlin::Context& ctx) override {   // called first; parameter types are the command grammar
        ctx.command<"greet">("Say hello", this, &Hello::greet);
    }
    void onEnable(perlin::Context& ctx) override {
        ctx.bind<"wave">("Wave", perlin::Key::R, this, &Hello::wave);
        ctx.on_change(settings::shout, [this] { /* ... */ });
    }
    void onDisable(perlin::Context&) override {}
    void onChatReceived(const perlin::abi::EventChat& chat);   // any onXxx for an event is subscribed automatically
    void greet(std::string who, std::optional<int> times);
    void wave();
};
PERLIN_REGISTER(Hello)
```

`Context` (also spelled `PluginCtx`) has `on`, `after`, `every`, `command`, `bind`, `hud`, `on_change`, `on_press`, `config`, `print` and `raw()`; `command`, `bind` and `hud` also take an object and a member function, and `ctx()` reaches it from any method. The event methods are generated from the ABI table, one `onName` per event (`onTick`, `onFrame`, `onChatReceived`, ..., and on the server `onPlayerJoin`, `onPlayerQuit`, `onPlayerChat`, `onBlockBreak`, `onBlockPlace`); each takes the payload or nothing, and may return `Flow` to cancel. `mods/waila`, `mods/examples/barrel_roll` and `mods/tests/sdk_test.cpp` are complete examples.

## One ABI for client and server (`perlin/abi_table.h`)

Every operation and event, for both the client and the server, is one line in `mods/sdk/perlin/abi_table.h`:

```c
X(Teleport, 0x7004, ServerTeleport, Nothing, None, Server)   // name, id, request, response, permission, hosts
X(PlayerChat, 49, EventPlayerChat, Server, Method)           // name, id, payload, hosts, wiring
```

From that line the SDK gets `abi::Op::Teleport` / `abi::Event::PlayerChat`, the C names `PERLIN_OP_TELEPORT` / `PERLIN_EVENT_PLAYER_CHAT` in `perlin.h`, the payload and response types (`abi::OpSpec<Op>`, `abi::payload_t<E>`), the permission the host checks, the `onPlayerChat` method on `Plugin`, and the subscription bit. The hosts read the same table: the client's `ModManager` and the server's `PluginHost` both register typed handlers with `Handle<Op>(fn)` and deliver events with `Dispatch<Event>(payload)` (`perlin/host.hpp`), so a request is decoded and a response encoded in exactly one way everywhere. Adding an operation is: one table line, the request/response structs in `abi.hpp` if they are new, and one `Handle<Op>` in the host that serves it.

Calling an operation directly is `rpc::call<abi::Op::BlockName>(abi::BlockCoord{x, y, z})`, which returns `Result<abi::TextMessage>`. Server plugins have friendlier wrappers in `perlin/server.hpp`:

```cpp
using namespace perlin;

struct Greeter : Plugin {
    void onPlayerJoin(const abi::EventPlayer& p) { (void)server::broadcast("welcome, {}!", p.name); }
    Flow onPlayerChat(const abi::EventPlayerChat& c) {
        return c.message.contains("creeper") ? Flow::cancel : Flow::pass;
    }
};
PERLIN_REGISTER(Greeter)
```

`server::broadcast`, `message`, `kick` (all `std::format`-style), `players()`, `teleport`, `set_block`, `block`, `info` and `give` are available; the same `.wasm` can serve both hosts, each calling only what its host provides (`abi::HostsOf(op)`).

## Bringing your own renderer (`perlin/gfx.hpp`, `perlin/input.hpp`)

Enough is exposed to hook up Dear ImGui or any other immediate-mode library:

* `gfx::Texture::create(w, h, rgba)` / `update` for atlases, `gfx::display()` for the framebuffer size and mouse.
* `gfx::load_image("textures/map/map_icons")` returns the pixels of a resource-pack image (the active pack, then vanilla), to copy into an atlas.
* `gfx::submit(layer, z, vertices, indices, commands)` takes ImGui's own arrays: 20-byte vertices (`ImDrawVert`), 16- or 32-bit indices, and commands with a clip rectangle, a texture id and index/vertex offsets. It is drawn every frame with straight alpha until replaced: layers with `z` below `abi::kGfxScreenZ` (100) draw with the HUD, under the pause screen and other menus, and higher layers draw on top of everything. `gfx::DrawList` is a small builder for hand-made lists.
* `input::grab(mouse, keyboard)` (needs `control`) delivers `MouseMove`, `MouseButton`, `MouseWheel`, `KeyRaw` and `TextInput` events and frees the cursor. Pressing Escape three times quickly always releases every grab.
* `gfx::native_handles()` (needs the **`native`** permission) returns the graphics API, adapter, window, display and device handles as plain integers. A WebAssembly module cannot dereference host pointers, so these are for identification and for a future native bridge; rendering goes through `gfx::submit`.

`examples/sketchpad` is a complete small example. Not tested inside the game yet.

## Everything is exposed

The wire format is derived from the member list of each struct (`abi.hpp`) and the operation list from `abi_table.h`, so adding a field to a struct the host already sends, such as an entity's attributes, exposes it to every plugin and every language with no wrapper to write. `tools/check_api.py` (run by CTest as `mod_api_check`) fails when an operation is declared without a host or without an SDK wrapper, or an event is never delivered. Every enumeration in `perlin::abi` prints by name with `std::format` without being listed anywhere. `perlin/json.hpp` is a small JSON reader for the text the game hands over (block entity tags, option dumps, HTTP bodies): `json::parse(text)` then `tag["Items"][0]["Name"].string()`; missing keys read as null, so chains never fail.

## Client hooks (`perlin/hooks.hpp`)

These reach into the client's own rendering, screens and systems.

| Area | Calls | Events |
| --- | --- | --- |
| HUD | `hooks::hud_element(HudElement, visible, dx, dy)` hides or moves one vanilla element (hearts, armor, hunger, air, hotbar, XP bar, F3, chat, effects, locator bar, fire, subtitles, crosshair); `hooks::debug_lines(left, right)` adds F3 lines | |
| Camera and world look | `hooks::fog({...})` overrides fog distance and colour, or turns it off; `hooks::screen_tint(argb, vignette)` tints the world and darkens the edges | |
| Entities | `hooks::entity_render({...})` hides, scales (per axis), tints, retextures or renames one entity (runtime id) or a whole type; `hooks::skin(player, png, slim)` swaps a player's skin on your screen | |
| World geometry | `hooks::world_mesh(id, triangles, texture, stage, depth_test)` draws textured or coloured triangles in the world (`texture` is a `gfx::Texture` id); `hooks::clear_world_mesh(id)` | |
| Screens | `hooks::add_button({screen, id, label, x, y, w, h})` puts a button on a vanilla screen; `hooks::open_custom_screen(title)` opens a blank screen of your own (closes on Escape) | `ScreenRender` (every frame while a screen is open, `Screen::Custom` for yours), `ScreenButton`, `SlotClick` (cancellable) |
| Tooltips | `hooks::tooltip(lines)` inside a `Tooltip` handler | `Tooltip` |
| Window and frame rate | `hooks::frame_limit(fps, unfocused_fps)`, `hooks::window()` | `WindowFocus` |
| Loading screen | `hooks::loading_overlay(hidden)` hides the connecting/loading screen | |
| Audio | `hooks::ambient(id, sound, volume, pitch, play)` keeps a looping sound going | |
| Network | `hooks::ping(host, port)` pings a server (needs `network`) | `ServerPing` |
| Mods | `hooks::send(to, channel, body)` messages another mod (or all of them); a manifest `"priority"` (higher runs first) orders event handlers; `game::mods()` lists version, author, website, hooks, permissions and priority | `ModMessage`, `ModsLoaded`, `ClientStopping` |
| Packs | `hooks::register_pack(path)` stacks a pack shipped in the mod's folder under the player's packs (needs `files`); `hooks::shader_pack(name)` switches shader pack (needs `control`) | `ResourcesReloaded` |
| Particles | `hooks::particle(name, x, y, z, count, spread)` | |
| Input | `hooks::bind({id, label, key, ctrl, shift, alt, context})` binds with modifiers, in game, on screens or both | `Key` |
| Your own actions | | `Interact` (start breaking, attack, interact with an entity, use on a block, use an item; cancel to stop it) |
| World | | `ChunkLoaded`, `ChunkUnloaded`, `BlockChanged`, `LevelLoaded`, `LevelUnloaded`, `Toast` (cancellable) |

HUD layers whose name starts with `screen:` (`ctx.hud("screen:panel", ...)`) are drawn while a screen is open instead of in game, so a `ScreenRender` handler can draw onto menus and inventories.

Everything a mod sets with these calls is undone when it unloads.

Events 32 and up are announced through a second export, `perlin_subscriptions_hi() -> i32`, holding bits for event ids 32 to 63; the SDK provides it.

## Settings

Settings are written in the mod's JSON, not in code. The game shows them on the mod's **Settings** page (Mods screen, pick the mod, **Settings**), stores them in `<data dir>/mods/<name>.settings.json`, and keeps the mod up to date while it runs.

```json
"settings": [
  {"type": "header", "label": "Readout"},
  {"id": "visible", "type": "toggle", "label": "Show overlay", "default": true, "icon": {"item": "minecraft:clock"}},
  {"id": "unit", "type": "choice", "label": "Unit", "default": "BlocksPerSecond",
   "choices": [{"value": "BlocksPerSecond", "label": "Blocks per second"}, {"value": "KilometersPerHour", "label": "Kilometers per hour"}]},
  {"id": "warn_at", "type": "number", "label": "Highlight above", "default": 8, "min": 1, "max": 40, "step": 0.5, "unit": "b/s"},
  {"id": "smoothing", "type": "integer", "label": "Smoothing", "default": 4, "min": 1, "max": 20},
  {"id": "reset_peak", "type": "button", "label": "Reset peak"}
]
```

| `type` | C++ handle | Value |
| --- | --- | --- |
| `toggle` | `Toggle` | `bool` |
| `integer` | `Integer` | `int` (`min`, `max`, `step`) |
| `number` | `Number` | `double` (`min`, `max`, `step`, `unit`) |
| `choice` | `Choice<E>` | an enumeration generated from `choices` |
| `button` | `Button` | no value; handle presses with `ctx.on_press` |
| `header`, `label` | | layout only |

Every setting also takes `label`, `description`, `restart` (needs a restart to apply) and `icon`: `{"item": "minecraft:clock"}` draws an item, `{"texture": "textures/items/apple"}` a resource-pack texture (so a pack can restyle it), `{"file": "icons/speed.png"}` a PNG from the mod's folder. Choice values are C++ identifiers; they become the enumerators.

`build.sh` reads `name.json` next to `name.cpp` (headers next to the source are found with `#include <dir/header.hpp>`) and generates `mod_settings.hpp` with one typed handle per setting. Use it like a plain value:

```cpp
#include <mod_settings.hpp>

if (settings::visible) { ... }                                   // bool
const double limit = settings::warn_at;                          // double
if (settings::unit == settings::Unit::KilometersPerHour) { ... }  // generated enum
(void)settings::visible.set(false);                              // the settings page shows it too

ctx.on_change(settings::unit, [](settings::Unit unit) { ... });  // fires for changes from the page or from set()
ctx.on_press(settings::reset_peak, [] { ... });
```

Without the generated header (another build system, another language) the same values are `perlin::setting<double>("warn_at")`. The generator is `mods/tools/perlin_settings.py`.

## Game actions

Mods call the game directly with typed functions and enums; there are no messages to compose.

```cpp
act::open_screen(Screen::Mods);        // Pause, Options, Mods, Wardrobe
act::open_settings();                  // this mod's settings page (or open_settings("other.wasm"))
act::screenshot();
act::cycle_perspective();
act::toggle_hud();
act::copy_to_clipboard("hello");
act::open_url("https://example.com");  // needs `network`

fs::copy(fs::in(Area::Screenshots, "a.png"), fs::in(Area::Mod, "keep/a.png"));   // needs `files`
fs::move(fs::in(Area::Mod, "pack.zip"), fs::in(Area::ResourcePacks, "pack.zip"));
net::download("https://example.com/pack.zip", fs::in(Area::ResourcePacks, "pack.zip"), [](Result<void> r) { ... });  // `network` and `files`
```

`Area` is `Mod`, `Screenshots`, `ResourcePacks` or `ShaderPacks`. Copies and moves never overwrite a game file.

## WAILA

`mods/barrel_roll` is a port of the Do a Barrel Roll mod: free elytra flight with mouse and keys, banking, thrust, smoothing and a full settings page. It uses the `look` event (raw mouse and key input every frame) and `game::set_look` to steer the camera, and needs the `control` permission. The prebuilt module is `mods/prebuilt/barrel_roll.wasm` with `barrel_roll.json` and `barrel_roll.png`.

`mods/minimap` is a minimap: relief-shaded terrain (slopes lit from where the sun or moon is, so shadows move through the day; higher ground lighter), water darkened by depth, a cave view when you are underground with its own zoom multiplier, stepped zoom (0.5 to 6) that eases between steps, day and night, an optional chunk grid with your chunk outlined, the vanilla map player marker, waypoints (B adds one, your death point is marked) and rotate-with-player. The whole map is one texture and everything on it, grid and markers included, is one indexed draw call; columns come from the host's `game::map_chunk` (surface colour, height and water depth per column, only re-read when a chunk changes). No entities are shown. Build it with `./build.sh --wasm32 minimap/minimap.cpp`; the prebuilt module is `mods/prebuilt/minimap.wasm` with `minimap.json` and `minimap.png`.

`mods/waila` is a full plugin built on this API: a tooltip for the block or entity under your crosshair, styled after Jade (icon, name, mod name in italic blue, harvest tool with a check mark, breaking progress, crop growth, redstone, health hearts, pet owner, horse stats, container contents). Its source is the best example of the API, and it has an extension API of its own; see `mods/waila/README.md`. The prebuilt module is `mods/prebuilt/waila.wasm` with `waila.json` and `waila.png`; drop the three into `<data dir>/mods/`.

## Any language

The C++ SDK is a convenience. A plugin is any WebAssembly module (core wasm, no component model) that targets a WASI preview 1 or freestanding environment, so Rust (`wasm32-wasip1`), C, Zig, Go (TinyGo), AssemblyScript, Swift, Nim and anything else that emits wasm work. The contract is small:

* Import one function: module `perlin`, name `call`, signature `(op: i32, in: ptr, in_len: i32, out: ptr, out_cap: i32) -> i32`.
* Export `perlin_alloc(size: i32) -> ptr`, `perlin_free(ptr)`, `perlin_event(id: i32, ptr, len: i32) -> i32` and `perlin_subscriptions() -> i32`.
* `ptr` is `i32` in a 32-bit module and `i64` in a 64-bit (memory64) one: exactly what `void*` becomes when you compile for `wasm32` or `wasm64`. The host reads the module's declared types, so it also accepts `i64` lengths (`size_t`) in a 64-bit plugin.
* Linear memory must be the module's own (not imported) and exported; 1 GiB at most.
* `perlin_subscriptions` returns a bitmask with bit `1 << event_id` set for every event you want delivered (ids 0 to 31; export `perlin_subscriptions_hi` for ids 32 to 63, bit `1 << (id - 32)`) (see `Event` in `mods/sdk/perlin/abi.hpp`; id 1 is Load, 7 is Connected, 3 is Tick, 4 is Frame). `perlin_event` returns bit 0 set to cancel a cancellable event.
* The host calls `perlin_event(Load = 1, ...)` once after instantiation; register everything (commands, keys, HUD) from there.
* Payloads and arguments are little-endian packed values: integers and floats at their natural width, strings and byte blobs as a `u32` length followed by the bytes, booleans as one byte.
* `call` returns the number of bytes written to `out` (>= 0), a negative `Status` code (`-1` unknown op, `-2` denied, `-3` bad arguments, `-4` unavailable, `-5` not found, `-6` failed), or a value below `-16` meaning the buffer was too small: call again with `out_cap` of `-rc - 16`.
* Operation ids are the `Op` enum in `abi.hpp`; each C++ wrapper in `game.hpp`, `act.hpp`, `ui.hpp` and `io.hpp` shows the exact argument layout for that op.

`mods/examples/c/hello.c` is a complete plugin in plain C (about 30 lines, no SDK) that prints a message when you join a server; build it with `mods/examples/c/build.sh` (`--wasm32` for wasi-sdk).

## Manifest

`name.json` next to the `.wasm` is optional (it also holds the `settings`, see above):

```json
{
  "name": "Speedometer",
  "description": "Shows your speed.",
  "version": "1.0.0",
  "author": "You",
  "website": "https://example.com",
  "permissions": ["network", "files", "packets", "control", "native"]
}
```

`name.png` is the icon in the Mods screen.

## Permissions

The API exposes everything the game knows and can do. Only the dangerous operations are gated: those that act on your behalf or leave your machine have to be declared in the manifest and are shown in the Mods screen. Everything else (reading state, drawing, local messages, settings, storage in the plugin's own folder) is always allowed.

| Permission | Allows |
| --- | --- |
| *(always)* | player, inventory, world, blocks, entities, player list, options (read), screen and input state, translations, HUD drawing, local messages, toasts, titles, sounds, commands, key binds, storage |
| `control` | sending chat and commands, hotbar and inventory actions, swinging, using items, attacking, interacting, breaking blocks, looking, driving movement inputs, jumping, respawning, leaving the game, changing options |
| `network` | HTTPS requests, `act::open_url`, downloads |
| `files` | `fs::copy`, `fs::move` and (with `network`) `net::download` between the mod's own folder and the game's screenshot, resource-pack and shader-pack folders; never overwrites a game file |
| `packets` | sending raw protocol packets (any id), dropping incoming or outgoing packets by id, injecting packets as if the server had sent them |
| `native` | `gfx::native_handles()`: the window and graphics-device handles |
| `storage` | always granted; reads and writes under `<data dir>/mods/<name>.data/` only |

An operation without its permission returns `Error::denied()` instead of running.

**Approval.** A mod that asks for `network`, `files`, `control`, `packets` or `native` is never enabled just because it was dropped into the folder. It shows as *Needs approval* in the Mods screen, with a plain-language list of what it asks to do, and starts only after you press **Allow & Enable**. The approval covers exactly the permissions it asked for; a later update that asks for more switches it off again until you approve it. Mods that only use the always-allowed operations start right away.

## Sandbox

* Plugins have no file, socket or process access of their own; of WASI only stdout/stderr (to the log), clocks and random numbers work. Everything goes through the host calls in `perlin/*.hpp`.
* Storage paths are relative to the plugin's own folder; `..`, absolute paths and backslashes are rejected.
* HTTP is HTTPS only, never follows redirects, blocks loopback, private and link-local addresses, caps responses at 4 MiB and allows six requests at a time.
* The account file (`auth.json`) and its tokens are never reachable. `game::options_json()` lists only a fixed set of settings, and `playerName`, `server`, `texturePack` and `shaderPack` are read-only.
* Each plugin runs on its own thread, one event at a time. An event that takes longer than 250 ms (4 s for loading) revokes the plugin: its host calls stop working, its commands, keys and HUD layers are removed, and the Mods screen shows it as unresponsive until you reload it. A plugin stuck in a loop is interrupted on the spot (WasmEdge); in the browser build the interpreter can't be interrupted, so it keeps one thread busy until the page closes.

## API tour

All calls return `Result<T>` (`std::expected<T, Error>`); `Error::status` is one of `UnknownOp`, `Denied`, `BadArguments`, `Unavailable` (for example no server connection), `NotFound` or `Failed`.

### `game::` (read state)

| Call | Returns |
| --- | --- |
| `player()` | position, velocity, look, flags, health, hunger, xp, air, permission level, game mode, dimension |
| `effects()`, `inventory()` | status effects, 36 slots + armor + offhand + cursor |
| `world()` | dimension, difficulty, game rules, time of day, day count, weather, spawn |
| `target()` | what the crosshair is on, fully resolved: a `BlockTarget` (position, face, block with states, `props`, display name, `entity()`) or an `EntityTarget` (all entity data, display name) or nothing |
| `block(pos)` | name, block states (`state_int("age")`, `state_bool(...)`), biome, liquid, loaded flag |
| `block_props(name)` | hardness, mining tool, harvest level, display name, block properties |
| `block_entity(pos)` | sign text, banner patterns, waxed, and the whole tag as JSON in `nbt` (read it with `json::parse`) |
| `display_name(NameKind::Item\|Block\|Entity, id)` | the translated, resource-pack-aware name: "Stone", "Cow" |
| `break_state()` | the block the local player is mining and how far along it is |
| `look()` | camera yaw and pitch |
| `region(a, b)` | up to 262 144 blocks as a palette + cells (`Region::at(x, y, z)`) |
| `raycast(origin, dir, {max_distance, include_liquids})`, `aim()` | block hit; what the crosshair targets (block or entity) |
| `find_blocks(center, {name, radius, max_results, exact})` | nearest matches first, radius up to 32 |
| `chunk_loaded(cx, cz)` | whether the chunk is in memory |
| `entities({radius, type, players_only, include_items})`, `entity(id)`, `players()` | nearby entities, one entity, the tab list. An `Entity` carries everything the game tracks: flags, health and max health, every attribute, entity properties, variant, owner, target, vehicle, held items, armor |
| `screen()` | GUI size and scale, fps, which screen is open, platform, server, language |
| `options_json()`, `option<Option::fov>()`, `option_text(key)` | settings |
| `perspective()`, `chat_history(n)`, `title_state()`, `resource_packs()`, `mods()`, `block_names()` | camera mode, recent chat lines, current title/subtitle/action bar, active packs, installed mods, every block name |
| `translate(key)` | localized string |
| `server_commands()`, `input_actions()`, `input_down(action)`, `mouse()` | server command names, action ids, held state of an action or `key.keyboard.x`, cursor in GUI pixels |

### `act::` (do things)

Free: `message`, `toast`, `title`, `play` (sound). Needs `control`: `say`, `run_command`, `select_slot`, `swing`, `use_item`, `use_on`, `attack`, `interact`, `break_block`, `look`, `look_at`, `jump`, `respawn`, `disconnect`, `drop`, `swap_slots`, `click_slot`, `open_inventory`, `close_inventory`, `drive(Inputs)`, `set_option`, `set_perspective`, `reload_resources`.

Free game actions: `open_screen`, `open_settings`, `screenshot`, `cycle_perspective`, `toggle_hud`, `copy_to_clipboard`. Needs `network`: `open_url`.

`drive` holds movement inputs for a number of ticks (`forward > 0` walks forward, `strafe > 0` goes right); call it every tick to keep moving. `look` takes the same yaw and pitch that `PlayerState` reports (yaw 0 faces south, pitch positive looks down). `break_block` breaks instantly in creative; in survival it starts mining, so keep the crosshair on the block and `drive` with `attack = true` until it breaks.

### `ui::` (HUD)

`ctx.hud("name", [](ui::Canvas& g) { ... }, z)` redraws every frame and only sends the layer to the game when the drawing changed. `Canvas` has `rect`, `gradient`, `border`, `line`, `text` (with `TextStyle{.color, .scale, .shadow, .align}`; `§` formatting codes work: `"§9§oMinecraft"` is italic blue), `item` (an item icon by id) and `image` (a resource-pack texture path). Coordinates are GUI pixels (the same units as the vanilla HUD); `g.width()` and `g.height()` are the current GUI size. `ui::text_width(text, scale)` measures with the game's font. Layers are hidden when the HUD is hidden or a menu is open.

### `io::` (`fs`, `Config`, `net`)

`fs::read_text/read_bytes/write_text/write_bytes/list/exists/remove/make_directory` work inside the plugin's data folder. `Config("settings.cfg")` is a small `key=value` store with typed `get`/`set` and `save()`. `net::get/post/send` take a callback that runs on a later frame with the `Response`. `net::send_packet(id, bytes)` and `net::packet_name(id)` are the raw packet escape hatch.

### Events

Register with `ctx.on<event::Name>([](const event::Name::Payload& e) { ... })`. Handlers returning `Flow::cancel` cancel cancellable events.

| Event | Payload | Cancellable |
| --- | --- | --- |
| `Tick` | `dt`, `tick` (20 per second) | |
| `Frame` | `dt`, `fps`, GUI size | |
| `ChatReceived` | `message` | yes (hides the line) |
| `ChatSending` | `text` | yes (nothing is sent) |
| `Command` | handled for you by `ctx.command` | |
| `Key` | handled for you by `ctx.bind` | |
| `Connected`, `Disconnected` | `server`, `message`, `reason` | |
| `Damage` | direction, health | |
| `Death`, `Respawned` | cause | |
| `DimensionChanged`, `ScreenChanged` | `from`, `to` | |
| `SlotChanged` | `slot`, `item` | |
| `EntityAdded`, `EntityRemoved` | entity (removed events only fill the id) | |
| `Sound` | name, position, volume, pitch | |
| `Title` | title, subtitle, action bar | |
| `Packet` | id and body of every packet received from the server | |
| `HttpResponse` | handled for you by `net::send` | |
| `SettingChanged` | handled for you by `ctx.on_change` / `ctx.on_press` | |

`ctx.after(delay, fn)` and `ctx.every(period, fn)` take any `std::chrono` duration (`20_ticks` too), run on the tick clock and return a `TimerId` for `ctx.cancel(id)`. `ctx.on_unload(fn)` runs before a reload.

### Commands and keys

`ctx.command<"name">("description", fn)` adds a client-side slash command that appears in chat completion (see *Typed commands*); the string form `ctx.command("name", "/name <arg>", "description", fn)` still works. `ctx.bind<"id">("Label", Key::G, fn)` (or `ctx.bind("id", "Label", Key::G, fn)`) registers a key that is active while no menu or chat is open. Players rebind plugin keys in **Options → Controls → Key Binds** (a section per plugin); the choice is stored in `<data dir>/mods/<name>.keys.json` (`{"id": "key.keyboard.g"}`).

## Writing against another game version

`game::meta()` returns the game version, platform, the plugin's permissions and the host API version (`abi::kApiVersion`). Operations a game version does not provide return `Error::status == UnknownOp`, so plugins written against newer headers degrade instead of crashing.

## Layout

```
mods/
  build.sh            build script (wasm64 with Emscripten; --wasm32 with wasi-sdk)
  sdk/perlin/         headers: perlin.hpp pulls in everything
    abi.hpp           wire format shared with the host
    rpc.hpp           Result/Error and the single host import
    meta.hpp          compile-time reflection (enum names, aggregates, flags, fixed_string)
    detail/           field-binding helpers used by meta.hpp
    game.hpp act.hpp ui.hpp io.hpp types.hpp runtime.hpp
  barrel_roll/        Do a Barrel Roll port: barrel_roll.cpp, barrel_roll.json
  waila/              the WAILA plugin: waila.cpp, waila.json, waila/ (api, layout, vanilla providers), tests/
  examples/c/         a plugin in plain C, no SDK
  tools/              perlin_settings.py (manifest -> typed handles), check_api.py (every operation is hosted and wrapped)
  tests/              meta_test, abi_test (wire format), sdk_test (fake host); build each with g++ -std=c++23 -Isdk
  prebuilt/           built .wasm files and their manifests
```

## Camera, motion, drawing and blocks (`perlin::game`)

For mods that need to reach into the game itself. Everything that changes the game needs the `control` permission, and every override carries a lifetime in ticks: it lapses on its own when the mod stops refreshing it, so a crashed or unloaded mod never leaves the player rolled, frozen or blind.

| Call | What it does |
| --- | --- |
| `game::camera({.roll, .fovAdd, .fovSet, .offset*, .yawAdd, .pitchAdd, .ticks})` | Rolls the picture, changes the field of view, shifts the view, turns it without changing the aim. |
| `game::camera_info()` | Eye position, angles, FOV, aspect and the column-major view-projection matrix. |
| `game::to_screen(x, y, z)` | Projects a world point to GUI pixels (use it with `ui::Canvas` for ESP-style overlays, labels and markers). |
| `game::set_velocity` / `add_velocity` / `motion` | Sets or adds to the local player's velocity (per axis). Elytra boosts, dashes, grapples. |
| `game::teleport` | Moves the local player; the server sees ordinary movement. |
| `game::model_pose({.pitch, .yaw, .roll})` | Rotates the local player's model about its middle after the glide lean. `yaw` is a spin around the body's axis, which in flight is a barrel roll. |
| `game::lines` / `game::boxes` | World-space lines and box outlines drawn through the world renderer. A schematic overlay (Litematica style) is one `boxes` call per frame. |
| `game::find_block` / `game::set_blocks` | Looks up a block by name and states, and changes blocks in the client's own copy of the world (ghost blocks, previews). |
| `game::drop_packets(ids, ticks, outgoing)` | Discards incoming (or, with `outgoing`, outgoing) packets by id for a while; needs `packets` too. |
| `game::inject_packet(id, bytes)` | Handles a packet as if the server had sent it; needs `packets` too. |
| `game::packet_counters` | How many packets of each id have arrived. |


<!-- API-REFERENCE-BEGIN -->

## Reference

Generated from the SDK headers by `mods/tools/gen_api_reference.py`; run it after changing the API.

### Permissions per operation

| Op | Id | Needs |
| --- | --- | --- |
| `Log` | `0x0001` | always allowed |
| `Meta` | `0x0002` | always allowed |
| `Clock` | `0x0003` | always allowed |
| `PlayerState` | `0x0100` | always allowed |
| `PlayerEffects` | `0x0101` | always allowed |
| `PlayerLook` | `0x0102` | always allowed |
| `Inventory` | `0x0200` | always allowed |
| `InventorySelect` | `0x0201` | control |
| `InventoryDrop` | `0x0202` | control |
| `InventorySwap` | `0x0203` | control |
| `InventoryClick` | `0x0204` | control |
| `InventoryOpen` | `0x0205` | control |
| `InventoryClose` | `0x0206` | control |
| `WorldState` | `0x0300` | always allowed |
| `Block` | `0x0301` | always allowed |
| `Region` | `0x0302` | always allowed |
| `Raycast` | `0x0303` | always allowed |
| `Aim` | `0x0304` | always allowed |
| `FindBlocks` | `0x0305` | always allowed |
| `ChunkLoaded` | `0x0306` | always allowed |
| `BlockNames` | `0x0307` | always allowed |
| `BlockProps` | `0x0308` | always allowed |
| `BlockEntityData` | `0x0309` | always allowed |
| `DisplayName` | `0x030A` | always allowed |
| `BreakProgress` | `0x030B` | always allowed |
| `Entities` | `0x0400` | always allowed |
| `EntityById` | `0x0401` | always allowed |
| `PlayerList` | `0x0402` | always allowed |
| `Options` | `0x0500` | always allowed |
| `OptionGet` | `0x0501` | always allowed |
| `OptionSet` | `0x0502` | control |
| `Screen` | `0x0503` | always allowed |
| `Translate` | `0x0504` | always allowed |
| `ReloadResources` | `0x0505` | control |
| `PerspectiveGet` | `0x0506` | always allowed |
| `PerspectiveSet` | `0x0507` | always allowed |
| `ResourcePacks` | `0x0508` | always allowed |
| `InputDown` | `0x0600` | always allowed |
| `InputOverride` | `0x0601` | control |
| `KeyRegister` | `0x0602` | always allowed |
| `InputActions` | `0x0603` | always allowed |
| `MousePosition` | `0x0604` | always allowed |
| `Chat` | `0x0700` | control |
| `Command` | `0x0701` | control |
| `LocalMessage` | `0x0702` | always allowed |
| `CommandRegister` | `0x0703` | always allowed |
| `CommandUnregister` | `0x0704` | always allowed |
| `ServerCommands` | `0x0705` | always allowed |
| `Toast` | `0x0706` | always allowed |
| `Title` | `0x0707` | always allowed |
| `ChatHistory` | `0x0708` | always allowed |
| `TitleState` | `0x0709` | always allowed |
| `SwingArm` | `0x0800` | control |
| `UseItem` | `0x0801` | control |
| `UseOnBlock` | `0x0802` | control |
| `AttackEntity` | `0x0803` | control |
| `InteractEntity` | `0x0804` | control |
| `BreakBlock` | `0x0805` | control |
| `Look` | `0x0806` | control |
| `Respawn` | `0x0807` | control |
| `Disconnect` | `0x0808` | control |
| `PlaySound` | `0x0809` | always allowed |
| `Jump` | `0x080B` | control |
| `UiSubmit` | `0x0900` | always allowed |
| `UiClear` | `0x0901` | always allowed |
| `UiMeasure` | `0x0902` | always allowed |
| `StorageRead` | `0x0A00` | always allowed |
| `StorageWrite` | `0x0A01` | always allowed |
| `StorageList` | `0x0A02` | always allowed |
| `StorageRemove` | `0x0A03` | always allowed |
| `StorageExists` | `0x0A04` | always allowed |
| `StorageMakeDir` | `0x0A05` | always allowed |
| `HttpRequest` | `0x0B00` | network |
| `PacketSend` | `0x0B01` | packets |
| `PacketName` | `0x0B02` | always allowed |
| `SettingGet` | `0x0C01` | always allowed |
| `SettingSet` | `0x0C02` | always allowed |
| `SettingList` | `0x0C03` | always allowed |
| `ModList` | `0x0C10` | always allowed |
| `IconSet` | `0x0C11` | always allowed |
| `OpenScreen` | `0x0D00` | always allowed |
| `OpenModSettings` | `0x0D01` | always allowed |
| `TakeScreenshot` | `0x0D02` | always allowed |
| `CyclePerspective` | `0x0D03` | always allowed |
| `CopyToClipboard` | `0x0D04` | always allowed |
| `OpenUrl` | `0x0D05` | network |
| `ToggleHud` | `0x0D06` | always allowed |
| `CopyFile` | `0x0E00` | files, network |
| `MoveFile` | `0x0E01` | files, network |
| `DownloadFile` | `0x0E02` | files, network |
| `CameraControl` | `0x0F00` | control |
| `CameraState` | `0x0F01` | always allowed |
| `WorldToScreen` | `0x0F02` | always allowed |
| `PlayerMotion` | `0x0F10` | control |
| `PlayerTeleport` | `0x0F11` | control |
| `PlayerModelPose` | `0x0F20` | control |
| `PlayerSetLook` | `0x0F21` | control |
| `WorldLines` | `0x0F30` | always allowed |
| `WorldBoxes` | `0x0F31` | always allowed |
| `ClientBlockSet` | `0x0F40` | control |
| `ClientBlockGet` | `0x0F41` | always allowed |
| `PacketDrop` | `0x0F50` | control, packets |
| `PacketCounters` | `0x0F51` | always allowed |
| `PacketInject` | `0x0F52` | control, packets |

### Functions

#### `perlin/game.hpp`

| Signature | Notes |
| --- | --- |
| `Result<PlayerState> player()` |  |
| `Vec3 position(const PlayerState& p)` |  |
| `Vec3 velocity(const PlayerState& p)` |  |
| `Vec3 eye_position(const PlayerState& p)` |  |
| `Vec3 look_direction(const PlayerState& p)` |  |
| `Result<Look> look()` |  |
| `Result<std::vector<Effect>> effects()` |  |
| `Result<Inventory> inventory()` |  |
| `Result<WorldState> world()` |  |
| `Result<BlockInfo> block(const BlockPos& pos)` |  |
| `std::string block_name(const BlockPos& pos)` |  |
| `bool chunk_loaded(int chunkX, int chunkZ)` |  |
| `Result<abi::Region> region(const BlockPos& from, const BlockPos& to)` |  |
| `Result<RayHit> raycast(const Vec3& origin, const Vec3& direction, RaycastOptions options = {})` |  |
| `Result<AimState> aim()` |  |
| `Result<std::vector<abi::FoundBlock>> find_blocks(const BlockPos& center, const FindOptions& options)` |  |
| `Result<BlockProps> block_props(std::string_view name)` |  |
| `Result<BlockEntityInfo> block_entity(const BlockPos& pos)` |  |
| `std::string display_name(NameKind kind, std::string_view id, int aux = 0)` |  |
| `Result<BreakState> break_state()` |  |
| `Result<std::vector<std::string>> block_names()` |  |
| `Result<std::vector<Entity>> entities(const EntityQuery& query = {})` |  |
| `Result<Entity> entity(uint64_t runtimeId)` |  |
| `Result<std::vector<PlayerEntry>> players()` |  |
| `Result<ScreenState> screen()` |  |
| `Result<abi::Meta> meta()` |  |
| `Result<abi::Perspective> perspective()` |  |
| `Result<std::vector<abi::ChatLine>> chat_history(uint32_t count = 20)` |  |
| `Result<abi::TitleStateData> title_state()` |  |
| `Result<std::vector<abi::PackEntry>> resource_packs()` |  |
| `Result<std::vector<abi::ModEntry>> mods()` |  |
| `Result<std::string> option_text(std::string_view key)` |  |
| `Result<std::string> options_json()` |  |
| `Target target()` |  |
| `std::string translate(std::string_view key)` |  |
| `Result<std::vector<std::string>> server_commands()` |  |
| `Result<std::vector<std::string>> input_actions()` |  |
| `bool input_down(std::string_view action)` |  |
| `bool input_down(Control control)` |  |
| `Result<MousePos> mouse()` |  |
| `int64_t now_ms()` |  |

#### `perlin/adv.hpp`

| Signature | Notes |
| --- | --- |
| `Result<void> camera(const CameraControlState& state)` |  |
| `Result<CameraInfo> camera_info()` |  |
| `Result<ScreenPoint> to_screen(double x, double y, double z)` |  |
| `Result<void> set_velocity(double x, double y, double z)` |  |
| `Result<void> add_velocity(double x, double y, double z)` |  |
| `Result<void> motion(const abi::MotionRequest& request)` |  |
| `Result<void> teleport(double x, double y, double z, bool keep_velocity = false)` |  |
| `Result<void> set_look(double x, double y, double z)` |  |
| `Result<void> model_pose(const ModelPose& pose)` |  |
| `Result<void> lines(const std::vector<WorldLine>& list, int ticks = 2)` |  |
| `Result<void> boxes(const std::vector<WorldBox>& list, int ticks = 2)` |  |
| `Result<uint32_t> find_block(std::string_view name, std::vector<std::pair<std::string, std::string>> states = {})` |  |
| `Result<void> set_blocks(const std::vector<abi::ClientBlockEdit>& edits)` |  |
| `Result<void> drop_packets(const std::vector<uint32_t>& ids, int ticks = 20, bool outgoing = false)` |  |
| `Result<std::vector<abi::PacketCount>> packet_counters()` |  |
| `Result<void> inject_packet(uint32_t id, std::span<const uint8_t> body)` |  |

#### `perlin/act.hpp`

| Signature | Notes |
| --- | --- |
| `Result<void> say(std::string_view text)` |  |
| `Result<void> run_command(std::string_view command)` |  |
| `Result<void> message(std::string_view text)` |  |
| `Result<void> toast(std::string_view text, std::chrono::milliseconds duration = std::chrono::milliseconds(3000))` |  |
| `Result<void> title(std::string_view text, const TitleOptions& options = {})` |  |
| `Result<void> select_slot(int slot)` |  |
| `Result<void> swing()` |  |
| `Result<void> use_item()` |  |
| `Result<void> use_on(const BlockPos& pos, Face face = Face::Up)` |  |
| `Result<void> attack(uint64_t runtimeId)` |  |
| `Result<void> interact(uint64_t runtimeId)` |  |
| `Result<void> break_block(const BlockPos& pos, Face face = Face::Up)` |  |
| `Result<void> look(float yaw, float pitch)` |  |
| `Result<void> look_at(const Vec3& from, const Vec3& target)` |  |
| `Result<void> jump()` |  |
| `Result<void> respawn()` |  |
| `Result<void> disconnect()` |  |
| `Result<void> drop(int count = 1)` |  |
| `Result<void> swap_slots(int a, int b)` |  |
| `Result<void> click_slot(int slot, bool right = false, bool shift = false)` |  |
| `Result<void> open_inventory()` |  |
| `Result<void> close_inventory()` |  |
| `Result<void> play(const Sound& sound)` |  |
| `Result<void> drive(const Inputs& inputs)` |  |
| `Result<void> set_option(std::string_view key, std::string_view value)` |  |
| `Result<void> set_perspective(abi::Perspective mode)` |  |
| `Result<void> reload_resources()` |  |
| `Result<void> open_screen(Screen screen)` |  |
| `Result<void> open_settings(std::string_view mod = {})` |  |
| `Result<void> screenshot()` |  |
| `Result<void> cycle_perspective()` |  |
| `Result<void> toggle_hud()` |  |
| `Result<void> copy_to_clipboard(std::string_view text)` |  |
| `Result<void> open_url(std::string_view url)` |  |

#### `perlin/io.hpp`

| Signature | Notes |
| --- | --- |
| `Result<std::vector<uint8_t>> read_bytes(std::string_view path)` |  |
| `Result<std::string> read_text(std::string_view path)` |  |
| `Result<void> write_bytes(std::string_view path, std::span<const uint8_t> data)` |  |
| `Result<void> write_text(std::string_view path, std::string_view text)` |  |
| `bool exists(std::string_view path)` |  |
| `Result<void> remove(std::string_view path)` |  |
| `Result<void> make_directory(std::string_view path)` |  |
| `Result<std::vector<std::string>> list(std::string_view path = "")` |  |
| `Location in(Area area, std::string_view path)` |  |
| `Result<void> copy(const Location& from, const Location& to)` |  |
| `Result<void> move(const Location& from, const Location& to)` |  |
| `Result<uint32_t> send(const Request& request, Callback callback)` |  |
| `Result<uint32_t> get(std::string url, Callback callback)` |  |
| `Result<uint32_t> post(std::string url, std::string body, std::string content_type, Callback callback)` |  |
| `Result<uint32_t> download(std::string_view url, const fs::Location& to, Callback callback)` |  |
| `Result<std::string> packet_name(uint32_t id)` |  |
| `Result<void> send_packet(uint32_t id, std::span<const uint8_t> body)` |  |

#### `perlin/ui.hpp`

| Signature | Notes |
| --- | --- |
| `float text_width(std::string_view value, float scale = 1.0f)` |  |
| `Result<void> submit(std::string_view layer, int z, const Canvas& canvas)` |  |
| `Result<void> clear(std::string_view layer)` |  |

#### `perlin/settings.hpp`

| Signature | Notes |
| --- | --- |
| `Result<std::string> setting_text(std::string_view id)` |  |

#### `perlin/types.hpp`

| Signature | Notes |
| --- | --- |
| `std::string_view short_name(std::string_view id)` |  |

#### `perlin/runtime.hpp`

| Signature | Notes |
| --- | --- |
| `std::string key_id(Key key)` |  |
| `Result<void> set_mod_icon(std::string_view relative_png)` |  |
| `std::vector<std::string> Context::split(const std::string& text)` |  |
| `void Context::run_timers()` |  |
| `void Context::draw_layers(const abi::EventFrame& frame)` |  |
| `void Context::dispatch(abi::Event id, abi::Reader& r, uint32_t& flags)` |  |
| `void Context::run_unload()` |  |

### Enumerations

* `Status`: `Ok`, `UnknownOp`, `Denied`, `BadArguments`, `Unavailable`, `NotFound`, `Failed`
* `Permission`: `None`, `Network`, `Packets`, `Control`, `Storage`, `Files`
* `Draw`: `Rect`, `Text`, `Item`, `Image`, `Line`, `Gradient`, `Border`
* `Screen`: `None`, `Pause`, `Chat`, `Inventory`, `Options`, `Mods`, `Wardrobe`, `Death`, `Workbench`, `Menu`, `Other`
* `GameMode`: `Survival`, `Creative`, `Adventure`, `SurvivalSpectator`, `CreativeSpectator`, `Default`, `Spectator`
* `Dimension`: `Overworld`, `Nether`, `End`
* `Difficulty`: `Peaceful`, `Easy`, `Normal`, `Hard`
* `PermissionLevel`: `Unknown`, `Visitor`, `Member`, `Operator`, `Custom`
* `Face`: `Down`, `Up`, `North`, `South`, `West`, `East`
* `AimKind`: `None`, `Block`, `Entity`
* `ChatType`: `Raw`, `Chat`, `Translation`, `Popup`, `JukeboxPopup`, `Tip`, `System`, `Whisper`, `Announcement`, `ObjectWhisper`, `Object`, `ObjectAnnouncement`
* `DisconnectReason`: `Unknown`, `CantConnectNoInternet`, `NoPermissions`, `UnrecoverableError`, `ThirdPartyBlocked`, `ThirdPartyNoInternet`, `ThirdPartyBadIp`, `ThirdPartyNoServerOrServerLocked`, `VersionMismatch`, `SkinIssue`, `InviteSessionNotFound`
* `EffectId`: `Speed`, `Slowness`, `Haste`, `MiningFatigue`, `Strength`, `InstantHealth`, `InstantDamage`, `JumpBoost`, `Nausea`, `Regeneration`, `Resistance`, `FireResistance`, `WaterBreathing`, `Invisibility`, `Blindness`, `NightVision`, `Hunger`, `Weakness`, `Poison`, `Wither`, `HealthBoost`, `Absorption`, `Saturation`, `Levitation`, `FatalPoison`, `ConduitPower`, `SlowFalling`, `BadOmen`, `VillageHero`, `Darkness`, `TrialOmen`, `RaidOmen`, `WindCharged`, `Weaving`, `Oozing`, `Infested`
* `Perspective`: `FirstPerson`, `ThirdPersonBack`, `ThirdPersonFront`
* `SettingKind`: `Toggle`, `Integer`, `Number`, `Choice`, `Button`, `Label`, `Header`
* `Area`: `Mod`, `Screenshots`, `ResourcePacks`, `ShaderPacks`
* `EntityFlag`: `OnFire`, `Sneaking`, `Riding`, `Sprinting`, `UsingItem`, `Invisible`, `Tempted`, `InLove`, `Saddled`, `Powered`, `Ignited`, `Baby`, `Converting`, `Critical`, `CanShowName`, `AlwaysShowName`, `NoAi`, `Silent`, `WallClimbing`, `CanClimb`, `CanSwim`, `CanFly`, `CanWalk`, `Resting`, `Sitting`, `Angry`, `Interested`, `Charged`, `Tamed`, `Orphaned`, `Leashed`, `Sheared`, `Gliding`, `Elder`, `Moving`, `Breathing`, `Chested`, `Stackable`, `ShowBase`, `Rearing`, `Vibrating`, `Idling`, `EvokerSpell`, `ChargeAttack`, `WasdControlled`, `CanPowerJump`, `Lingering`, `HasCollision`, `HasGravity`, `FireImmune`, `Dancing`, `Enchanted`, `ReturnTrident`, `ContainerPrivate`, `Transforming`, `DamageNearbyMobs`, `Swimming`, `Bribed`, `Pregnant`, `LayingEgg`, `PassengerCanPick`, `TransitionSitting`, `Eating`, `LayingDown`
* `Tool`: `None`, `Pickaxe`, `Axe`, `Shovel`, `Hoe`, `Shears`, `Sword`
* `NameKind`: `Item`, `Block`, `Entity`

### Data types

#### `Item`

| Field | Notes |
| --- | --- |
| `std::string name` |  |
| `int32_t count = 0, aux = 0, damage = 0` |  |
| `bool enchanted = false` |  |
| `std::string customName` |  |
| `std::vector<std::pair<int32_t, int32_t>> enchants` |  |
| `std::vector<std::string> lore` |  |

#### `PlayerState`

| Field | Notes |
| --- | --- |
| `uint64_t runtimeId = 0` |  |
| `int64_t uniqueId = 0` |  |
| `std::string name` |  |
| `double x = 0, y = 0, z = 0` |  |
| `double vx = 0, vy = 0, vz = 0` |  |
| `float yaw = 0, pitch = 0, headYaw = 0, eyeHeight = 1.62f` |  |
| `spectator = false, onFire = false, riding = false, connected = false` |  |
| `GameMode gameMode = GameMode::Survival` |  |
| `Dimension dimension = Dimension::Overworld` |  |
| `int32_t selectedSlot = 0` |  |
| `PermissionLevel permission = PermissionLevel::Unknown` |  |
| `float health = 20, maxHealth = 20, absorption = 0, hunger = 20, saturation = 5, xpProgress = 0, walkSpeed = 0.1f` |  |
| `int32_t xpLevel = 0, air = 300, maxAir = 300` |  |

#### `Effect`

| Field | Notes |
| --- | --- |
| `EffectId id{}` |  |
| `int32_t amplifier = 0, ticks = 0` |  |
| `bool ambient = false, particles = true` |  |

#### `Inventory`

| Field | Notes |
| --- | --- |
| `std::array<Item, 36> slots` |  |
| `std::array<Item, 4> armor` |  |
| `Item offhand, cursor` |  |
| `int32_t selected = 0` |  |
| `bool open = false` |  |

#### `Attribute`

| Field | Notes |
| --- | --- |
| `std::string name` |  |
| `float min = 0, value = 0, max = 0` |  |

#### `Entity`

| Field | Notes |
| --- | --- |
| `uint64_t runtimeId = 0` |  |
| `int64_t uniqueId = 0` |  |
| `std::string type, name, nameTag, uuid` |  |
| `double x = 0, y = 0, z = 0` |  |
| `float yaw = 0, pitch = 0, headYaw = 0, health = -1, scale = 1` |  |
| `bool onGround = false, isPlayer = false, isItem = false, isSelf = false` |  |
| `EntityFlags flags` |  |
| `std::string itemName` |  |
| `int32_t itemCount = 0` |  |
| `std::string mainHand, offHand` |  |
| `std::array<std::string, 4> armor` |  |
| `int64_t vehicleUid = 0, ownerUid = 0, targetUid = 0` |  |
| `int32_t variant = 0, markVariant = 0, color = 0` |  |
| `float maxHealth = -1` |  |
| `bool dying = false` |  |
| `std::vector<Attribute> attributes` |  |
| `std::vector<std::pair<std::string, float>> properties` |  |
| `std::vector<std::pair<std::string, std::string>> propertyText` |  |

#### `PlayerEntry`

| Field | Notes |
| --- | --- |
| `std::string uuid, name` |  |
| `int64_t uid = 0` |  |
| `PermissionLevel permission = PermissionLevel::Unknown` |  |

#### `WorldState`

| Field | Notes |
| --- | --- |
| `Dimension dimension = Dimension::Overworld` |  |
| `Difficulty difficulty = Difficulty::Normal` |  |
| `GameMode worldGameMode = GameMode::Survival` |  |
| `int64_t day = 0` |  |
| `int32_t timeOfDay = 0` |  |
| `double totalTicks = 0` |  |
| `float rain = 0, thunder = 0` |  |
| `bool commandsEnabled = true, haveSpawn = false` |  |
| `int32_t spawnX = 0, spawnY = 0, spawnZ = 0` |  |
| `std::vector<std::pair<std::string, int32_t>> rules` |  |

#### `BlockInfo`

| Field | Notes |
| --- | --- |
| `bool loaded = false` |  |
| `uint32_t runtimeId = 0` |  |
| `std::string name` |  |
| `int32_t biome = 0` |  |
| `uint32_t liquid = 0` |  |
| `std::vector<std::pair<std::string, std::string>> states` |  |

#### `BlockProps`

| Field | Notes |
| --- | --- |
| `bool known = false` |  |
| `float hardness = 0` |  |
| `Tool tool = Tool::None` |  |
| `int32_t harvestLevel = 0` |  |
| `bool needsSupportBelow = false, breaksWholeStack = false` |  |
| `std::string displayName` |  |
| `std::vector<std::string> properties` |  |

#### `BlockEntityInfo`

| Field | Notes |
| --- | --- |
| `bool present = false` |  |
| `std::string id` |  |
| `int32_t color = -1` |  |
| `bool waxed = false` |  |
| `float rotation = 0` |  |
| `std::string frontText, backText` |  |
| `bool frontGlow = false, backGlow = false` |  |
| `int32_t base = -1, bannerType = 0` |  |
| `std::vector<std::pair<std::string, int32_t>> patterns` |  |
| `std::string nbt` |  |

#### `BreakState`

| Field | Notes |
| --- | --- |
| `bool active = false` |  |
| `int32_t x = 0, y = 0, z = 0` |  |
| `float progress = 0` |  |

#### `RayHit`

| Field | Notes |
| --- | --- |
| `bool hit = false` |  |
| `int32_t x = 0, y = 0, z = 0` |  |
| `Face face = Face::Up` |  |
| `double hx = 0, hy = 0, hz = 0, distance = 0` |  |
| `std::string block` |  |

#### `AimState`

| Field | Notes |
| --- | --- |
| `AimKind kind = AimKind::None` |  |
| `RayHit block` |  |
| `uint64_t entity = 0` |  |
| `std::string entityType` |  |
| `double distance = 0` |  |

#### `ScreenState`

| Field | Notes |
| --- | --- |
| `int32_t guiWidth = 0, guiHeight = 0, guiScale = 1, framebufferWidth = 0, framebufferHeight = 0` |  |
| `float fps = 0, frameMs = 0` |  |
| `Screen screen = Screen::None` |  |
| `bool paused = false, connected = false, hudHidden = false, debug = false` |  |
| `std::string server, language, platform, gameVersion` |  |

#### `Meta`

| Field | Notes |
| --- | --- |
| `uint32_t apiVersion = kApiVersion` |  |
| `std::string gameVersion, plugin, platform` |  |
| `Permissions permissions` |  |
| `int64_t nowMs = 0` |  |
| `uint64_t tick = 0` |  |

#### `InputOverrideState`

| Field | Notes |
| --- | --- |
| `float forward = 0, strafe = 0` |  |
| `bool jump = false, sneak = false, sprint = false, attack = false, use = false` |  |
| `int32_t ticks = 1` |  |

#### `CameraControlState`

| Field | Notes |
| --- | --- |
| `float roll = 0` |  |
| `float fovAdd = 0` |  |
| `float fovSet = 0` |  |
| `float offsetX = 0, offsetY = 0, offsetZ = 0` |  |
| `float yawAdd = 0, pitchAdd = 0` |  |
| `int32_t ticks = 2` |  |

#### `CameraInfo`

| Field | Notes |
| --- | --- |
| `double x = 0, y = 0, z = 0` |  |
| `float yaw = 0, pitch = 0, fov = 70, aspect = 1.78f, nearPlane = 0.1f, farPlane = 600` |  |
| `int32_t perspective = 0` |  |
| `std::array<float, 16> viewProjection{}` |  |

#### `WorldPoint`

| Field | Notes |
| --- | --- |
| `double x = 0, y = 0, z = 0` |  |

#### `ScreenPoint`

| Field | Notes |
| --- | --- |
| `float x = 0, y = 0` |  |
| `float depth = 0` |  |
| `bool visible = false` |  |

#### `MotionRequest`

| Field | Notes |
| --- | --- |
| `bool add = false` |  |
| `double x = 0, y = 0, z = 0` |  |
| `bool axisX = true, axisY = true, axisZ = true` |  |

#### `TeleportRequest`

| Field | Notes |
| --- | --- |
| `double x = 0, y = 0, z = 0` |  |
| `bool keepVelocity = false` |  |

#### `ModelPose`

| Field | Notes |
| --- | --- |
| `float roll = 0, pitch = 0, yaw = 0` |  |
| `int32_t ticks = 2` |  |

#### `LookSet`

| Field | Notes |
| --- | --- |
| `double x = 0, y = 0, z = -1` |  |

#### `WorldLine`

| Field | Notes |
| --- | --- |
| `double x1 = 0, y1 = 0, z1 = 0, x2 = 0, y2 = 0, z2 = 0` |  |
| `uint32_t color = 0xFFFFFFFF` |  |
| `float width = 1.0f` |  |

#### `WorldLines`

| Field | Notes |
| --- | --- |
| `int32_t ticks = 2` |  |
| `std::vector<WorldLine> lines` |  |

#### `WorldBox`

| Field | Notes |
| --- | --- |
| `double x0 = 0, y0 = 0, z0 = 0, x1 = 1, y1 = 1, z1 = 1` |  |
| `uint32_t color = 0xFFFFFFFF` |  |
| `float width = 1.0f` |  |

#### `WorldBoxes`

| Field | Notes |
| --- | --- |
| `int32_t ticks = 2` |  |
| `std::vector<WorldBox> boxes` |  |

#### `ClientBlockEdit`

| Field | Notes |
| --- | --- |
| `int32_t x = 0, y = 0, z = 0` |  |
| `uint32_t runtimeId = 0` |  |

#### `ClientBlockEdits`

| Field | Notes |
| --- | --- |
| `std::vector<ClientBlockEdit> edits` |  |

#### `BlockQuery`

| Field | Notes |
| --- | --- |
| `std::string name` |  |
| `std::vector<std::pair<std::string, std::string>> states` |  |

#### `BlockQueryResult`

| Field | Notes |
| --- | --- |
| `bool found = false` |  |
| `uint32_t runtimeId = 0` |  |

#### `PacketDropRequest`

| Field | Notes |
| --- | --- |
| `int32_t ticks = 20` |  |
| `std::vector<uint32_t> ids` |  |
| `bool outgoing = false` |  |

#### `PacketCount`

| Field | Notes |
| --- | --- |
| `uint32_t id = 0, count = 0` |  |

#### `Region`

| Field | Notes |
| --- | --- |
| `int32_t x0 = 0, y0 = 0, z0 = 0, sx = 0, sy = 0, sz = 0` |  |
| `std::vector<std::string> palette` |  |
| `std::vector<uint16_t> cells` |  |

#### `FoundBlock`

| Field | Notes |
| --- | --- |
| `int32_t x = 0, y = 0, z = 0` |  |
| `std::string name` |  |

#### `SettingDef`

| Field | Notes |
| --- | --- |
| `std::string id` |  |
| `SettingKind kind = SettingKind::Toggle` |  |
| `std::string label, description` |  |
| `std::string icon` |  |
| `std::string value, fallback` |  |
| `double min = 0, max = 1, step = 0` |  |
| `std::vector<std::string> choices, choiceLabels` |  |
| `std::string unit` |  |
| `bool restart = false` |  |

#### `SettingEntry`

| Field | Notes |
| --- | --- |
| `std::string id, value` |  |

#### `FileLocation`

| Field | Notes |
| --- | --- |
| `Area area = Area::Mod` |  |
| `std::string path` |  |

#### `ModEntry`

| Field | Notes |
| --- | --- |
| `std::string name, displayName, description, version, author, state` |  |
| `bool enabled = true, self = false, hasSettings = false` |  |

#### `ChatLine`

| Field | Notes |
| --- | --- |
| `std::string text` |  |
| `float alpha = 1` |  |

#### `PackEntry`

| Field | Notes |
| --- | --- |
| `std::string name` |  |
| `bool server = false` |  |

#### `TitleStateData`

| Field | Notes |
| --- | --- |
| `std::string title, subtitle, actionBar` |  |
| `float titleAlpha = 0, actionBarAlpha = 0` |  |

#### `EventTick`

| Field | Notes |
| --- | --- |
| `float dt = 0.05f` |  |
| `uint64_t tick = 0` |  |

#### `EventFrame`

| Field | Notes |
| --- | --- |
| `float dt = 0, fps = 0` |  |
| `int32_t guiWidth = 0, guiHeight = 0` |  |

#### `EventChat`

| Field | Notes |
| --- | --- |
| `ChatType type = ChatType::Raw` |  |
| `std::string sender, message, raw` |  |

#### `EventKey`

| Field | Notes |
| --- | --- |
| `std::string id` |  |
| `bool down = false` |  |

#### `EventCommand`

| Field | Notes |
| --- | --- |
| `std::string name, args` |  |

#### `EventConnection`

| Field | Notes |
| --- | --- |
| `std::string server, message` |  |
| `DisconnectReason reason = DisconnectReason::Unknown` |  |

#### `EventDamage`

| Field | Notes |
| --- | --- |
| `bool known = false` |  |
| `float dx = 0, dz = 0, health = 0` |  |

#### `EventText`

| Field | Notes |
| --- | --- |
| `std::string text` |  |

#### `EventChange`

| Field | Notes |
| --- | --- |
| `T from{}, to{}` |  |

#### `EventSlot`

| Field | Notes |
| --- | --- |
| `int32_t slot = 0` |  |
| `Item item` |  |

#### `EventEntity`

| Field | Notes |
| --- | --- |
| `Entity entity` |  |

#### `EventSound`

| Field | Notes |
| --- | --- |
| `std::string name` |  |
| `double x = 0, y = 0, z = 0` |  |
| `float volume = 1, pitch = 1` |  |

#### `EventHttp`

| Field | Notes |
| --- | --- |
| `uint32_t id = 0` |  |
| `int32_t status = 0` |  |
| `std::string error` |  |
| `std::vector<std::pair<std::string, std::string>> headers` |  |
| `std::vector<uint8_t> body` |  |

#### `EventPacket`

| Field | Notes |
| --- | --- |
| `uint32_t id = 0` |  |
| `bool outgoing = false` |  |
| `std::vector<uint8_t> body` |  |

#### `EventTitle`

| Field | Notes |
| --- | --- |
| `std::string title, subtitle, actionBar` |  |

#### `EventLook`

| Field | Notes |
| --- | --- |
| `float dx = 0, dy = 0, dt = 0` |  |
| `double dirX = 0, dirY = 0, dirZ = -1` |  |
| `double vx = 0, vy = 0, vz = 0` |  |
| `uint32_t keys = 0` |  |
| `bool gliding = false, underwater = false, thirdPerson = false, onGround = false` |  |

#### `EventSettingChanged`

| Field | Notes |
| --- | --- |
| `std::string id, value` |  |
| `SettingKind kind = SettingKind::Toggle` |  |

#### `Vec3`

| Field | Notes |
| --- | --- |
| `double x = 0, y = 0, z = 0` |  |

#### `BlockPos`

| Field | Notes |
| --- | --- |
| `int x = 0, y = 0, z = 0` |  |

#### `Rgba`

| Field | Notes |
| --- | --- |
| `uint8_t r = 255, g = 255, b = 255, a = 255` |  |

#### `BlockInfo`

| Field | Notes |
| --- | --- |
| `int out = fallback` |  |

<!-- API-REFERENCE-END -->
