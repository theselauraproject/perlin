# First-party plugins

Plugins written and shipped with Perlin, built against the [perlin-api](https://github.com/theperlinproject/perlin-api) headers. They are also the largest examples of the API.

```
waila/       block and entity tooltip (has an extension API of its own, see waila/README.md)
minimap/     minimap
barrel_roll/ free elytra flight
examples/    c (plain C plugin), hooks_demo, sketchpad, barrel_roll (small version)
prebuilt/    built .wasm files with their manifests and icons
build.sh     builds one: PERLIN_API=/path/to/perlin-api plugins/build.sh [--wasm32] waila/waila.cpp
```

`PERLIN_API` can be omitted once Perlin has been configured: the script then uses the copy CMake fetched into `build/_deps`.

`barrel_roll` is a port of the Do a Barrel Roll mod: free elytra flight with mouse and keys, banking, thrust, smoothing and a full settings page. It uses the `look` event (raw mouse and key input every frame) and `game::set_look` to steer the camera, and needs the `control` permission. The prebuilt module is `prebuilt/barrel_roll.wasm` with `barrel_roll.json` and `barrel_roll.png`.

`minimap` is a minimap: relief-shaded terrain (slopes lit from where the sun or moon is, so shadows move through the day; higher ground lighter), water darkened by depth, a cave view when you are underground with its own zoom multiplier, stepped zoom (0.5 to 6) that eases between steps, day and night, an optional chunk grid with your chunk outlined, the vanilla map player marker, waypoints (B adds one, your death point is marked) and rotate-with-player. The whole map is one texture and everything on it, grid and markers included, is one indexed draw call; columns come from the host's `game::map_chunk` (surface colour, height and water depth per column, only re-read when a chunk changes). No entities are shown. Build it with `plugins/build.sh --wasm32 minimap/minimap.cpp`; the prebuilt module is `prebuilt/minimap.wasm` with `minimap.json` and `minimap.png`.

`waila` is a full plugin built on this API: a tooltip for the block or entity under your crosshair, styled after Jade (icon, name, mod name in italic blue, harvest tool with a check mark, breaking progress, crop growth, redstone, health hearts, pet owner, horse stats, container contents). Its source is the best example of the API, and it has an extension API of its own; see `waila/README.md`. The prebuilt module is `prebuilt/waila.wasm` with `waila.json` and `waila.png`; drop the three into `<data dir>/mods/`.
