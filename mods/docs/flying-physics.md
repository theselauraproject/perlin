# Native flying physics

Reference: the iOS Minecraft 26.52 `minecraftpe` image already loaded in IDA
(`minecraftpe.i64`, image base `0x100000000`). This is native client/singleplayer
code. It is not a claim that the entire multiplayer prediction scheduler has
been reproduced.

The port now follows these recovered callbacks:

- `PlayerFlyingSpeedSystem`, `0x1070F0C74`: ability 6, float slot 0, doubled
  while sprinting. Explicit zero ability speeds remain zero.
- `VerticalFlySpeedControlSystem`, `0x10704C838`: raw horizontal input selects
  hovering below `0.01f`; creative idle vertical velocity is multiplied by
  `0.375f`. Up, up-slow, down and down-slow contribute `+0.15f`, `+0.05f`,
  `-0.22f`, `-0.15f` in that order. Simultaneous ordinary up/down writes
  positive zero. Ability 7, float slot 1, scales acceleration. Multiply and
  addition round separately. Game type 5 inherits the world's creative mode.
- `MobMovementFriction<PlayerFlyingTravelComponent>`, `0x1070E076C`:
  hover friction overrides use `(coefficient * override) * 0.91f`.
  Without that override, `minecraft:air_drag_modifier` scales both friction
  losses before their individual clamps. The native small-velocity epsilon
  check and signed zero multiplication are retained.
- `PlayerFlyingVerticalDragSystem`, traversal `0x1070D925C`: movement
  attribute offset 64 is `minecraft:air_drag_modifier`, established through
  the attribute map at `0x105F49030` and descriptor initialization at
  `0x10ACA67FC`. The default loss is `0.39999998f`; attribute-scaled loss
  is clamped and fall distance is reset. Scalar vertical and SIMD horizontal
  comparisons have different unordered-value behavior, kept separately.

`physics_selftest` includes 256 bit-exact outputs captured by executing the
original vertical-control ARM64 callback in Unicorn 2.1.4. The synthetic ECS
fixture provides an existing friction component, ability slot 1, a flying
request, actor modes 0/1, and a creative external world. Only pool lookup and
external world-mode access are hooked; flight arithmetic and branches execute
from the original instructions. The fixture covers every combination of the
four vertical inputs, hovering, creative mode, zero/nonzero speed and two
initial velocities.

Another 216 fixtures execute the original friction callback without hooks,
using synthetic sparse component pools. They cover absent/present hover
overrides, absent/zero/positive/negative air drag, ground coefficients and
positive/negative velocities on both sides of the epsilon cutoff. Expected
float bit patterns are checked into `tests/fixtures`; executable bytes and
reference extraction scripts remain outside tracked source. An 80-tick
replay test additionally checks deterministic local flight across input
changes; it is not an end-to-end native scheduler comparison.

## Remaining parity work

The complete flight path is not yet demonstrated 1:1. Native flight-trigger
intent/action callbacks (`0x10677FCC8`, `0x106781068`) have been recovered,
including their seven-tick repeated-trigger window and queued ability request.
Their component ownership, expiry and ordering still need integration with
client input sampling and server ability reconciliation. Slow flight inputs
are represented in the physics input but do not yet have a user control
binding. The grounded-flight workaround at `0x1070CA964` writes the smallest
positive float when grounded vertical velocity is zero; its scheduler position
and denormal handling must be established before integration. Full native
multi-tick collision, correction/replay and flight-mode transition comparisons
remain necessary. No percentage or zero-correction guarantee is inferred from
isolated arithmetic fixtures.
