# Perlin v7: shield icon, HUD paperdoll, creative scrollbar

This source update builds on v6 and retains a separate Conduit dependency.

## Code changes

- `ShieldItemIcon.hpp` now projects the existing shield entity atlas onto a front plate plus narrow side faces, with the GUI rotation from the supplied Minecraft Java shield item model (`15, -25, -5`). This replaces the 7-pixel-wide vertical strip from v5/v6. No Java textures are bundled. The actual shape must still be visually compared in-game.
- `HudScreen::DrawPaperDoll` now positions the doll using the actual `hud_player_renderer` rectangle supplied by the HUD layout, bounded by the GUI viewport. It remains composed with the HUD, before the other HUD controls/debug overlay, rather than being moved to the world or a persistent foreground overlay.
- Creative scrollbar dragging preserves the pointer offset within the thumb, allows track clicks to jump, updates the thumb position from the current drag before rebuilding the scene, and retains wheel-based row scrolling. No inventory transactions are emitted for scrolling.
- The standalone creative-scroll regression previously used invalid C++ namespace member syntax (`sc.Wheel` instead of `sc::Wheel`). This test is corrected.
- The shield icon regression now also checks that the projected preview is visibly wider than a collapsed vertical strip and remains upright.

## Validated locally

- Five standalone test executables compile and run: shield item icon, creative scroll, creative catalog, catalog order, and builtin camera preset names. The catalog suite reports 20 checks.
- Full client CMake configure is still blocked fetching `perlin-api` from GitHub. No Windows/DX12 build or in-game visual validation completed for this archive.

## Not established by these checks

- Exact Java or Bedrock shield pixels (the existing Bedrock texture atlas is used with Java GUI rotation), exact first-person hand geometry, or correct rendering for every custom shield texture.
- Bedrock creative ordering for a server that supplies an incomplete or differently ordered `CreativeContent` catalog. The client preserves the authoritative server catalog; a screenshot alone cannot recover its full item/variant sequence.
- Camera effect rendering for `/camera` when the server sends no corresponding instruction. The supplied singleplayer logs showed an empty CameraPresets catalog, but no corresponding singleplayer /camera instruction was captured. Multiplayer logs show CameraInstruction packets without decoded field detail. The packet-side trace switch `PERLIN_TRACE_CAMERA=1` remains available.
- Performance improvement or Windows FPS gain. This patch focuses on visual and input correctness, not new renderer benchmarks.

This is a targeted tested source patch, **not** a claim that the full requested rendering/camera overhaul is finished.
