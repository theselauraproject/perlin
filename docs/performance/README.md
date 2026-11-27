# Perlin and Conduit performance changes

This source bundle changes chunk decoding, section preparation, mesher visibility analysis, terrain selection and Conduit resource bookkeeping. Conduit changes are delivered as a separate source archive for the Conduit repository. Perlin fetches that repository independently; push the companion archive first or point `PERLIN_CONDUIT_DIR` at your separate checkout.

The measurements below are CPU workloads on Linux, compiled with GCC 14.2 and `-O3`. They establish reduced cost in the measured operations. They are **not measurements of Windows Direct3D 12, whole-game FPS, GPU time or every supported device**. The final integrated build and regression results are recorded in [VALIDATION.md](../protocol-audit/VALIDATION.md).

## Implemented changes

### Chunk decoding and publication

Palette index expansion uses fixed-width word loops for the supported storage widths. The previous loop repeatedly divided each of the 4,096 cell indices by the number of entries per word. The new loop consumes each packed word directly. Palette bounds, short storage, final partial words and the output prefix on a malformed palette index are retained. The change applies to both ordinary blocks and the extra liquid layer.

Section preparation transposes network-order cells and constructs compact block/liquid storage before taking the world mutex. A short commit swaps the prepared storage into the section, preserving its biome data. Identical updates keep the existing version and dirty state. Changed updates retain undo logging, version increments, border increments and dirty-column coalescing.

The session prepares independent decoded bodies on its decode workers, then commits them in packet-entry order. All-air entries participate in that same order. A world generation and chunk incarnation prevent prepared data from applying after a reset, rewind, transfer, or removal and recreation at the same coordinates. Deferred blob data retains its original receive revision. A late cached column can fill untouched sections but cannot overwrite newer section data or publish stale block entities there. The chunk commit mutex coordinates publication with dimension resets and pruning; it does not enclose the expensive cell preparation.

Individual block, liquid and block-entity updates use the existing world-work queue as well. For a pending cached reply, later cell/layer edits are coalesced in a bounded per-column record shared by that pending work. They are applied over the admissible old section before publication; newer block-entity positions suppress the corresponding old tail entries. This preserves a newer single-cell edit without throwing away the section's other cells. The record is allocated only when blob data is missing and released when its retained contexts are gone.

Container opening, sign editing and block-cracking events that read this world state retain their receiver-thread callbacks. They wait for a captured watermark of earlier queued work, rather than all future arrivals. Reset, stop and world-generation changes cancel the wait. This preserves the inventory lifecycle and avoids moving unrelated UI/audio handling onto the chunk worker.

These protections are part of the optimization: moving work outside a lock is only useful if the final write still belongs to the current world and chunk.

### Mesher section connectivity

The cave-visibility analysis copies the padded occupancy cache in contiguous rows. It starts flood fills only at section boundaries. An enclosed interior cavity cannot connect two section faces, so traversing that cavity cannot change the result. The kernel preserves the existing 15-bit face-pair layout, accepts every nonzero opacity byte as blocked, and keeps the previous out-of-range-air behavior for partial vertical windows.

An independent FIFO reference verifies the new implementation against empty/solid sections, sealed cavities, tunnels, walls, corners, random occupancy and partial cache windows. The tests also verify that neither dense nor padded input storage is mutated.

### Terrain visibility and draw runs

`ChunkMeshStore` retains two CPU selections: all terrain and translucent terrain. Each is keyed by the exact camera position and frustum bits, cull radius, cave mode, mesh epoch and cave epoch. Mesh uploads/removals, LOD changes, GPU resource recreation and mesh-format resets invalidate the descriptions. The time-gated cave update runs before cache lookup, so a delayed connectivity update is observed even with a stationary camera.

Draw runs are admitted to the cache only after a selection is reused. Moving or changing terrain therefore builds directly into the caller's output instead of building and copying an extra run vector. The old temporary per-frame chunk-pointer list is also removed. Caches contain CPU descriptions, not transient frame or GPU handles. Clearing the world returns their retained capacity.

This also fixes a rendering error: disabling cave culling no longer applies stale section-visibility masks left from when it was enabled. Tests compare full ordered draw runs and cover every cache-key component, face culling, LOD-only changes, uploads, removals, resource release/recreation, format changes and delayed cave refresh.

### Conduit and Windows release builds

Conduit's Direct3D 12/Vulkan retirement queue groups callbacks with the same frame-fence requirements, checks completion once per batch and recycles up to eight empty callback buffers. It still considers all ready batches, including out-of-order queue serials. Callback invocation and captured-resource destruction remain outside the mutex. Fence values, queue waits, barriers and destruction eligibility are unchanged.

