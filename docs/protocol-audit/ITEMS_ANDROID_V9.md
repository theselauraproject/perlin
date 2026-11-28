# Perlin v9: Java item rendering, Android networking and logging, HUD doll layer

Everything here was checked against the Minecraft Java 26.3 client JAR (`version.json` id 26.3): the item
definitions and models under `assets/minecraft/items` and `models`, and the bytecode (`javap`) of
`ItemTransform`, `ItemTransforms$Deserializer`, `ResolvedModel.findTopTransform`,
`ItemStackRenderState$LayerRenderState`, `ModelPart$Cube`, the special renderers and their models.
Nothing from the JAR is redistributed; `item_display.json` holds numbers only.

## Item rendering

### Display transforms (`tools/gen_item_display.py`, `resources/data/items/item_display.json`)

The table is regenerated with the Java client's own resolution rules. The old generator merged the
fields of a transform across the parent chain, which Java never does.

- A model file's missing left-hand transform takes that same file's right-hand transform
  (`ItemTransforms.Deserializer`); a context then takes the first transform found walking up the parent
  chain (`ResolvedModel.findTopTransform`), whole, with missing fields defaulting to rotation 0,
  translation 0, scale 1. Translation and scale use the same clamps as Java.
- `gui` transforms are now recorded (the old table dropped them), together with `gui_light`.
- Every item gets `hand`, `ground` and `gui` rows. Items that change model while in use get a
  `hand_use` row (shield to `shield_blocking`, bow, crossbow, trident, goat horn).
- The `transformation` on any node of an `items/*.json` tree is composed down to the leaf and stored
  as the model's local matrix. Examples: the shield's `scale(1, -1, -1)`, the conduit's
  `translate(0.5, 0.5, 0.5)`, the shulker box's flip.
- Dyed beds (composite head and foot models) are marked `special:bed` (see below).

`ItemTransform.apply` mirrors left-hand transforms itself: it negates translation x and rotation y
and z. The v8 notes said the left-hand JSON translation is "not mirrored a second time". That was
wrong according to the bytecode, and it was one cause of the misplaced off-hand shield.

### One code path for every item

`itemdisplay::Pose(t, leftHand)` is `ItemTransform.apply`: the transform, then `translate(-0.5)`, with
`NO_TRANSFORM` only recentring. The first-person, third-person, ground, HUD-doll and GUI paths all use
it, so they agree with each other and with Java.

- **Flat items stay flat.** Items Java draws from `item/generated` or `item/handheld` (bell, doors,
  signs, torches, ...) never pick up a 3D block mesh. Before, the bell was drawn as a block cube.
- **Block items** use the Java inventory mesh, as before. Their transforms now come from the correct
  table, which fixes stairs, fences, walls, trapdoors and other models that inherited mixed fields.
- **Special items** are built from the Java model definitions (`sprites/SpecialItemModels.cpp`):
  - shield: `ShieldModel`;
  - conduit: the closed `ConduitRenderer` shell;
  - chests (normal, trapped, ender, copper);
  - shulker boxes;
  - banners (pole, bar and dyed flag);
  - mob heads and the player head;
  - copper golem statues;
  - dyed beds.

  Cubes reproduce `ModelPart.Cube` exactly: the same polygons, vertex order and texture rectangles,
  including mirror. The decorated pot and the trident keep their previous paths.

### Shield

| View | Before | Now |
|---|---|---|
| First person | Missing `translate(-0.5)` and an extra 180° Y turn put the plate across the middle of the screen. | Exact `ItemTransform.apply`: lower corner of the screen, mirrored for the off hand. Raised, it switches to `shield_blocking`'s transforms. |
| Third person and paper doll | Drawn through an attachable with the transform baked into the vertices and no recentring. | Drawn by the Java held-item layer (`translateToHand`, X -90°, Y 180°, `translate(±1/16, 0.125, -0.625)`) with the `thirdperson_*` transforms. Only the hand that holds the raised shield uses the blocking model. |
| Inventory icon | A hand-written projection. | Rendered from the model with the JAR's `gui` transform `(15, -25, -5)`, translation `(2, 3, 0)`, scale 0.65 and front lighting. Special-model icons are baked with a margin around the slot (a 90 px image whose middle 60 px is the slot), and `Canvas::Item` widens the rect to match. A model that reaches past its slot, as Java's shield, banners and beds do, is no longer cut off. |

### Inventory icons (`UiAssets::ItemIcon`)

Block and special items are rasterised with their own `gui` transform. Before, every block used one
fixed `(30, 225)` view and was recentred afterwards. Stairs and fences now face the way they do in Java,
slabs sit low in the slot, and chests, banners, heads, shulker boxes, beds and the conduit show their
real models.

