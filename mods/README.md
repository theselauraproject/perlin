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

## One schema for client and server (`mods/api/*.papi`)

`mods/` is the Perlin mod API, kept apart from the game so it can become its own repository (theperlinproject/perlin-api). Everything a plugin can call or receive, on the client and on the server, is declared once in the schema files in `mods/api/`, and everything else is generated from them by `mods/codegen/generate.py`:

```
tag hot on event, op @doc("Raised or called many times per second");
struct Form @since(3) { kind: FormKind = Message; title: string; body: string; buttons: list<FormButton>; controls: list<FormControl>; }
op PlayerForm = 0x7023 @hosts(server) @category(ui) (player: u64, form: Form) -> u32;
event PlayerDamage = 52 @hosts(server) @cancellable EventServerDamage;
```

Declarations are `api`, `tag`, `const`, `enum`, `flags`, `alias`, `struct`, `op` and `event`. Types are the scalars (`bool`, `i8` to `u64`, `f32`, `f64`), `string`, `bytes`, `list<T>`, `optional<T>`, `pair<A, B>`, `array<T, N>`, `flagset<F>`, `indexset<E, u64>`, and `rest_text` / `rest_bytes` (the rest of the message, without a length). An inline parameter list `(x: i32, y: i32)` makes a `NameRequest` or `NameResponse` struct. Tags are declared in the schema itself (`00_tags.papi`) and checked by the generator: `@doc`, `@hosts`, `@permission`, `@since`, `@deprecated`, `@internal`, `@cancellable`, `@hot`, `@category`, `@range`, `@unit`, `@max`, `@layout(packed)`, `@size` and `@cpp`. Bindings for other languages read the same tags from the JSON IR.

`python3 mods/codegen/generate.py` writes:

| Output | What it is |
| --- | --- |
| `sdk/perlin_abi.h` | The C ABI: every enum and struct, writer/reader/arena helpers, an encoder and decoder per type, a typed wrapper per op (`perlin_player_form(&req, &id, &arena)`), a decoder per event (`perlin_decode_event_player_damage`), `PERLIN_OP_*` and `PERLIN_EVENT_*`. Freestanding C99, no libc needed. |
| `sdk/perlin/gen/abi_types.hpp`, `abi_spec.hpp` | C++ types, `Op`/`Event`, `OpSpec<Op>` and `EventSpec<Event>` (request, response, permission, hosts, since, hot, name, category). |
| `sdk/perlin/gen/calls.hpp` | One typed C++ function per op: `perlin::api::player_info(id)`, `perlin::api::fill(...)`. |
| `sdk/perlin/gen/event_tags.hpp`, `plugin_wire.hpp` | `event::Name` tags and the automatic `onName` wiring for plugin classes. |
| `sdk/perlin/gen/client_service.hpp`, `server_service.hpp` | A host interface per program: a virtual `OpName(ctx, request)` per op it serves, and `Bind(table)` registering them all. The server implements `ServerService`; an op it does not override answers `Unavailable`. |
| `sdk/perlin/gen/coverage.hpp`, `sdk/perlin/abi_table.h` | Op and event lists per host, and an X-macro table for C. |
| `api/perlin_api.json` | The whole schema as JSON, for C#, Rust, Java, Zig, Python and JavaScript generators. |
| `docs/API.md` | The reference: every op, event, enum and struct with its tags. |

`generate.py --check` runs in ctest (`perlin_api_codegen_check`), and `tools/check_api.py` fails when the schema has an op or event a host never serves. `abi::host::RequestMatchesSchema` / `ResponseMatchesSchema` check any message against the schema; set `PERLIN_MOD_WIRE_CHECK=1` to make the client log every call that does not match.

Adding an operation is: one `op` line in the schema, run the generator, then either override `OpName` in the server's service or `Handle<Op::Name>` in the client. The wrappers in C and C++ appear by themselves.

Server plugins have friendly wrappers in `perlin/server.hpp` plus every generated call:

```cpp
using namespace perlin;

struct Greeter : Plugin {
    void onPlayerJoin(const abi::EventPlayer& p) { (void)server::broadcast("welcome, {}!", p.name); }
    Flow onPlayerChat(const abi::EventPlayerChat& c) {
        return c.message.contains("creeper") ? Flow::cancel : Flow::pass;
    }
    Flow onPlayerDamage(const abi::EventServerDamage& d) {
        if (d.cause == abi::DamageCause::Fall)
            (void)api::event_amend("amount", d.amount / 2, "");
        return Flow::pass;
    }
};
PERLIN_REGISTER(Greeter)
```

The server side covers players (info, health, food, experience, effects, game mode, inventory slots, velocity, dimension, titles, action bar, sounds, transfer, operator, spawn point, forms, commands), the world (block states, fill, time, weather, game rules, difficulty, explosions, particles, item drops, world spawn), entities (spawn, list, remove, damage, teleport, velocity, health), loot tables (read and replace any Java loot table) and the server itself (run a console command and read its output, server name), with events for damage, death, respawn, using blocks, attacking, entity damage, death and spawn, eating, game mode, form answers, movement, explosions, dropping and picking up items, opening containers, changing dimension, commands and logins. `EventAmend` changes the event being handled: the damage amount, a chat message, an explosion's power, a command line, a login refusal reason. Storage, settings, HTTP, the mod list and messages between plugins work on both hosts.

