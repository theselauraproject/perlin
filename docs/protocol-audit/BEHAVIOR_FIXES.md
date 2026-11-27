# Movement, physics, item-use and container fixes

This source bundle includes the compatibility repairs from the earlier continuation plus the changes below. The native reference is the identity-matched **Minecraft 1.26.51.1 / protocol 2193** executable and PDB. The same Perlin consumers are exercised under profiles 944, 2193 and 2225. Running those additional profiles tests Perlin's code paths; it does not independently identify the native behavior of a different Minecraft build.

The final build and test record is in [VALIDATION.md](VALIDATION.md). Camera and creative-menu changes are described in [camera-behavior.md](camera-behavior.md) and [creative-inventory.md](creative-inventory.md). Rendering and chunk performance changes are documented in [the performance report](../performance/README.md), with their measured workloads.

## Movement and physics

### Ability layers use native priority and replacement rules

`UpdateAbilities` now resolves each Boolean and speed through the six native layers, from LoadingScreen down to Cache. Packet array order no longer changes the result. A later duplicate replaces that layer's complete record, so an omitted bit exposes the next lower layer instead of retaining an old value. Unknown layer IDs follow the recovered Base-layer fallback. Permission-derived defaults are filled before explicit records are applied.

The resolution preserves explicit zero movement/flying speeds. Updates addressed to another actor, or received before a known local player exists, do not change local movement abilities. Reconnection and replay reset the ability snapshot, including the sequence observed by movement consumers.

The handler regression runs all 720 permutations of a six-layer fixture in each of the three profiles, plus duplicate layers, empty records, permission defaults, zero values, unknown layers, actor filtering and reset behavior. Native evidence: [ability_layers_findings.json](evidence/v26_51/continued/ability_layers_findings.json).

### Attribute changes participate in prediction history

Movement, underwater movement, lava movement and air-drag updates now enter a local attribute queue. The movement consumer places stamped corrections at the next saved frame, retains complete attribute events, and reapplies later events in their original order when an earlier correction changes history. Each replayed frame evaluates its saved sprint and freeze state against its own retained modifier model.

A matching historical acknowledgement preserves a newer live attribute model. Mixed packets update the differing live attributes while retaining all entries needed for a historical correction. Once a correction is pending, later arrivals in the same drain use the native correction path instead of the equal-value shortcut. At the newest retained stamp, comparison uses the evaluated movement component, including freeze, rather than the raw packet's current value.

Unstamped/future updates retain their next-frame boundary. A failed movement send does not discard a not-yet-recorded attribute event. Other actors' updates stay outside the local prediction history. Session resets also clear old modifiers, metadata dimensions, freeze events, movement locks and local flags.

The new `attribute_replay` app test exercises real encoded packets, history transitions, modifier preservation and the next outgoing movement delta. Native evidence: [attribute_replay_findings.json](evidence/v26_51/continued/attribute_replay_findings.json).

### Effects merge and replay through their actual effective state

Add and Update use the recovered effect merge operation. A stronger amplifier replaces the active effect; an equal amplifier extends a shorter duration; a weaker effect does not replace a stronger one or wait to reappear later. Infinite duration, ambient status and particles follow their respective native branches. Remove clears the effect; unrelated event values do not mutate it.

Prediction history retains both effective effect snapshots and later update/removal events. Replaying an earlier correction does not lose a subsequent removal or resurrect a rejected weaker effect. Events survive failed sends. Expired effects no longer leave stale HUD flags.

Slow falling applies reduced gravity only when vertical velocity is strictly negative. Zero and upward velocity retain ordinary gravity. Tests cover successive descents, jump boost, levitation, landing, ceilings and fluid interactions. The gliding path now applies the recovered levitation vertical adjustment as well; this correction is checked numerically and through actual outgoing movement packets.

Evidence: [mob_effect_contracts.json](evidence/v26_51/continued/mob_effect_contracts.json), [slow_falling_findings.json](evidence/v26_51/continued/slow_falling_findings.json), and [glide_levitation_findings.json](evidence/v26_51/continued/glide_levitation_findings.json).

### Item movement modifiers reach the input consumer

The default native item-use modifier remains `0.35f`, squared once by the movement-input consumer. Registered `minecraft:use_modifiers` data is now preserved and used for custom timed-use items. A missing component uses the normal default; a present component with its modifier omitted uses `1.0`; an explicit finite modifier is retained, including zero.

Integration tests feed item definitions through the registry, actual item use and physics, then decode the resulting movement vector. Releasing or invalidating the active use restores ordinary movement. Evidence: [item_use_movement_findings.json](evidence/v26_51/continued/item_use_movement_findings.json).

## Eating, charging and selecting items

### Held food use continues through the normal input path

