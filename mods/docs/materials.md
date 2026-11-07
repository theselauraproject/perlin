# RenderDragon renderer and game materials

The default renderer compiles and executes pixel programs from
[veka0/mcbe-shader-codebase](https://github.com/veka0/mcbe-shader-codebase),
`release/obfuscated` at `bf4c3467b384f74a13e045f759f0f21e837c5536`.
CMake downloads a pinned, SHA-256 verified archive into its dependency cache.
Vanilla `.material` files, material lists, and the superseded local game shader
bodies have been removed. No network access is needed at runtime.

## Shader compilation

`tools/gen_renderdragon_shaders.py` reads the upstream GLSL fragment programs
and generates bgfx shader sources under `build/generated/renderdragon`.
It adapts shader interfaces, samplers, precision qualifiers, and scalar
constructors for bgfx's cross-platform compiler. The pixel-program calculations
come from the upstream files rather than local replacement material bodies.
`manifest.json` in that directory records each generated fragment's upstream
family/pass. CMake compiles these programs for the selected graphics backends
and embeds the resulting binaries in the executable.

The small vertex bridges adapt Perlin's geometry to the upstream interpolants:

- Terrain keeps Perlin's texture-array layers, light bytes, CPU biome tint, and
  chunk visibility; upstream `RenderChunk.Opaque`, `AlphaTest`, and `Transparent`
  perform the actual shading. Atlas sampling still handles Perlin's mip/RGSS
  texture-array interface.
- Sky, clouds, stars, sun/moon, sunrise, and screen color use the corresponding
  upstream color/texture programs. Vertex bridges supply color, UV, and fog
  inputs for Perlin's existing sky geometry.
- Entity meshes remain skinned on the CPU and carry lighting/tint in their
  vertex colors. The renderer selects upstream Entity pixel programs for
  alpha testing, color masks, emissive shading, and additive glint.
- Particles, weather, cracks, and name-tag backgrounds select their own upstream
  programs instead of sharing the UI fragment shader. Weather keeps Perlin's
  existing CPU roof occlusion and lighting, supplied through adapter inputs.
- Textured UI and name-tag glyphs use upstream `UIText.Transparent` with the
  true-type, alpha-test-off variant, preserving vertex tint and alpha.

`UiDraw.material` selects a draw domain. `Auto` chooses entity rendering for
world, hand, and doll views and UI rendering for screen views. Explicit particle,
weather, cracks, and name-tag domains are set by their draw producers.
Fog is enabled for world geometry and disabled for hand/doll/UI/name tags.
Glint remains a separate additive draw, so its upstream constant base is black.
Render-state compatibility (blend, depth, culling) stays in the renderer because
upstream publishes shader code rather than complete pipeline state metadata.

Engine postprocessing, temporal/upscaling effects, the shadow-map encoding
pass, and optional user-selected shader packs retain their own programs.
The renderer does not activate the upstream deferred/PBR pipeline or GPU
skinning; those are separate rendering paths from Perlin's forward renderer.

## Material names and packs

`tools/gen_material_catalog.py` discovers paired upstream vertex/fragment GLSL
files and generates `MaterialCatalog.hpp` in the build directory. Source passes
are registered as `Family.Pass`, such as `RenderChunk.Opaque`, with case
insensitive lookup. Compute and fragment-only programs are excluded.
`MaterialDb.cpp` maps legacy names used by draw calls and entity render
controllers to those source identities and Perlin's compatibility render states.
Fancy/fast clouds retain their separate blending behavior.

Resource-pack `materials/*.material` overrides still load after the defaults.
They may inherit an upstream pass (`custom:RenderChunk.Opaque`) or a legacy
name. Pack definitions take precedence over generated defaults. Unknown entity
names fall back to the upstream-backed `entity_alphatest` alias.

## Building and validation

For an offline or custom upstream checkout:

```sh
cmake -S . -B build -DPERLIN_MCBE_SHADER_SOURCE_DIR=/absolute/path/to/mcbe-shader-codebase
```

The checkout must have the release/obfuscated directory layout. Missing required
passes fail configuration. To update upstream, change the pinned revision and
archive SHA-256 in `cmake/Materials.cmake`. The shader-only ESSL build uses the
same generator; the CI shader cache includes both the generator and dependency
pin in its key.

With `PERLIN_BUILD_TESTS=ON`, run:

```sh
cmake --build build --target Perlin material_selftest
ctest --test-dir build -R '^(material_selftest|renderdragon_shader_selftest)$' --output-on-failure
```

The shader test compiles all generated programs for the native build profiles
plus ESSL and checks vertex/fragment interface hashes. It can also test every
portable profile directly:

```sh
python3 tests/renderdragon_shader_selftest.py \
  --source /path/to/mcbe-shader-codebase \
  --shaderc /path/to/bgfx/shaderc --bgfx-include /path/to/bgfx/src \
  --profiles metal 430 300_es spirv
```
