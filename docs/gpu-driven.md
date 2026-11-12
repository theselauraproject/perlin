# GPU-driven terrain

The opaque terrain layers (solid, cutout, cutout-cull) are culled on the GPU and drawn with indexed indirect draws
whose count the GPU writes (`draw_indexed_indirect_count`). The CPU only touches a chunk when it is meshed, replaced
or dropped.

## How a frame runs

1. **Records.** Every non-empty (chunk section, layer) pair is one 80-byte record in a persistent GPU buffer
   (`GpuTerrain`). A chunk owns a contiguous block of records. When a chunk is remeshed only its block is
   re-uploaded; nothing else is rebuilt. The CPU keeps a mirror so the buffers can grow without a GPU copy or a
   `wait_idle`.
2. **Early cull** (`terrain_cull.slang`, one thread per record): render radius, cave visibility (the CPU flood fill
   uploads one bit mask per chunk), frustum, then *facing groups*. Writes compacted draws per (page, layer) bucket and
   counts them.
3. **Draw** the early list: one indirect-count call per vertex page and layer.
4. **Occlusion** (when the world pass has a depth target): a depth pyramid is built from that depth
   (`hiz_build.slang`, kept in a buffer, reversed Z), then a late cull tests every record against it. Sections it finds
   newly visible are drawn in the `WorldLate` view, and the visibility bits feed the next frame's early pass. This is
   the two-pass scheme, so a section that comes out from behind a hill is drawn the same frame.
5. Translucent layers stay on the CPU path: they need back-to-front order. The CPU now walks only the chunks that
   have translucent geometry.

### Facing groups

The tessellator stores each section's solid and cutout-cull quads grouped by facing (+x +y +z, other, -z -y -x).
When the camera is behind every plane of a group in that section (with a 0.5 block margin), the group is skipped.
With the camera outside a section's slab on an axis, that axis's back-facing group never reaches the vertex
shader. On the benchmark world this removes about 45–48% of opaque triangles.

## Shadows

The opaque shadow casters (solid, cutout, cutout-cull) are culled against the light's box by the same compute pass
(mode 3 of `terrain_cull.slang`), over one quarter of the records per frame while a shadow map is rebuilt, and drawn
with indirect-count draws. Glass (the coloured-light passes) stays on the CPU list, batched into multi draws.

## Entities, particles and UI

Every UI, entity, particle, weather, cracks and nametag draw carries its per-draw values (model matrix, alpha cutoff,
overlay, glint, material) in a 160-byte instance on vertex stream 1 instead of in the uniform block. The uniform block
is then the same for all of them in a view, so it is written once instead of once per draw. Neighbouring draws with
the same pipeline, textures and buffers become one multi draw. World-space draws (entities, particles, weather) are
culled by `ui_cull.slang`: each draw's bounding sphere against the frustum, culled draws keep their slot with no
instances so the order stays exact.

## Fallbacks

| Condition | Path |
|-----------|------|
| multi draw indirect + draw indirect count (Vulkan 1.2+ or `VK_KHR_draw_indirect_count`, D3D12) | GPU-driven |
| no indirect count (Metal, WebGPU, D3D11, GLES, old Vulkan) | CPU culling, multi draw from the ring when available |
| no depth target for the world pass | GPU culling without occlusion |
| `PERLIN_NO_GPU_CULL=1` | CPU culling |
| `PERLIN_NO_OCCLUSION=1` | GPU culling without the depth pyramid |
| `PERLIN_NO_FACE_CULL=1` | GPU culling without facing groups |
| `PERLIN_NO_GPU_SHADOW_CULL=1` | shadow casters from the CPU list |
| `PERLIN_NO_GPU_UI=1` | UI, entity and particle draws one by one (still instanced, no multi draw, no GPU cull) |
| `PERLIN_NO_GPU_ENTITY_CULL=1` | UI batching without the frustum cull pass |
| `PERLIN_<FEATURE>=1` (`GPU_CULL`, `OCCLUSION`, `FACE_CULL`, `GPU_SHADOW_CULL`, `GPU_ENTITY_CULL`, `GPU_UI`) | forces that feature on and keeps the auto-tune from testing it |
| `PERLIN_NO_AUTOTUNE=1` | no measuring: every feature on, occlusion off on integrated GPUs |
| `PERLIN_AUTOTUNE_RESET=1` | measure again, ignoring the saved result |
| `CONDUIT_SYNC_PIPELINES=1` | pipelines compile on the render thread (Vulkan and Direct3D 12) |
| no multi draw indirect with first instance | UI draws one by one |

## Auto-tune

Which of these features pay off depends on the GPU: occlusion culling, for one, costs about 1 ms a frame on Intel UHD
graphics and saves time on discrete cards. So Perlin measures them on the machine it runs on (`RenderTuner`).

- Every feature starts on, except occlusion on integrated GPUs.
- About ten seconds after a world has loaded, each feature is switched on and off in alternating blocks of 30 frames,
  eight times each. Alternating cancels out changes in the scene, and the medians ignore hitches. The picture does not
  change while this runs: these are culling choices, not visual ones.
- The terrain sub-features (occlusion, facing groups, shadow culling) are measured before GPU culling itself, so one
  costly sub-feature cannot make GPU culling as a whole look slow.
- Frame-to-frame time decides. When it ties (vsync or an FPS cap hides the work), GPU time from timestamp queries
  decides. A feature is turned off only when that is at least 3% faster on frame time, or 5% on GPU time.
- The result is written to `render-tune.txt` in the data folder, one section per GPU, driver and API, and applied at
  once on later launches. A driver update or another GPU measures again. The log shows each decision under `[tune]`.

## Work graphs (Direct3D 12, opt-in)

`PERLIN_WORK_GRAPHS=1` runs the early cull as a D3D12 work graph (`src/shaders/hlsl/terrain_cull_graph.hlsl`). One
node rejects whole chunks, and the survivors launch a small grid over just their own sections. It writes the same
draws as the compute pass, so drawing and occlusion are unchanged.

Requirements:
- the D3D12 renderer;
- a GPU and driver with work graphs tier 1.0 (shader model 6.8);
- a build that had DXC (the Windows build fetches it).

The log says what happened, in lines starting with `[workgraph]`. If anything is missing, the compute path runs
instead.

## Measuring

- `render_bench <pack> [radius] [frames] [remesh per frame]` runs the terrain frame on the null device. It reports
  the CPU cost and the triangles each path would submit.
- `gpu_terrain_selftest` checks the records, the facing groups and the culling.
- `terrain_cull_cpu_selftest` runs the real cull shader (compiled to C++ by Slang) against the CPU reference,
  including the occlusion passes.