## Bringing your own renderer (`perlin/gfx.hpp`, `perlin/input.hpp`)

Enough is exposed to hook up Dear ImGui or any other immediate-mode library:

* `gfx::Texture::create(w, h, rgba)` / `update` for atlases, `gfx::display()` for the framebuffer size and mouse.
* `gfx::load_image("textures/map/map_icons")` returns the pixels of a resource-pack image (the active pack, then vanilla), to copy into an atlas.
* `gfx::submit(layer, z, vertices, indices, commands)` takes ImGui's own arrays: 20-byte vertices (`ImDrawVert`), 16- or 32-bit indices, and commands with a clip rectangle, a texture id and index/vertex offsets. It is drawn every frame with straight alpha until replaced: layers with `z` below `abi::kGfxScreenZ` (100) draw with the HUD, under the pause screen and other menus, and higher layers draw on top of everything. `gfx::DrawList` is a small builder for hand-made lists.
* `input::grab(mouse, keyboard)` (needs `control`) delivers `MouseMove`, `MouseButton`, `MouseWheel`, `KeyRaw` and `TextInput` events and frees the cursor. Pressing Escape three times quickly always releases every grab.
* `gfx::native_handles()` (needs the **`native`** permission) returns the graphics API, adapter, window, display and device handles as plain integers. A WebAssembly module cannot dereference host pointers, so these are for identification and for a future native bridge; rendering goes through `gfx::submit`.

`examples/sketchpad` is a complete small example. Not tested inside the game yet.

## Everything is exposed

The wire format is the field list of each struct in the schema, so adding a field to a struct the host already sends, such as an entity's attributes, exposes it to every plugin and every language with no wrapper to write. `tools/check_api.py` (run by CTest as `perlin_api_host_check`) fails when an op or event in the schema is never served or raised by its host. Every enumeration in `perlin::abi` prints by name with `std::format` without being listed anywhere. `perlin/json.hpp` is a small JSON reader for the text the game hands over (block entity tags, option dumps, HTTP bodies): `json::parse(text)` then `tag["Items"][0]["Name"].string()`; missing keys read as null, so chains never fail.

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

A plugin is any WebAssembly module (core wasm, no component model), 64-bit (memory64) by default and 32-bit when the toolchain cannot do 64. C gets the whole API from `perlin.h` (`mods/examples/c/hello.c`, `server/examples/hello_server/hello_server.c`); other languages generate their bindings from `mods/api/perlin_api.json`. The contract:

* Import module `perlin`, name `call`: `(op: i32, in: ptr, in_len, out: ptr, out_cap) -> i32`.
* Export `perlin_alloc(size) -> ptr`, `perlin_free(ptr)`, `perlin_event(id: i32, ptr, len) -> i32`, and either `perlin_subscriptions_word(word: i32) -> i32` (bit `id % 32` of word `id / 32`, up to 128 events) or the older `perlin_subscriptions` / `perlin_subscriptions_hi` pair.
* `ptr` is `i32` in a 32-bit module and `i64` in a 64-bit one; linear memory is the module's own and exported, 1 GiB at most.
* The host calls `perlin_event(Load = 1, ...)` once after instantiation; register commands, keys and HUD layers there. Bit 0 of the result cancels a cancellable event.
* Messages are little-endian fields in schema order: scalars at their width, strings and bytes as a `u32` length and the bytes, booleans as one byte, lists as a `u32` count and the items, optionals as a presence byte and the value.
* `call` returns the bytes written, a negative `Status` (`-1` unknown op, `-2` denied, `-3` bad arguments, `-4` unavailable, `-5` not found, `-6` failed), or below `-16`: the buffer was too small, call again with `out_cap = -rc - 16`.

Build the C examples with their `build.sh` (system `clang`, wasm64 by default, `--wasm32` for 32-bit). `mods/build.sh` builds C++ plugins as wasm64 with Emscripten (`--wasm32` with wasi-sdk). The client runs both on WasmEdge; the server runs both on WasmEdge when built with the client, and 32-bit modules on wasm3 when built alone.

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
mods/                 the mod API (future theperlinproject/perlin-api), buildable on its own: cmake -S mods -B build
  api/                the schema: 00_tags, 10_core, 20_types, 30_events, 40_client, 50_server (.papi)
  codegen/            generate.py and the perlin_codegen package (parser, validator, C, C++, JSON, docs and host backends)
  cmake/              PerlinApi.cmake: include it to get perlin_api, the host sources and the checks
  host/               the host core shared by the client and the server: WasmEdge and wasm3 runtimes, WASI shim, event calls, subscriptions
  sdk/                perlin.h and perlin_abi.h (C), perlin/ (C++; perlin.hpp pulls in everything, perlin/gen is generated)
  docs/API.md         the generated reference
  build.sh            builds a C++ plugin (wasm64 with Emscripten, --wasm32 with wasi-sdk)
  barrel_roll/ waila/ minimap/   plugins
  examples/c/         a plugin in plain C
  tools/              perlin_settings.py (manifest -> typed handles), check_api.py (every op and event is hosted)
  tests/              abi, meta, plugin and sdk tests, and the C header test
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


## Reference

The full reference, generated from the schema, is [docs/API.md](docs/API.md).