Perlin previously left its consumption latch set after the local timer completed, requiring another physical press. Held intention can now enter ordinary item use again when the active use clears. The new use starts its own timer and slowdown. The golden-apple regression holds through completion, observes the next ordinary air-use transaction, and verifies one cancellation when the button is released.

The client does not fabricate a server-side consumption transaction merely because its timer finishes. The supplied gameplay log alone does not establish whether the server accepted a particular consumption attempt. The repaired defect is reproduced in the interaction tests. Evidence: [consumable_use_findings.json](evidence/v26_51/continued/consumable_use_findings.json).

### Active use follows the selected item, slot and hand

Changing slot/hand or replacing/emptying the active item stops its old local use state and slowdown. This cancellation does not invoke a manual weapon-release action. Ordinary supported item classes retain active use across count, aux/durability and compatible NBT refreshes, following the recovered continuity checks.

The signed use duration is captured at startup. Later enchantment or hunger updates do not retroactively replace that timer or incorrectly remove active slowdown. Failed air-use transmission does not leave an orphaned StartUsingItem flag for the next movement packet. Evidence: [item_use_identity_findings.json](evidence/v26_51/continued/item_use_identity_findings.json).

### Crossbow completion no longer fires from the original held press

Loaded crossbows recognize the authoritative `chargedItem` compound and can fire without spare ammunition. Charged-projectile NBT is parsed separately from the weapon's name and durability. The parser requires a complete root and preserves the projectile's registered item identity.

Quick Charge uses the active enchantment list and the recovered signed duration: 25 ticks for a nonpositive level, otherwise the 32-bit result of `25 - 5 * level`. Zero and negative results retain their distinct native timer behavior. A later metadata change does not replace an already-running duration.

Local event 74 records the charge-completion input reset. The interaction consumer waits for valid loaded metadata, so either event/NBT arrival order works. It blocks automatic firing from the same held press while preserving the active timer. A physical release clears that continuation state; a fresh press fires the loaded crossbow once.

Evidence: [ranged_item_contracts.json](evidence/v26_51/continued/ranged_item_contracts.json) and [crossbow_use_reset_findings.json](evidence/v26_51/continued/crossbow_use_reset_findings.json).

### Trident startup uses the recovered item gates

The trident checks the active Riptide enchantment, its wet-condition gate and the built-in damage limit before starting held use. Startup and release use the same rain predicate. Enchantment lookup uses the active `ench` list and first matching entry, rather than `StoredEnchantments`.

An attempted ordinary use still follows its transaction path when an item-level gate fails. Missing ammunition no longer prevents the earlier interaction with a chest or another usable block. The inspected mace/Wind Burst client branches also do not justify adding a fabricated local velocity impulse. Evidence: [ranged_item_contracts.json](evidence/v26_51/continued/ranged_item_contracts.json) and [projectile_item_use_findings.json](evidence/v26_51/continued/projectile_item_use_findings.json).

## Pearls, wind charges and inventory reconciliation

Pearl and wind-charge air use now captures the hand, slot and pre-use raw stack once. A consuming use includes the old-to-new container action. Surviving stacks receive the native negative-even legacy request identity and changed-slot mapping; consuming the final item produces an old-to-empty action with legacy request ID zero and no changed-slot list. Instabuild leaves the count unchanged.

The process-wide even counter starts at -4 and advances by two for each ordinary air-use call, including non-consuming and cooldown-blocked attempts. Ordinary stack requests remain in the separate negative-odd family. Invalid, positive, zero and even IDs cannot apply ordinary ItemStackResponse updates.

For the matched 2193 serializer, outgoing held-item and action descriptor copies omit optional stack IDs. The local predicted stack retains its even ID so the following ordinary inventory request can refer to it. This version-specific behavior is tested independently of the raw inventory identity.

Predicted count and cooldown changes commit only after a successful send. Per-slot revisions, per-category cooldown revisions and a reset epoch protect authoritative changes received during the send, including a correction that repeats the exact old stack bytes. Unrelated slots/categories do not cancel valid prediction. A changed selection does not redirect the captured use to another stack.

Pearls use the recovered 20-tick cooldown; the vanilla wind-charge definition supplies 10 ticks. The common native path starts cooldown only when the post-use stack is nonempty. No clientbound cooldown packet is echoed back to the server. Holding a throwable can repeat after cooldown while a stack remains; emptying it clears the intention without inventing ItemRelease.

Wind charges now stack to 64. Rejected inventory drops restore horse equipment's raw identity/NBT along with its visible stack. Tests exercise modern and legacy inventory modes, count-one/count-many uses, offhand, Instabuild, cooldown failures, corrections during send, reset during send, failed sends and subsequent ordinary stack requests.

