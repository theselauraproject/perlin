# Inventory rendering and camera follow-up (November 27, 2026)

This update applies to standalone Perlin only. Conduit remains an external repository dependency, not a bundled source tree.

## Changed

- The shield inventory icon no longer compresses a rectangular segment of the 64x64 shield entity atlas into an opaque 8x14 sprite. Instead it samples the atlas plate UVs into a narrow, slanted 32x32 preview with a transparent background and visible edge. It is intended to be closer to the inventory reference image. In-hand shield attachable geometry from v4 remains unchanged.
- The fallback stair item mesh now builds the raised stair section on the far half of the local Z axis instead of the right half of X. This affects item previews and fallback item models, **not** world placement or stair collision.
- When the server sends no camera presets, Perlin registers the four documented built-in names (`minecraft:first_person`, `minecraft:third_person`, `minecraft:third_person_front`, `minecraft:free`) for its local camera-state consumer. Nonempty server catalogs remain authoritative and retain their wire order. This is useful for Perlite singleplayer servers whose previous log showed `CameraPresets` with zero entries, but it does not prove that Perlite emits the correct `/camera set` packet or runtime IDs. No arbitrary client replies are introduced.
- New standalone `shield_item_icon_selftest` and `builtin_camera_presets_selftest` targets run basic shape, buffer-bounds and name-order checks.

## Confirmed existing behavior

- Creative tab order and icons are set to Construction, Equipment, Items, Nature, Search; entry order and group icons are passed through from the server's creative-content packet. The existing 20-check creative-catalog test passes. An accurate vanilla category arrangement still depends on Perlite sending the correct groups and item ordering; this patch does **not** replace the vanilla catalog.
- Stair `weirdo_direction` layout in the world mesher follows the four directional state values and was not changed. An inventory-preview orientation is a separate concern from placement and world block states.
- The Java shader path already has low-tier shadow-resolution limits and a default three-frame shadow update interval. No new whole-game FPS gain was measured in this pass. The provided log identifies large chunk-mesh jobs as well as expensive shader passes, not a single conclusively measured bottleneck.

## Validation and limitations

- `shield_item_icon_selftest`: passed.
- `builtin_camera_presets_selftest`: passed.
- `creative_catalog_selftest`: passed, 20 checks.
- Full application build: **not verified**. CMake configuration was blocked by failure to resolve `github.com` while fetching `theperlinproject/perlin-api`.
- No Windows Direct3D 12 execution, native-client visual diff, Perlite `/camera` command run, or Java-shader GPU benchmark was executed. Shield preview UV appearance and stair preview need on-screen confirmation on the target game assets.

This update does not establish full visual parity or that all requested rendering/performance issues are resolved.
