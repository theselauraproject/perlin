# Validation record — combined source bundle v3

Perlin base: `0329b0cc4b1c21482b46cf275bd8a5eafc8aaeab`. Bundled Conduit base: `5e94c96d7e0e52ba0b072aa5248329bedeeaca2e`. Validation date: November 27, 2026.

## Completed build and tests

The modified **Linux Perlin application compiled and linked successfully**, together with all **37 targets** selected by the Windows compatibility workflow. All **57 selected CTests passed, with zero failures and zero skips**, in 3.85 seconds on this host. A separate `handler_dispatch_selftest` run with `PERLIN_TRACE=1` and `PERLIN_OFFHAND_EXPERIMENT=1` also passed, in 0.06 seconds.

The final build and execution use the same frozen set of 1,200 source/configuration inputs. The complete final test output is retained in [test-results-v3.txt](test-results-v3.txt), with [JUnit results](test-results-v3.xml) and [trace/offhand JUnit results](test-results-v3-trace.xml). Older v1/v2 records are historical, not the validation of this combined bundle.

| Area | Executed regression evidence |
| --- | --- |
| Input and movement reporting | `input_reporting_selftest`: 44 checks; `movement_protocol_selftest`: 210 checks, including failed-send retention and versioned outgoing packets |
| Movement effects and correction history | `movement_effects_selftest`: 234 checks; `attribute_replay_selftest`: 150; `status_effect_replay_selftest`: 88; standalone physics and status-effect tests pass |
| Ability layers | Actual handler dispatch covers all 720 permutations of six layers in each of 944/2193/2225, plus duplicate replacement, zero speeds, actor filtering and resets |
| Eating, charging, projectiles and inventory prediction | `player_interaction_selftest`: 104 checks; `ranged_item_selftest`: 129; `item_use_packets_selftest`: 554; `item_nbt_selftest`: 73 |
| Placement input | `heldbuild_selftest`: 80 checks, including duplicate-down suppression and intentional held building |
| Older inventory wire forms | `item_use_wire_selftest`: 78 independent checks; main/offhand container addresses and optional transaction fields |
| Older chunk wire forms | `chunk_wire_selftest`: 302 independent checks; request sentinels, cache hashes, both entry layouts, payload presence, both heightmaps and signed request offsets |
| Creative inventory | `creative_catalog_selftest`: 20 checks; real packet-to-screen `creative_inventory_selftest`: 45 |
| Camera | `camera_wire_selftest`: 493 checks; `camera_effects_selftest`: 175; actual packet-to-consumer `camera_effects_packets_selftest`: 310 |
| Sound and server UI | Sound wire: 207 checks; audio packet: 42; server UI packet: 183; PCM audio, director, HUD feed and HUD screen tests pass |
| Metadata and session codecs | Independent 2193 metadata: 19 checks; session codec context: 21; generated packet, variable-integer and dispatch regressions pass |
| Chunk storage and publication | `chunk_ingest_selftest`: 868 checks; `chunk_pipeline_selftest`: 378; chunk codec, packed cells, undo and benchmark-fixture correctness tests pass |
| Terrain visibility | `section_connectivity_selftest`: 1,907 checks against an independent reference; real `ChunkMeshStore` visibility/cache test: 260 checks |
| Complete mesh output | `mesh_exactness` passes with checksum `466c21ae50e4e75b`; full and partial edit cases agree |
| Conduit | Common CPU lifetime/binding checks, null backend, render profile and vendor tests pass |
| Tooling and startup | Seven protocol-audit Python tests and linked application resource/startup test pass |

The selected CTest names and build targets are recorded in [workflow-verification-v3.json](workflow-verification-v3.json). All names were checked against the actual generated Ninja targets and CTest registrations. This validates the workflow's requested targets against the source configuration; it is **not an executed Windows build**.

## Regression evidence from the failing implementations

The final independent chunk-wire fixture was compiled against the preserved pre-fix codec library. It failed **110 of 302 checks**. The same fixture passes **all 302 checks** with the corrected generator. The changes affect bridges 944/975/1001/2168; isolated generation found no change to schemas, generated packet codecs, or 2193/current output. The [regression record](chunk-wire-regression-v3.json) identifies the fixture and baseline library hashes.

The older inventory bridge failed 38 of 78 independent literal-byte checks before its correction; the final bridge passes 78. The original 2193 prediction/metadata bridge failed 14 of 19 literal-byte checks; the final bridge passes 19. The sound-tag regression reproduced 126 failures in 207 checks before the full-width guard; the final test passes 207. The glide correction's original isolated physics test failed the three new vertical-velocity assertions before the production change.

