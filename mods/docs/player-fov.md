# Movement FOV

The movement target follows the Minecraft Windows 26.52 native client
(SHA-256 `6001a63a1033977a87bfe06db7d9f932f5334f1fe695d6f32e1fa497db061f73`).
The mapped `LevelRendererPlayer::getFov` entry is RVA `0x46BD250`, with image
base `0x140000000`.

`LocalPlayer`'s target producer at `0x144762630` reads the current
`minecraft:movement` attribute and the walk-speed ability. Sprint's attribute
modifier is already included in that current value. Applying another 1.3
multiplier widened the view twice after the server updated the attribute.
The native ordinary movement arithmetic is, in scalar float order:

```
ratio = movement / walkSpeed
ratio *= 1.2
ratio += 1
ratio *= 0.5
target = (flying ? 1.1 : 1) * ratio
```

The native slowness branch instead uses `max(0.01, 1 + amplifier * -0.1)`
for the movement term. The flight factor is shared by both branches. The
old arbitrary 1.5 target cap is removed. Invalid network movement/ability values
fall back to a finite target instead of contaminating the camera matrix.

Renderer tick instructions at `0x1446a8295` save the old multiplier and move
halfway toward the target once per tick. `getFov` at `0x1446bd31c` interpolates
the old/current multipliers using the render partial tick. Perlin now does the
same at 20 Hz and samples after the current frame's player update. Render frame
rate no longer changes the smoothing trajectory.

## Validation

`tests/player_fov_reference.hpp` contains numerical outputs from execution of
the original native arithmetic instructions with Unicorn 2.1.4, x86-64 Windows
register layout and MXCSR `0x1f80`. No executable bytes are committed.

- 24 targets execute `[0x144762871, 0x144762891)` with movement in XMM6,
  walk speed in XMM0 and the flight factor in XMM7.
- 10 slowness targets execute `[0x1447627ad, 0x1447627d5)`, with the supplied
  amplifier at `[RAX+0x20]` and flight factor in XMM7.
- 29 consecutive renderer updates execute `[0x1446a82ad, 0x1446a82cd)`,
  preserving current state at `[RSI+0xf10]`. The preceding native previous-state
  copy is supplied at `[RSI+0xf14]`. Each update is sampled at five partial ticks
  by executing `[0x1446bd31c, 0x1446bd338)` with partial tick in XMM6.

The self-test requires exact output bits, exercises sprint start/release,
movement speed changes and flight transitions, and checks that additional
render samples do not change state. It also guards against the former duplicate
sprint multiplier and target cap.

```sh
cmake --build out/memory-release --target Perlin player_fov_selftest
ctest --test-dir out/memory-release -R '^player_fov_selftest$' --output-on-failure
```

For controlled renderer comparisons, `PERLIN_PREVIEW_FOV_MOVEMENT` accepts
`movement,walkSpeed,flying,slownessAmplifier` in offline preview mode. It passes
those supplied values through the production target helper. These captures
illustrate the steady-state projection, not live server attribute timing.

This validates movement/slowness/flight arithmetic and renderer interpolation.
It does not establish complete native camera parity: bow use, camera modes,
underwater/death effects and the complete client attribute-prediction lifecycle
require separate coverage.
