# Benchmark mode

A repeatable performance run: the same world, the same flight path and your current settings, including the
shader pack.

    Perlin --benchmark            # 60 s recorded, then quits
    Perlin --benchmark 120        # 120 s
    Perlin --benchmark --benchmark-seed 42 --benchmark-out result.json

You can also start a run from Options > Video > Run Benchmark (60 s). The result appears in the status line.

## What it does

1. It deletes `<data dir>/benchmark-world` and starts the embedded server with seed 8675309 in spectator mode.
2. Once the player has spawned, the camera flies a 96 block loop at 8 blocks/s. It climbs and dips 12 blocks
   and sweeps left and right, so every direction and chunk loading are covered.
3. The first 10 s are warmup (chunks load, pipelines compile) and are not recorded.
4. Every frame's wall time is recorded for the chosen length.

## Result

The result is written to `<data dir>/benchmarks/<date>.json` (or `--benchmark-out`) and printed to stdout. It
contains:

- average fps, and the 1% and 0.1% lows (average fps of the slowest 1% / 0.1% of frames);
- median, p95, p99, best and worst frame times;
- hitches: frames slower than both twice the median and 33 ms;
- renderer, GPU, resolution, render distance, vsync, fps cap and shader pack.

Turn vsync off and set the fps cap to unlimited for a raw number. Compare runs only when they used the same
settings.

## Chunk meshing

`perlin_meshbench` (built with the tests, from `tools/mesh_bench.cpp`) meshes a fixed synthetic terrain on the
CPU, without a window or a GPU, so it runs in CI and under valgrind:

    perlin_meshbench [radius] [rounds]                # time per chunk, vertices, a checksum of the output
    PERLIN_SAMPLES=<folder> perlin_meshbench 4 3      # with textures, tints and block models

`<folder>` holds `resource_pack/` (`blocks.json`, `textures/terrain_texture.json`, the textures) and
`metadata/vanilladata_modules/mojang-blocks.json`. Nothing of the game is shipped for this: point it at your
own copy, or at a small pack of your own.

Each run also checks itself, and fails (`FAIL ...`) when a check does not hold:

- **Merged faces draw the same.** Every chunk is meshed with face merging off and on, and every face of the
  unmerged mesh is sampled at four points inside each triangle. The merged mesh must cover each point and give
  it the same colour, light, AO and texture position. This is the pixel-exact test for merging: the GPU draws
  what those interpolated values say.
- **Partial rebuilds match full ones**, after ten kinds of block edit.

The checksum is of the vertices made in the first round. A change that only makes meshing faster must keep it.

To compare two builds, use `tools/bench_compare.py`. It runs them in interleaved rounds and reports medians.
With `--instructions` it counts the instructions spent meshing under callgrind instead of timing: the count does
not move with machine load, so it is the number to trust for small changes.

    tools/bench_compare.py old/perlin_meshbench new/perlin_meshbench --instructions --radius 2 --rounds 1
