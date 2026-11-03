# Java (OptiFine / Iris) shader packs

Perlin runs OptiFine and Iris shader packs on its own renderer. It follows Iris 1.11.7 for Minecraft 26.3.

## What runs where

| Renderer   | Graphics programs | Compute, custom images, SSBOs |
|------------|-------------------|-------------------------------|
| Vulkan     | yes               | yes                           |
| Direct3D 12| yes               | yes                           |
| Metal      | yes               | yes                           |
| Direct3D 11| yes               | no: packs take their fallback path |
| WebGPU     | no                | no                            |

The features a renderer has are announced to the pack the way Iris does it. `shaders.properties` sees every
`IRIS_FEATURE_*` flag. The shaders only see the flags listed in the pack's `iris.features.optional`. A pack whose
`iris.features.required` lists a missing feature refuses to load and says which feature is missing.

Packs see Perlin as Iris (`IS_IRIS`, `IRIS_VERSION 11107`, `IRIS_TAG_SUPPORT 2`). They also get the real OS
(`MC_OS_WINDOWS`, `MC_OS_MAC`, `MC_OS_LINUX`) and the GPU vendor and renderer macros.

## Pipeline

1. `ShaderPackInstance` reads the pack. It scans options and preprocesses `shaders.properties`, expanding macros
   in values the way Iris does. It also keeps the unpreprocessed file for screens, sliders, profiles and feature
   flags.
2. `TransformProgram` and `TransformCompute` turn each program into Vulkan GLSL:
   - uniforms go into a block;
   - samplers are split into textures and samplers;
   - custom images go to set 2;
   - storage buffers go to set 3.
3. glslang compiles the GLSL to SPIR-V. SPIRV-Cross then makes HLSL (D3D) or MSL (Metal).
4. `JavaPack` creates the pack's buffers, images and SSBOs. It then runs:
   - setup, once;
   - begin;
   - shadow;
   - shadowcomp, only when a shadow pass exists;
   - prepare;
   - gbuffers;
   - deferred;
   - translucent;
   - composite;
   - final.

   Each pass runs its `.csh` / `_a`…`_z.csh` compute shaders before its fragment program.

   The geometry passes follow Iris:
   - shadow: clear (shadow colour to `shadowcolorNClearColor`, white by default), `shadow_solid`,
     `shadow_cutout`, `shadow_entities`, `shadow_block`, copy `shadowtex0` to `shadowtex1`, `shadow_water`;
   - gbuffers: sky, `gbuffers_terrain_solid`, `gbuffers_terrain_cutout`, entities, spider eyes, lightning,
     armor glint, block entities, block breaking, outlines, particles, the hand, deferred, `gbuffers_water`,
     translucent entities and block entities (sign text), translucent particles, clouds, weather, the
     translucent hand.

   Every gbuffers and shadow program gets the alpha test of its render type (0.5 for cutout terrain, 0.1 for
   entities, particles and the hand, above 0 for water, glint and spider eyes), unless `alphaTest.<program>`
   sets another. As in Iris, it runs only for programs that write `gl_FragData[0]`. `blend.<program>.<buffer>`
   overrides blending per draw buffer, and `renderStage`, `alphaTestRef`, `entityId`, `blockEntityId` and
   `currentRenderedItemId` are set per draw.

## What packs read about the world

Iris feeds packs live values, and so does Perlin:

- the biome at the eye, with its id, category and precipitation, numbered as Iris numbers Java's biomes
  (`BIOME_*`, `CAT_*` and `PPT_*` are defined for shaders and custom uniforms);
- the light at the eye (`eyeBrightness` / `eyeBrightnessSmooth`);
- the level clock, rain, thunder and wetness, with the pack's half-lives;
- `far` as the render distance in blocks;
- the sun path tilted by `sunPathRotation`;
- vanilla ambient occlusion in the vertex colour (in the alpha with `separateAo`);
- block light emission in `at_midBlock.w`;
- block ids from `block.properties` with block state filters (`lantern:hanging=true`, `%leaves` tags). Bedrock
  states are turned into Java ones, and the first matching entry wins as in Iris;
- entity and item ids from `entity.properties` / `item.properties` (Bedrock names mapped to Java's);
- the light where each mob, item and particle stands. Mesh colours stay plain so the pack does the lighting.
- the dimension. The nether and the end load the pack's `world-1` / `world1` programs and get Java's dimension
  values: no skylight (sky light 0 everywhere), `hasCeiling` and 0.1 `ambientLight` in the nether, the height
  limits, the fixed sun angle, the nether's thick fog, no sky in the nether, and the textured end sky
  (`gbuffers_skytextured`, CUSTOM_SKY). `PERLIN_DEV_PACK_DIMENSION=1|2` runs those programs in any world for testing;
- the vanilla sky as Iris draws it: sky, sunrise fan, sun and moon fading in rain, stars only at night (coloured by
  their brightness through `gl_Color`), and the void disc below the horizon.

Direct3D 11 has 16 sampler registers a stage; packs declare more, so textures read with the same sampler state
share one sampler variable there. `jshader_check -b hlsl` checks that and the Direct3D 12 stage linkage.

The renderer (Vulkan, Direct3D 12/11...) changes on the next launch; the options show "restart to apply".

Full-screen passes (composite, deferred, final...) get Iris's quad: `gl_Color` is white, and `texture` /
`gtexture` / unbound samplers read colortex0.

`jshader_check <pack> -u minecraft:plains` prints the pack's custom uniforms for a biome.

## Checking a pack offline

`jshader_check` is built with `PERLIN_BUILD_TESTS=ON`. It sends every program of every dimension through the
same path, for SPIR-V, HLSL and MSL:

    jshader_check <pack folder or zip>              # default options
    jshader_check <pack> -P all                     # every profile
    jshader_check <pack> -r 16                      # 16 random settings of the options the screens show
    jshader_check <pack> -o COLORED_LIGHTING=512    # set options
    jshader_check <pack> -p composite -b msl -v     # one program, one backend, dump the output to /tmp

Complementary Reimagined and Unbound r5.9.3 and Sildur's Vibrant 2.02 pass on all three backends:

- default options;
- every profile;
- colored lighting with world-space reflections.

The only failures in random sweeps are option combinations the packs themselves do not compile in, such as
Complementary with `RP_MODE=1` and `IPBR_EMISSIVE_MODE=1`.

## Measuring

`JSHADER_GPU_TIMES=1` logs, every 120 frames, the gpu milliseconds of each part of the pack's frame (shadow,
terrain, translucent, every composite pass by name) and the cpu time spent recording it. `JSHADER_STATS=1` counts
render passes and feedback copies. `JSHADER_VIA_HLSL=11` (or `12`) runs the hlsl the Direct3D 11 (12) backend
gets on Vulkan, to check the Direct3D shader path on a machine without it.

## Settings

Options > Shader Pack Settings follows the pack's own layout:

- sub-screens, with a Back row;
- `<profile>` rows, which cycle through the pack's profiles;
- sliders for the options in `sliders=`;
- value labels from `value.<option>.<value>`, plus `prefix.` / `suffix.`;
- tooltips from `option.<name>.comment`.

Changes are saved to the pack's `.txt` file and applied with Apply Changes.

## Not done yet

- Geometry shaders (`.gsh`) and tessellation. The engine's pipelines have no geometry stage, and Metal has
  none at all.
- Distant Horizons / Voxy programs (`dh_*`), which Iris also skips without those mods.
- Per-texture PBR maps from resource packs: `normals` and `specular` bind flat defaults.
- Block states Bedrock does not store (`waterlogged`, `snowy`) use Java's defaults.
