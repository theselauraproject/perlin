# Java inventory-model geometry follow-up (v9)

The supplied Java 26.3 client JAR was used as an input **during generation only**. The source archives do not contain that JAR or raw Java texture images.

## Changes

- `tools/modelgen/modelgen.py` can now bake the separate `models/block/*_inventory` mesh selected by Java's item model definitions, as well as ordinary block model items.
- `tools/modelgen/merge_inventory_models.py` merges those meshes with the previous `block_models.bin` **without altering any of its 20,328 world-state hash records**. The new optional `IMOD` footer is read by `BlockModels` and ignored by older PBM1 readers.
- `UiAssets::ItemIcon()` prefers the distinct Java item mesh for un-hashed items. Hashed block-state icons continue using their state-specific model.
- `HandRenderer::ItemModel()` now uses actual Java item geometry and a small per-item texture atlas for held/dropped model-backed blocks, rather than generic cube/stair/fence-box approximations. Other item paths retain the existing fallback.
- The model selftest verifies representative items' geometry and texture indices, rather than relying only on a random hash sample that previously found zero quads.

## Validation

- Re-generated inventory model data from the supplied Java JAR: 831 item meshes.
- Merged binary: 20,328 preserved world-state hash records. The merge tool byte-compares the state records after writing.
- Built and ran standalone `models_selftest`: passed; checked 109 representative quads.
- Compiled the standalone model loader and verified resolution of fences, stairs, fence gates, walls, and planks.
- Python portability/source check passed.

## Limitations

This is not full Minecraft renderer parity. It does **not** fix the detached cat parts or distorted llama geometry, or guarantee bell and conduit special renderers, torch icons, animations, or resource-pack behavior. Those require checking the actual entity geometry/animation definitions and Java special renderers under a running client. Not all Java model textures have exact Bedrock texture equivalents, and the existing fallback remains for unresolved tiles.

The complete Windows and Android builds were not run. The build environment cannot download all CMake dependencies from GitHub, and an Android GL context/device is unavailable here. No FPS improvement is claimed for these changes.