Native evidence: [item_prediction_reconciliation_findings.json](evidence/v26_51/continued/item_prediction_reconciliation_findings.json). The adjacent official asset reference is [Mojang's pinned v1.26.50.4 wind-charge definition](https://github.com/Mojang/bedrock-samples/blob/46ba6ea985fb5a92d79a9419198f10dda14c199d/behavior_pack/items/wind_charge.json); its half-second cooldown and stack size 64 are also stated in the [official Bedrock preview notes](https://www.minecraft.net/en-us/article/minecraft-preview-1-20-70-24). That asset reference is distinct from the identity-matched executable.

## Inventory selection and container lifecycle

A local MobEquipment packet for inventory container zero selects a valid hotbar slot without overwriting its item contents. Other local equipment containers do not change that selection or either hand. InventorySlot and InventoryContent remain responsible for authoritative item replacement. PlayerHotbar and MobEquipment ignore invalid unsigned slot indices instead of clamping them to slot eight, and ignore local selection changes before a player exists.

Container-close handling now follows the recovered manager lifecycle. An unsolicited close response received while a manager is open leaves the screen open and sends the native warning: Unknown type, Warning severity, cause packet ID 156 and the recovered context text. The incoming window ID is not used to treat that response as an instruction to close the current screen.

A server-initiated close records the requested type. The screen consumes it only for a matching current container type. Deleting the active manager sends one ordinary close response using that manager's ID, container type None and `serverInitiated=false`. Receiving the request no longer sends an early acknowledgement followed by a second UI close. Responses and repeated close calls after the manager is gone do not create another close packet. Session resets clear pending open/close signals.

These are ordinary Bedrock state and packet behaviors. Falcon's public container probe exercises the unsolicited-response case. The inspected Falcon revision also consults an external classification service; this source work does not establish that service's classification or manufacture an official-client identity.

Evidence: [inventory_selection_findings.json](evidence/v26_51/continued/inventory_selection_findings.json), [container_close_findings.json](evidence/v26_51/continued/container_close_findings.json), and [Falcon ClientProbe at the inspected revision](https://github.com/Falcon-MC/Falcon/blob/e1a665fa2a7cd72f4d05a1a9de3a6ad1b79aba63/Falcon.Server/src/Network/ClientProbe.cpp).

## Placement, protocol selection and codec repairs

Mouse-down input creates a press only on the up-to-down transition. Repeated backend notifications while the button is already held no longer become additional block placements on later frames. The integrated regression verifies one press, one placement and one predicted decrement; a genuine release followed by a new press still places normally. Intentional held building remains supported.

A transfer endpoint advertising an unsupported placeholder no longer silently changes the established protocol/game version to the newest default. Supported advertisements and an explicit user override retain their precedence.

The legacy switch generator now correctly excludes explicit payload-free cases from a default payload. This fixes zero-legacy-ID transaction serialization in profiles 944, 975 and 1001 without changing their schema or adding a wire presence byte. Literal byte cases and an independently compiled generated fixture cover zero/nonzero IDs, nested records and truncation. Camera bridges also preserve older flat fade times and supported easing enums.

Profiles 944 and 975 also now map legacy action `inventoryId` to the current Container `windowId` and back. Previously, an offhand action could silently become a main-inventory action. Optional flags are kept only for WorldInteraction actions, and an absent legacy changed-slot list stays absent. The independent literal-byte regression fails 38 of 78 checks with the previous bridges and passes all 78 with the corrected generator. The profile-944 app assertions account for its format having no hand field; container IDs, counts and action identities remain checked.

### Older chunk layouts retain their full contents

The bridges for profiles 944, 975 and 1001 now distinguish ordinary columns, unlimited subchunk requests and requests with a highest relative subchunk index. An absent request limit no longer becomes a present zero. Cached column hash lists survive both conversion directions, including a present empty list.

Subchunk replies now select the cached or uncached entry layout from the actual packet flag. Payload presence, blob IDs, ordinary and render heightmaps are preserved according to each layout. The fixed 256-byte maps retain their signed byte values and row order when converted to the current row-based representation. The intermediate 2168 layout also preserves its independently optional heightmaps. Subchunk request offsets keep their signed X/Y/Z components across the older member-name conversion.

The independent `chunk_wire_selftest` supplies literal packet bytes for each affected layout. It also checks that unrepresentable outgoing limits and malformed map dimensions fail serialization instead of silently narrowing, padding or discarding data. Those rejection rules are Perlin representation checks, not a claim about every native malformed-packet response. The request-mode sentinel interpretation is documented in [Mojang's subchunk request guide](https://mojang.github.io/bedrock-protocol-docs/guides/sub-chunk-request-system-v1-18-10/).

## Verification meaning

The source archive contains the actual implementations, regression tests, extracted metadata and Windows build workflow. It excludes the supplied executables/PDB, private logs and raw disassembly. Build/test evidence verifies the exercised Perlin code and packet sequences; it does not certify universal server/anticheat acceptance or complete behavioral equivalence with every Bedrock build.