## HUD paper doll under the debug screen

The HUD doll draws to `UiLayer::HudDoll` (`View::HudDoll`), right after the HUD's own controls: it is part
of the HUD. The F3 debug screen draws to a new `UiLayer::Debug` (`View::Debug`) after it, so the debug text
covers the doll. The debug text is wrapped in a scene box tagged `layer: "debug"`; that layer applies to
the box's subtree only. The inventory preview doll stays on `UiLayer::Doll`, above its GUI.

The overlay pass is `Hand, Gui, HudDoll, Debug`. On frames that draw the doll, `SplitOverlayStages`
starts a new pass at `HudDoll` with cleared depth (the doll is 3D, and the hand's depth comes from
another projection). Otherwise the doll view is removed and nothing changes.

## Android HTTPS: platform stack first, OpenSSL as fallback

`io.perlin.client.PerlinHttp` uses `HttpURLConnection`, so Conscrypt TLS, the system and user CA
stores, Private DNS, per-app VPNs, proxies and the network security config all apply. It handles
redirects (POST becomes GET on 301, 302 and 303), decodes gzip and deflate, and streams downloads to
file with progress and cancel through a shared direct buffer. `Http_android.cpp` calls it over JNI:

- it resolves the class through the activity's class loader, so worker threads can find it;
- it keeps every call in its own local reference frame.

`http::Request` tries this path first and falls back to the OpenSSL client only when the request did
not complete. The fallback is logged.

`download::Fetch` (updates, fonts, sample archive) and the per-file bedrock-samples download use the
same native-first path.

## Android logging

- **Native crashes are recorded.** Bionic has no `backtrace()`, so the glibc handler never ran on
  Android. A `sigaction` handler now writes the signal, code, fault address, thread id and name, and an
  unwound stack with library offsets and symbols to `perlin.log` and logcat. It then returns the signal
  to the previous handler, so debuggerd still writes its tombstone.
- **Everything reaches logcat.** stdout is `/dev/null` in an Android app. The log reader now also writes
  each line to logcat under the tag `Perlin`, with crash, error and warning priorities.
- **The previous log is kept** as `perlin.prev.log`, so the launch made to read a crash log no longer
  overwrites it. This applies on every platform.
- **SDL messages** go through the game log with `[sdl-*]` tags.
- **New lines** for the background and foreground transitions, low memory and termination. A log that
  stops after "entering the background" is the system stopping the app.
- **Boot lines** now name the device, Android version and API level, ABI, core count and the native
  library path, in place of the meaningless `exe dir /system/bin`.

## Rendering performance

- **Empty overlay passes are skipped.** An overlay pass with nothing to draw and no colour clear is
  dropped; on tiled GPUs each pass is a full-screen tile load and store. The HUD doll's extra pass exists
  only on frames that draw it.
- **Mesh workers on fast cores.** On Android, chunk-mesh workers are pinned to the mid and big cores.
  The efficiency cores and a lone prime core, which is left to the game thread, are excluded. The 60-360 ms
  `slow chunk mesh` lines in the attached log are what meshing on efficiency cores tends to produce;
  this has not been measured on a device.
- **Less mesh churn for dropped items.** A shading state seen once (a spinning dropped item changes it
  every frame) is drawn from the frame ring instead of allocating, then evicting, a persistent mesh
  every frame. Only a state that comes back gets a buffer.
- The glint strength environment lookup is no longer repeated for every enchanted draw.

## Validation

- `tests/special_item_model_selftest.cpp` replaces the removed shield-icon test. It checks:
  - the first-person shield bounds for both hands (lower corner, below the crosshair, in front of the
    camera);
  - the conduit shell bounds;
  - the chest's bounds and latch side;
  - `ModelPart.Cube` texture rectangles and normals.

  It was built and run here against a minimal glm stand-in, because the real dependencies could not be
  fetched.
- The GUI rasteriser was run on the shield, conduit and chest models; the outlines match the Java icons.
- `PerlinHttp.java` was compiled and run against a local HTTP server: gzip decoding, 307 POST, 303 to
  GET, error bodies and file download with progress.
- The Android crash, logcat and device code, and the JNI bridge, were syntax-checked against stub
  Android headers and the JDK's `jni.h`.
- **No full build was possible here:** SDL3, glaze, httplib, glm and Conduit could not be downloaded.
  No Android device or APK build was available either. Compile the full tree and run the tests before
  merging.

## Round 3: stairs, item audit, snow, paperdoll scale

