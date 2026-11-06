# Bedrock bridging

Placing blocks always follows Bedrock Edition's own rules. They were
read out of the client (`ClientInstance::_tickBuildAction`, `GameMode::startBuildBlock`, `continueBuildBlock`,
`stopBuildBlock` and `GameMode::useItemOn`) and live in `src/client/player/HeldBuild.{hpp,cpp}`. Reach-around for
fresh presses (placing against the block you stand on while aiming past its edge) lives in
`src/client/player/Bridging.{hpp,cpp}`.

## Fresh press

A new press places at the crosshair (or the reach-around target) immediately and starts a new build: the previous
block's kind and any build line are forgotten. A block is never placed into the player's own body.

## Held use

While use stays held, each attempt first picks what to click:

1. With a **build line** (see below), the next cell on the line, but only while the look ray (eye to the crosshair hit,
   or to full reach without a hit) passes through that cell.
2. Otherwise, with the crosshair directly on a block:
   - the crosshair block and face, unless the player is moving, the last block was snappable, no line exists yet and
     a block was already placed: then the **last placed block**, on the face along the player's movement this tick
     (dominant axis of the 3D movement, so vertical movement counts too).
3. Without a direct hit (and no line), nothing.

Held attempts are evaluated at most once per local simulation tick. A fresh press
marks that tick as attempted, so later render frames cannot repeat in the same tick.
The saved native reference records elapsed-time delays below; this tick gate does
not replace them with unverified integer tick delays.

It then waits for the repeat delay since the last placement:

| State | Delay |
| --- | --- |
| Sneaking, after an interactive block, or a snappable block before a line exists | 300 ms |
| Moving | `min(900 / speed, 180)` ms, speed in blocks per second from this tick's movement |
| Standing still | 200 ms |

While moving, a late placement banks at most 180 ms of the lost time, so the next one is not instant.

## Build line

Snappable blocks are full cubes, stairs, slabs, fences, walls, panes and bars (`isSnappableBlock`). When a held repeat
lands exactly one block from the last placed block, that step becomes the line: from then on only the line's next cell
is placed, and each success moves it one step further. Sneaking, or a non-snappable block, places wherever the click
goes without a line. Releasing use ends the line.

## Prediction

Bridge placements are predicted against the player's actual body, without the extra airborne padding used for
ordinary placement, so jump bridging shows blocks in time. The server stays authoritative; rejected placements are
corrected by the existing prediction expiry.

## Validation

`bridging_selftest` covers reach-around targeting.

```sh
cmake --build build --target bridging_selftest && ./build/bridging_selftest
```