Direct3D 12 bind layouts now build an immutable binding-number index. Bind-group creation uses that index instead of scanning the full layout once per entry. Small IDs use a bounded direct table and larger IDs use a sorted sparse table. Duplicate IDs retain the previous last-match result; missing IDs retain the existing error path. Frame-slot cleanup also reuses an emptied bind-group handle vector without overwriting concurrent entries. See [Conduit's change record in the companion Conduit archive](https://github.com/theperlinproject/conduit) for details and its standalone tests.

The Windows release optimization setup previously replaced source-specific options and could remove `/fp:strict` from simulation files. It now appends optimization flags, preserving those numerical rules. `tools/check_simulation_compile_options.py` validates the generated compiler database. The Windows workflow uses the optimized `windows-clang-cl` release preset with LTO, while keeping `PERLIN_FAST_MATH=OFF` and strict source options on simulation code.

## Measured CPU results

### Palette expansion and decoding

The benchmark processes a deterministic mixture of uniform, two-entry, 3-bit, 5-bit, wide and waterlogged sections. Each run uses 96 sections and 128 iterations. Both binaries were compiled before timing; each scenario ran three ABBA rounds, giving six runs per implementation. All corresponding output checksums agreed.

| Workload | Workers | Before, ns/section | After, ns/section | Lower elapsed time |
| --- | ---: | ---: | ---: | ---: |
| Palette expansion | 1 | 7,223 | 1,409 | 80.5% |
| Palette expansion | 4 | 3,029 | 518 | 82.9% |
| Decode | 1 | 9,679 | 3,749 | 61.3% |
| Decode | 4 | 3,572 | 1,055 | 70.5% |

The four-worker values divide the elapsed wall time by the total number of sections; they are throughput measurements, not the latency of an individual task. This comparison isolates the decoder change before the prepared-section pipeline. [Raw runs, checksums and binary hashes](chunk-decoder-abba-2026-11-27.json).

### Section preparation, world writes and readers

This separate comparison passes already-decoded cell arrays to `Level::SetSubChunk`. It includes transposition, compact-storage preparation, locking and publication, but excludes packet parsing. The baseline performs the preparation under the shared world mutex; the candidate prepares before taking the final commit lock. The deterministic fixture uses 96 sections and 128 iterations, with three ABBA rounds per scenario. Every final cell/liquid check and content checksum agrees.

| Writers | Concurrent reader | Before, µs/section | After, µs/section | Lower elapsed time |
| ---: | --- | ---: | ---: | ---: |
| 1 | No | 3.441 | 3.381 | 1.7% |
| 4 | No | 6.597 | 1.350 | 79.5% |
| 1 | Yes | 7.016 | 6.921 | 1.4% |
| 4 | Yes | 7.326 | 2.891 | 60.5% |

The single-writer difference is small. The clearest benefit is reduced contention among several writers and a reader. In the four-writer case, the median sampled reader p99 fell from 78.704 to 10.711 µs; the median of each run's largest sampled wait fell from 2.799 ms to 0.126 ms.

The reader runs continuously rather than at a fixed request rate, so its sample distribution changes when writers release the lock sooner. With one writer it completed a median 73,156 reads before and 1,489,222 after. Its p95 rose from 0.060 to 0.156 µs and p99 from 0.170 to 0.727 µs, while its largest sampled wait fell from 0.761 to 0.212 ms. These results do not support a claim that every reader percentile improves. [All throughput, read-count, percentile and checksum data](chunk-ingest-abba-2026-11-27.json).

### Terrain selection and draw-run construction

The same harness drives the original and modified `ChunkMeshStore` through Conduit's null backend. It uses 2,401 synthetic chunks and 2,000 frames per case, with an ABBA run order and CPU affinity fixed to one core. The benchmark's ordered-run checksum agrees across both implementations.

| Case | Before, µs/frame | After, µs/frame | Lower CPU time |
| --- | ---: | ---: | ---: |
| Stationary camera and unchanged meshes | 247.609 | 1.917 | 99.2% |
| Camera moving every frame | 246.140 | 199.234 | 19.1% |
| A mesh replaced every frame | 260.109 | 229.735 | 11.7% |

The stationary case demonstrates reuse of unchanged work; its large ratio must not be described as an FPS multiplier. In this fixture, owned cache capacity rose from 98,304 to 159,744 bytes for the stationary case, an additional 60 KiB. The fresh moving/churn cases retained 98,304 bytes in each implementation. After world reset, the modified selection caches retained zero bytes. [Full measurements, source hashes and memory counts](visibility-abba-2026-11-27.json).

### Mesher connectivity kernel

Each case processes 3,200 sections in ABBA order on one core. The reference and candidate produce identical connectivity masks.

| Occupancy fixture | Before, ms/run | After, ms/run | Kernel speedup |
| --- | ---: | ---: | ---: |
| Air | 17.986 | 3.337 | 5.39× |
| Solid | 19.180 | 3.338 | 5.75× |
| Sealed cavities | 129.602 | 5.853 | 22.14× |
| Terrain | 39.669 | 25.477 | 1.56× |
| Sparse | 125.056 | 9.711 | 12.88× |
| Dense | 43.345 | 8.510 | 5.09× |
| Random | 177.726 | 12.312 | 14.44× |

This is the connectivity step, not complete mesh generation. [Measurements and source hashes](section-connectivity-abba-2026-11-27.json).

### Complete meshing workload

The paired full-mesher benchmark measures 25 synthetic chunks across eight rounds per process, with an ABBA process order. Both executables link the same preserved dependency objects and use the original Level layout; only `ChunkMesher` differs. This isolates the mesher optimization from the separate section-ingestion changes.

| Measurement | Before | After | Change |
| --- | ---: | ---: | ---: |
| Median of each run's mean meshing time | 3.759 ms/chunk | 3.216 ms/chunk | 14.46% lower |

All four runs produced vertex checksum `466c21ae50e4e75b`, zero differences across 1,589,856 merge sample points, and matching full/partial results for all ten edit cases. The cases include chunk borders, water, leaves and relighting. The workload performs real meshing and lighting on an untextured synthetic scene. It does not measure GPU drawing or a resource-pack-heavy multiplayer session. [Raw logs, method and results](full-mesher-abba-2026-11-27.json).

### Conduit resource retirement

The same CPU harness is compiled against the original and modified Conduit headers. It runs 500 measured frames after warmup, then the comparison script repeats it in three ABBA rounds. Callback counts and checksums agree. Values below are medians across six runs per implementation.

| Releases per frame | Before, µs/frame | After, µs/frame | Before → after allocations/frame |
| --- | ---: | ---: | ---: |
| 64 | 2.114 | 0.721 | 7 → 0 |
| 512 | 16.951 | 5.463 | 10 → 0 |
| 4,096 | 140.552 | 46.130 | 13 → 0 |

The zero allocations are steady-state results for this workload after warmup, not a promise that the queue never allocates. First use, larger batches and distinct fence patterns can allocate. The report also contains dense/sparse binding-lookup microbenchmarks; those lookup timings do not include native Direct3D descriptor creation. [Full CPU report](conduit-cpu-results.json).

## Reproducing checks and measurements

The source bundle includes the harnesses, regression tests and raw data. No supplied Minecraft executable or PDB is required to run these performance checks.

```sh
cmake --preset unix-ninja-fast -DPERLIN_BUILD_TESTS=ON -DCONDUIT_BUILD_TESTS=ON
cmake --build build/unix-ninja-fast --parallel 4 --target \
  chunk_ingest_selftest perlin_chunkbench section_connectivity_selftest \
  chunk_visibility_selftest perlin_visibilitybench perlin_meshbench common_cpu_selftest
```

On Windows, substitute the `windows-clang-cl` preset and its build directory. The manually triggered **Protocol compatibility - Windows** workflow builds the client and the correctness targets. Its artifact contains the application after the configured checks pass.

`perlin_chunkbench --verify` checks the deterministic chunk fixtures. Its `--workload` choices are `expand`, `decode`, `commit`, `reader` and `all`; `--sections`, `--iterations`, `--workers` and `--reader-sample-rate` control the workload. `section_connectivity_selftest --bench` includes the original connectivity algorithm and the independent reference in one reproducible comparison. `tools/benchmark_visibility_pair.py --help` describes the paired visibility benchmark, and `tools/benchmark_mesh_pair.py --help` describes the complete-mesher ABBA comparison. `tools/compare_cpu_paths.py --help` (from the separate Conduit checkout) describes the Conduit comparison, including `--rounds`.

Use identical compiler settings and input data for baseline and candidate, finish compiling before timing, and run on an otherwise idle machine. The recorded source or binary hashes identify the measured inputs. Real device FPS should be measured separately with the same world, route, resolution, render distance and graphics settings; Perlin's existing [benchmark mode](../benchmark.md) provides its end-to-end frame-time procedure.
