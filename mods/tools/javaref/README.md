# Java worldgen reference harness

Ground truth for the vanilla terrain port: it runs Mojang's own worldgen classes (from a client jar you provide) and prints
what the real engine computes, so the C++ port in `server/` can be compared number for number.

Nothing from the jar is stored here; the tools read your copy of the jar.

What it does
- `Prep.java` rewrites the jar's classes so they load on a JDK older than the one they were built for (class version patched to 65),
  makes each static initialiser failure-tolerant, drops calls into the serialization/networking libraries (Codecs are not needed to evaluate
  terrain), and swaps the pattern-switch bootstrap for a compatible one (`jh/Boot.java`).
- `Scan.java` + `Stubs.java` list the library classes the jar references and generate empty stand-ins for them.
- `Relocate.java` + `helper/` provide the few fastutil types the noise code uses (built from the shaded copy inside a Kotlin compiler jar when no
  real fastutil jar is at hand; with a real fastutil jar on the classpath none of this is needed).
- `helper3/` has the commons-lang3 Mutable* types.
- `Oracle.java` fakes the registries, runs the jar's own `NoiseData.bootstrap` and `NoiseRouterData.bootstrap`, builds a `RandomState` for a seed and
  evaluates the overworld router (temperature, vegetation, continents, erosion, depth, ridges, chunk surface level, final density) at block positions.

- `BiomeDump.java` runs the game's `OverworldBiomeBuilder` and prints every (climate box, biome) row: `overworld_biomes_26_3.txt` (7,594 rows), which `server/tools/gen_biome_tables.py` bakes into the server.
- `BiomeLookup.java` asks the game's parameter list which biome 30,000 climate samples land in (half on box edges), printing its brute-force answer: `biome_lookup_ref.txt`, which `server/tests/vanilla_biome_test.cpp` compares against. Its r-tree and brute-force search disagree on about 5% of those samples, all ties on shared edges (the r-tree starts from the last result on its thread), so the brute-force answer is the reference.
- Both need the biome classes and `Climate` re-patched with `Prep` so that `com.mojang.datafixers.util.Pair` keeps working (it is exempt from the neutralising rule; `helper/pair/Pair.java` is a minimal one).

`reference_seed12345.txt` is its output for seed 12345 (ore vein functions are skipped: they need the block registry).
`run.sh` shows the JVM flags (large young generation: the patched initialisers crash the JVM if a GC happens while they run).

On a machine with a real JDK 25 and the game's libraries this is simpler: put the client jar and its libraries on the classpath and run `Oracle.java`
directly; none of the rewriting is needed.