The first combined chunk-pipeline run also exposed a fixture error: hashed palette mode was active, but two intended air edits used runtime ID 0. In that mode the air hash maps to local zero, while runtime ID 0 does not. The fixture now asserts that distinction and sends the actual air hash. Its exact liquid-clear, unchanged-block and neighboring-waterlogging assertions were retained; the production overlay logic was not weakened to satisfy the fixture.

During integration, the mesher benchmark's existing `Options.cpp` dependency also exposed missing SDL target linkage. The benchmark and the two related texture/block utility targets now link `SDL3::SDL3`, supplying both its headers and library through the normal cross-platform target.

## Performance evidence

The [performance report](../performance/README.md) contains completed baseline/candidate CPU measurements, raw runs, checksums and source or binary hashes. It covers packed chunk decoding, section preparation/publication, stationary and moving visibility selection, the connectivity kernel, complete meshing, and Conduit resource retirement/binding lookup.

The full-mesher comparison produced the same vertex checksum across four ABBA runs, zero differences across 1,589,856 merge sample points, and matching full/partial results in all ten edit cases. CPU timing improvements apply to those recorded workloads. Windows Direct3D 12 GPU time, whole-game FPS and the user's device were not measured here.

## Source, evidence and compiler checks

- `git diff --check` and `tools/check_portability.py` passed on the final tree.
- All **104 evidence files** match the refreshed SHA-256 manifest.
- Both packet inventories match their final schema digests and contain no missing active IDs or catalog name mismatches. Protocol 2193 has 246 definitions covering 231 active reference IDs; the current schema has 254 definitions covering 239 active IDs. Both include 15 retained legacy definitions and have no opaque codec fallbacks.
- The previous v3 integrated build used the then-bundled modified Conduit. This split tree uses the separate Conduit repository instead; the earlier validation is historical and does not constitute a new cross-repository build.
- The generated compiler database passes 24 checks across eight simulation sources. Strict floating-point rules remain in force. The [compiler-check result](../performance/simulation-compile-options.json) and Windows workflow preserve this gate.
- The identity checks for the supplied executable/PDB/catalog remain intact. The game binaries were inspected as data and were not executed or included in the source archive.

Catalog enumeration, generated codecs, literal byte fixtures, state assertions and native behavioral equivalence are different results. No test count or packet count is presented as a percentage of complete Bedrock support.

## Build configuration and reproduction

This host used GNU C++ 14.2.0, CMake 4.4.5, Ninja 1.13.2 and Linux x86-64. The existing `unix-ninja-fast` preset was configured with tests enabled, bundled Conduit enabled, `PERLIN_FAST_MATH=OFF`, `PERLIN_SINGLEPLAYER=OFF`, and `SDL_UNIX_CONSOLE_BUILD=ON`. The latter accommodates the host's missing display development packages. Four compiler jobs were used. Dependency updates were disabled in the already-populated local FetchContent cache.

```sh
cmake --preset unix-ninja-fast -B build/compatibility \
  -DPERLIN_BUILD_TESTS=ON -DCONDUIT_BUILD_TESTS=ON \
  -DPERLIN_CONDUIT_DIR=../conduit -DPERLIN_FAST_MATH=OFF \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
  -DPERLIN_SINGLEPLAYER=OFF -DSDL_UNIX_CONSOLE_BUILD=ON
python tools/check_simulation_compile_options.py build/compatibility/compile_commands.json
```

The complete 37-target build command and 57-test selection are in [the Windows instructions](README.md#windows-build) and `.github/workflows/protocol-compatibility-windows.yml`. Use the Windows `windows-clang-cl` release preset on that platform; the workflow includes the actual Windows build, tests and application packaging. It produces the `Perlin-protocol-compatibility-windows-x64` artifact after those gates pass.

## Additional targeted checks and practical limits

The chunk-ingest test passed its 868 checks under AddressSanitizer and UndefinedBehaviorSanitizer. The connectivity reference test and Conduit common CPU tests also passed targeted sanitizer runs. LeakSanitizer was disabled because of the container's tracing setup. Earlier variable-integer/callback and physics sanitizer checks are retained in the historical validation records. The full final application was not built with sanitizers.

The completed execution is on Linux without a display or GPU. The Windows workflow, native Direct3D 12 changes and source review do not constitute a Windows execution result. No controlled differential run of the supplied Minecraft client and Perlin against the user's custom server was performed. These repairs and passing regressions therefore do not certify every Bedrock state, universal anticheat acceptance, or an external service's official-client classification.
