# Plugin host

How Perlin runs plugins. What a plugin can call, the manifest, the build steps and the event model are defined by [perlin-api](https://github.com/theperlinproject/perlin-api) and documented there; this page covers only what Perlin, as the host, does with them.

## Engines

| Platforms | Engine |
|-----------|--------|
| Windows x64, Linux, macOS, Android | WasmEdge (wasm32 and wasm64 modules) |
| Windows on ARM, FreeBSD, iOS, web | wasm3 (wasm32 modules only) |

Ship wasm32 modules if the plugin should run everywhere.

## Lifecycle

`ModManager::Start(dir)` scans `dir` for mods. Each mod is a folder (`mods/<name>/`) or a zip (`mods/<name>.zip` or `.perlinmod`), the same way texture packs and shader packs are laid out. A zip may hold the files at its top level or inside one folder; it is unpacked into `mods/.unpacked/<name>/`. Inside, the module is the `.wasm` named by `main` in `manifest.json` (or `mod.json`), or else the first `.wasm` in the folder; `manifest.json` and `pack_icon.png`/`icon.png` (or the manifest's `icon`) are optional. A loose `<name>.wasm` left directly in `mods/` is moved into `mods/<name>/` together with its `<name>.json` (which becomes `manifest.json`) and `<name>.png`. Each module gets its own worker thread and instance.

A plugin is in exactly one of these states (`ModInfo::state`):

| State | Meaning |
|-------|---------|
| `approval` | the manifest asks for a dangerous permission the user has not approved; nothing runs |
| `disabled` | switched off by the user |
| `loading` | module is being instantiated |
| `running` | loaded and receiving events |
| `unresponsive` | an event ran past its budget, or loading never finished; the instance was interrupted and its commands, key binds, layers and textures were dropped |
| `error` | the module is invalid or `init` failed; `ModInfo::error` has the reason |

Changing a `.wasm` or its manifest on disk reloads that plugin (`SetAutoReload` turns this off). A failure in one plugin does not stop the others: events are delivered to each plugin separately, and a plugin that blows its time budget is stopped alone.

## Resource ownership

Everything a plugin registers (commands, key binds, UI and graphics layers, textures, input grabs, settings) is stored in that plugin's record and cleared when it is revoked, unloaded or reloaded. Plugins read and write files only under their own storage root; paths from the guest are validated as relative before use.

## Permissions

Permissions that the host classifies as dangerous (`ModManager::DangerousMask`) must be approved by the user. The approved set is stored per plugin and passed in with `SetApproved`; a manifest that later asks for more goes back to `approval`.

The sandbox is the WebAssembly one: a module can only reach the host through the registered ops, and ops check the caller's permissions. Perlin does not limit CPU beyond the per-event time budget, and this page makes no claim about memory limits.
