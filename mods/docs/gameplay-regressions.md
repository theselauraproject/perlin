# Gameplay regression checks

The regression branch uses local fixtures and terminal tests. No Hive connection
is required. Flying and elytra work are excluded from this follow-up.

## Custom block boxes

Server definitions can encode boxes with `minX/minY/minZ/maxX/maxY/maxZ` in
sixteenths. Resource-pack definitions use bottom-centred `origin` and `size`.
`ParseBlockBoxComponent` accepts both forms, including multiple boxes and
explicit disabled components. Partial, non-finite, overflowing and degenerate
boxes are rejected. These paths compile with strict floating-point checks.

Custom `minecraft:selection_box` definitions and conditional permutations now
reach `LevelCollision::Selection` separately from physical collision. An empty
explicit selection remains unselectable. Fallback selection queries terrain
only: nearby actor contact boxes must not change a block's selected face.

`block_box_definition_selftest` covers a thin pad, a two-box dropper, equivalent
network/pack encodings, disabled components and malformed bounds. The local
Metal outline capture uses a supplied network-format pad definition through the
production parser, registry, selection query and raycast. It is a fixture, not
a capture from a public server.

For a repeatable renderer fixture, supply a JSON object mapping block names to
components through `PERLIN_PREVIEW_BOX_FILE`. Use the normal offline preview
flags and `PERLIN_PREVIEW_AIM=1` to render the selected shape without gameplay
input. Existing vanilla render geometry can serve as a local visual stand-in.

## Pose clearance

Standing/crouching probes now use native binary32 bounds and a `0.01f` inset
instead of `0.0001`. The native producer is documented in the local swimming
investigation: PlayerBoundingBoxStateUpdateSystem callback `0x10610532c` in the
26.52 client. Small wall/support overlaps must not lower the pose. Physics tests
cover shallow wall/support overlaps, actual wall penetration and a low ceiling
that admits crouching but rejects standing. The prior crawl-entry guard remains.

## Chunk lifetime and decoding

LOD eviction only removes columns for which request-mode recovery metadata
exists. Inline columns previously disappeared inside the server's streaming
radius without a way to request them again.

Inline column decoding is transactional. All advertised subchunks must decode
with valid, nonrepeated heights before data is applied and the column marked
complete. Failed decoding queues bounded subchunk recovery and retains column
metadata. Truncated data cannot be silently converted into complete empty
terrain. Tests include valid multi-subchunk data, every truncation of its second
subchunk, repeated/out-of-range heights, excessive counts and empty columns.
Live revisit/recovery testing is still required for broad network validation.

## Native skeleton pose

The native bone Y rotation used the opposite sign, turning the vanilla bow
pose's left wrist away from the bow. It now turns toward the other hand.
Regression checks use the vanilla humanoid bow angles and an independent
forward-vector/head-yaw check. The Java player-pose adapter is adjusted to keep
its world transforms unchanged; its numerical JAR fixtures and independent
matrix checks remain enabled in CTest. This changes native model rotation,
including attachables, so broader resource-pack visual checks remain useful.

## Touch input

Touch input logic is separated from rendering so its real event paths can be
exercised without a graphics context. Entering/exiting layout editing releases
held movement, jump, sneak and pointer input before clearing finger ownership.
Done preserves the edited layout until the caller takes and saves it; a stale
editing-state refresh no longer discards that result. Editor changes no longer
rebuild an unchanged layout every frame. Desktop
editor transitions are processed even without touch mode enabled.

The overlay no longer adds desktop F-key/debug/player-list shortcuts to its
system-button row. Several idle sprites used colliding button-enum values;
they now select the intended jump/sneak/attack/sprint textures explicitly.
`touch_input_selftest` exercises editor transitions between update/event calls,
stick movement, jump, menu pointer ownership and cancellation. This validates
the implemented joystick/crosshair path, not all Bedrock Android control modes
or Android hardware behavior. `PERLIN_TOUCH=1 PERLIN_PREVIEW_TOUCH=1` allows a
controlled desktop renderer capture of the touch overlay.

## Remaining verification

Placement/creative-breaking debounce and sword guards are present from the
initial regression commit. Creative breaking now processes the initial press
immediately instead of requiring a 50 ms hold; repeat cooldowns still apply. Real client/server click cadence, skeleton shooting
and release timing, combined chunk/network failures and Android device testing
still require verification. Renderer screenshots demonstrate geometry and
supplied states; they cannot prove network timing or absence of intermittent
failures. No overall completion or native-parity percentage is claimed.
