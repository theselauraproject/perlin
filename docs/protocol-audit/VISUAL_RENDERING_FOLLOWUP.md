# Visual rendering follow-up (November 27, 2026)

This source-only follow-up is based on the separated v3 Perlin repository. Conduit remains an entirely separate dependency; no bundled Conduit code is included.

## Implemented

- **Paperdoll safe-area positioning**: the small portrait is anchored to an in-bounds top-left position rather than inheriting a generic HUD renderer rectangle whose anchor may be at the screen edge. Its render scissor covers the viewport instead of truncating the bottom of the doll.
- **First-person shield**: prefer the Bedrock resource-pack attachable geometry when the asset is present; fall back to the existing geometric approximation if it is missing. The original branch always forced the approximation.
- **Stair geometry seams**: avoid independently shrinking both half-cuboids of a stair, which creates visible seams between their shared faces. Non-stair thin geometry keeps its prior inset.
- **Java-pack shadows on low-tier GPUs**: when no explicit `PERLIN_PACK_SHADOW_INTERVAL` is set, refresh shadow maps every third frame rather than every second on a low-tier device; all other tiers stay at every second frame. This trades some shadow update smoothness for lower GPU work, not necessarily higher overall FPS. The existing user override remains authoritative.
- **Stair direction audit**: Bedrock stair direction values represent the full-height side (0 east, 1 west, 2 south, 3 north). The original placement mapping was correct and was retained rather than introducing an opposite-facing regression.

## Not resolved / not verified

- The inventory creative catalogue is received in the server's order; the shown singleplayer catalog and item preview graphics require native creative ordering and resource-asset reference comparisons. No reordering was made without such evidence.
- The provided singleplayer log shows a zero-entry `CameraPresets` message; it does not demonstrate that any `CameraInstruction` command was sent during the session. `/camera` effects cannot be verified from that log, and this patch does not fix them.
- Vanilla item model appearance for every item, all stair corner and UV variants, shader-pack frame timing, and DX12 hardware performance were not validated.
- Full CMake configuration cannot complete in this environment because the project's `perlin-api` dependency must be fetched from GitHub and DNS resolution fails. Do not treat this as a successful compiled build.

## Checks

- The existing BlockUse stair state mapping was compiled and exercised at the five cardinal/negative cardinal yaw samples.
- ZIP CRCs, source path inventory, and archive-integrity validation were run.

## Shader performance context

The uploaded log reports Intel UHD Graphics with Java shader packs BSL and SuperDuperVanilla, capped 1024px shadow maps, and occasional 62-70 ms chunk-mesh jobs. This is not a controlled before/after benchmark. The built-in upscaler is off, according to the log.
