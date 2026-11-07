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
