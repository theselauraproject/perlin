# Native Bedrock placement and breaking: reverse-engineering notes

Work in progress. Only facts read directly from the IDA database are listed.
Nothing here has been compared against, or ported into, Perlin's bridging,
placement or break code yet.

Reference database: Windows client `Minecraft.Windows.exe` (26.50 client
tree, image base `0x140000000`), stripped. Names below are inferred from
strings and field usage, not from symbols.

## Transaction dispatcher (ItemUseInventoryTransaction::handle)

- `sub_1426736A0` is `ItemUseInventoryTransaction::handle`. Most of it builds
  error text (`SourceItemMismatch`, `StateMismatch`, `BalanceMismatch`,
  `ApiDenied`). The error strings name three distance checks it can report:
  `playerDistanceToBlockPos`, `playerDistanceToFromPos`,
  `playerViewDirection`. The comparisons and thresholds behind them are not
  located yet.
- Offhand use is rejected with `ApiDenied` unless the parity experiment is on.
- The actual action dispatch is lambda `sub_14268D530`, switching on the
  transaction's action type (offset `+104`):
  - `0`: use item on block. Block position is at `+112/+116/+120`, face at
    `+124`, hand slot at `+136`, click offset at `+252/+256/+260`. It walks the
    chunk/block-entity candidates, calls the game-mode methods through its
    vtable and resends blocks around the position if the use is rejected.
  - `1`: use item (no block); result checked through a game-mode vtable call.
  - `2`: break block. A failed break logs
    `destroy block failed, resending blocks around area to player` and
    resends the area.
  - `3`: use item along a direction vector computed as the click position
    minus the from position (`+240/+244/+252/+256/+260`).
- `sub_14266FC20` compares the item in the transaction with the held item,
  including the `Damage` tag, before the dispatch runs.

## Client targeting (not yet traced)

- `HitResultSystem` (vtable `0x14E776230`) and `InteractSystem` (vtable
  `0x14E9A38F0`) are registered ECS systems. Their `vtable[2]` entries,
  `0x14665AFA0` and `0x14C5F4470`, are generic per-entity iteration wrappers.
  The targeting code is in the callbacks they invoke, which are not found yet.
- `ClientHitDetectCoordinator` has `sendChangedHitResult`,
  `sendContinuousHitResult`, `sendChangedPickHitResult` and
  `sendContinuousPickHitResult`. These are event notifications, not the ray
  itself.
- There are no strings for a bridging assist or air-targeting placement.
  The 26.60 build lambda (`0x143771570`) takes the click from the player's
  `HitResult`: `mPos - BlockPos` on a block hit, `(0, 0, 0)` without one, so a
  click that the look ray does not reach never comes from the vanilla client.

## Open questions

1. Where the client chooses the block, face and click position for a place,
   including any fallback when the ray hits nothing.
2. Reach and the three server distance checks (values and which position each
   one uses).
3. Placement repeat rate while use is held (`ClientCooldownState`).
4. Break start/continue/predict action ordering and delay.