- **Icon lighting.** Stairs (and every 3D block icon) looked wrong because the left face was darkest.
  RasterIcon and BakeCubeIcon now use Java's exact `Lighting.ITEMS_3D` / `ITEMS_FLAT` vectors and the
  `min(1, 0.6*(l0+l1) + 0.4)` shade. A Python re-render with the jar's textures matched Java for stairs,
  fences, walls, furnace and snow.
- **Bedrock → Java item ids.** `itemdisplay::JavaItemName(name, aux)` maps Bedrock ids to the Java ids
  used by item_display.json and the IMOD meshes (snow → snow_block, snow_layer → snow,
  stone_stairs → cobblestone_stairs, nether_brick → nether_bricks, aux beds/banners/skulls, …).
  Previously Bedrock `snow` resolved to the Java snow *layer*, so the held snow block was a sliver.
- **Snow tint.** BlockLibrary::ApplyTints never applies a `tint_color` to snow or ice textures; the
  pack's tint on the snow texture turned snow blocks, layers and their icons water-blue.
- **Audit.** Every block-kind Java item has a model mesh except air, big_dripleaf, small_dripleaf,
  heavy_core and spyglass; decorated_pot and trident keep their previous renderers.
- **Paperdoll Scale** slider (0.5–2.5, Video options, after "Always Show Paperdoll"); anchored top-left.

## Round 4: block-item icons, flat items, leather

- **Block items ignored their Java model.** Every placeable stack carries a block runtime id ("#id" in
  the icon key) and ItemIcon sent those keys to the placed-block path: a lone fence post, a world-facing
  stair viewed at (30, 225), a backwards shelf. ItemIcon now always asks the Java item model first —
  special renderer, the IMOD inventory mesh with the item's own gui transform, or a flat sprite — and only
  falls back to the block-state render for items with no Java model. Re-rendering the IMOD meshes with
  the jar's textures gives Java's stairs (profile on the left), fence (two posts, two rails), shelf and
  wall icons.
- **Lookup double-mapping.** UiAssets passed already-Java ids back through JavaItemName (Java `snow`
  became `snow_block`, `stone_stairs` became cobblestone). `itemdisplay::LookupJava` takes Java ids as is.
- **Flat items.** Items whose Java model is a flat sprite (lantern, soul lantern, chain, ...) use the
  pack's item texture (by Bedrock id, then the Java texture name) in the GUI and in the hand, never the
  block render.
- **Leather armour.** Undyed leather icons were left grey: they now take Java's default #A06540. Worn
  leather armour is coloured with the dye (`customColor`, read from MobArmorEquipment for other players
  and from the inventory for the local player) or the default brown, through the colour-mask bake.

## Round 5: plants and leather follow-up

- **Saplings, flowers, grass, rails, panes, ...** are flat items in Java (their block texture as a
  sprite). With no Bedrock item texture they fell through to the placed-block render (a 3D cross); the
  GUI and the hand now use the block's flat sprite (tinted like the plant) for every Java flat item.
- **Leather item icons** follow Bedrock's change-colour convention, as worn armour does: opaque pixels
  are dyed (default #A06540), nearly transparent pixels are the undyed overlay shown as is. The previous
  rule had it backwards, leaving most of the helmet, leggings, boots and horse armour white. Leather horse
  armour is dyed too.

## Round 6: whole-catalogue audit (`tools/audit_items.py`)

`python3 tools/audit_items.py --jar <client.jar> --sheets <dir>` checks every item and renders every
block item's icon with the client's icon maths and the jar's textures (192 per sheet). Results for 26.3:

- **Paths.** 1658 Java items: 819 block (inventory mesh), 761 flat, 78 special. Block items without a
  mesh: big_dripleaf, small_dripleaf, heavy_core (old path). Special without a builder: decorated_pot.
- **Names.** All 2029 Bedrock ids were checked against JavaItemName. The 351 without a Java item are
  non-items (double slabs, wall signs, fluids, portals, candle cakes) or Bedrock/Education-only items
  (elements, hard glass, chemistry tables, border/allow/deny, colored torches); they keep Bedrock icons.
- **Geometry.** The 815 rendered block icons match Java's layout (stairs, fences, gates, walls,
  shelves, trapdoors, buttons, plates, carpets, anvils, lecterns, grindstones, ...).
- **Textures.** Two pixel-matcher mistakes were fixed in block_models.bin and recorded in texalias.json:
  loom top (was lectern top) and the sandstone / red sandstone sides (were the bottom texture; Bedrock's
  side is `*_sandstone_normal`), for icons and placed blocks. Still fuzzy, needing the Bedrock pack to
  regenerate: dried_ghast body faces and the dried kelp block's bottom.
