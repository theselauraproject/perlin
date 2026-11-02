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

## Settings

Options > Shader Pack Settings follows the pack's own layout:

- sub-screens, with a Back row;
- `<profile>` rows, which cycle through the pack's profiles;
- sliders for the options in `sliders=`;
- value labels from `value.<option>.<value>`, plus `prefix.` / `suffix.`;
- tooltips from `option.<name>.comment`.

Changes are saved to the pack's `.txt` file and applied with Apply Changes.

## Not done yet

- Geometry shaders (`.gsh`) and tessellation.
- Distant Horizons / Voxy programs (`dh_*`), which Iris also skips without those mods.
- Per-texture PBR maps from resource packs: `normals` and `specular` bind flat defaults.
- `shadowtex0` and `shadowtex1` share one depth map, so translucent shadows are not separate.
