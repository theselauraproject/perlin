# Continued compatibility implementation

This update includes the first compatibility bundle and the additional work below. It remains an incomplete reproduction of Bedrock. A complete packet catalog, a generated decoder, a registered callback, an implemented effect, and proven behavioral equivalence are different results. Neither the source changes nor the tests establish acceptance by every anticheat.

The native behavioral reference is the matched **1.26.51.1 / protocol 2193** executable and PDB identified in [README.md](README.md). Current protocol 2225 metadata provides a separate wire-schema cross-check. No Minecraft executable, PDB, or raw disassembly is distributed.

## Changes with concrete consumers

| Area | Implemented effect | Principal regressions |
| --- | --- | --- |
| Physical input and PlayerAuthInput | Real controller/touch magnitude reaches local physics and effective/raw/analog packet vectors. SDL device activity selects the reported device; a connection or generated cursor event does not. | `input_reporting_selftest`, `client_control_selftest` |
| Movement send path | Failed encoding/transmission preserves queued flags, action groups and teleport acknowledgments. Accepted sends consume only their snapshot. A reset cannot restore an old send's caches. Initialization and due prediction synchronization can retry. | `movement_protocol_selftest` |
| Per-session codec context | Protocol and shield item identity stay bound to the owning operation across interleaved connections, callbacks, and deferred chunk decoding. Nested contexts restore their caller. | `session_codec_context_selftest`, `wire_selftest` |
| Distant world positions | StartGame, Respawn, ChangeDimension and prediction corrections use the shared finite `abs < 2^25` Perlin simulation envelope. StartGame fallback checks Y as well. Tick-zero player/vehicle corrections are rejected. | `movement_protocol_selftest`, `move_input_selftest` |
| PlayerArmorDamage 149 | In stack-request inventory mode, absolute signed 16-bit damage updates root NBT `Damage` and the displayed armor. The stack, stack ID and unrelated NBT remain intact. | `armor_packets_selftest`, `armor_damage_selftest` |
| MobArmorEquipment 32 | Local authoritative armor updates synchronize raw items as well as the visible view, so a later request does not use the previous stack ID or NBT. | `armor_packets_selftest` |
| UnlockedRecipes 199 | Add/remove/clear knowledge affects the actual recipe book and stale recipe selection. Visibility is grouped by output alternatives, following the inspected recipe-book consumer. | `recipe_packets_selftest`, `recipe_unlocks_selftest` |
| GuiDataPickItem 54 | Localizes the item name, preserves the supplied effect text, replaces the popup, and exposes it once to the HUD. Records independent flash-slot/time state. Never selects the inventory slot or sends equipment. | `server_ui_packets_selftest`, `gui_data_selftest`, `hud_screen_selftest` |
| PlayerLocation 326 | Maintains visible/hidden positions by unique ID even for unloaded actors. The locator renders the received position without moving the actor. Nonfinite coordinates fail with BadPacket reason 90. | `server_ui_packets_selftest`, `hud_screen_selftest`, `hud_feed_selftest` |
| SetPlayerInventoryOptions 307 | Stores all five fields. Perlin's existing inventory screen consumes creative tab, craftable filtering and the inventory/crafting book layout preferences. | `server_ui_packets_selftest`, `hud_screen_selftest` |
| PlaySound 86 and UpdateSoundData 348 | Preserves server handles, loop count and initial playback position; routes stop, volume, pitch, fade, seek, script pause and resume into real PCM playback. Range bypass no longer turns a positional sound into a global one. | `audio_packets_selftest`, `audio_selftest`, `audio_director_selftest` |
| CameraInstruction 300 | Correct 2193 optional fade timing/color and FOV string encoding reach the existing camera consumer. Known 2193 FOV easing names accept ASCII case variations. | `camera_wire_selftest`, `server_ui_packets_selftest` |

SetPlayerFurnaceOptions 351 now copies independent preferences for Furnace, Blast Furnace and Smoker, ignores other type values, and clears them on reset. **Its tests establish storage, not a completed furnace-screen consumer.** Perlin does not yet reproduce the native furnace recipe-book tabs/layouts.

## Proven wire corrections

For both 2193 and 2225:

| Field | Corrected representation | Evidence |
| --- | --- | --- |
| HurtArmor `armor_slots` | Unsigned variable-length 64-bit integer, replacing zigzag64 | Matched writer at RVA `0x2715350`, unsigned stream slot `0x68`, and current Mojang metadata |
| PlayerArmorDamage entry `armor_slot` | Signed zigzag32 enum, replacing a byte | Matched reflected property/binary archive path to signed stream slot `0x70`, and current Mojang metadata |
| SoundDataUpdate Fade | Duration float, then target-volume float | Matched 2193 member registration and current Mojang Fade ordinals |

The armor tests compare against independent literal bytes, including multi-slot packets and signed damage. Older schemas were not changed without version-specific evidence. A successful encode/decode round trip alone would not detect a shared wrong field type.

The following corrections are specific to the matched 2193 schema:

| Field or packet | Corrected representation | Evidence and regression |
| --- | --- | --- |
| ClientMovementPredictionSync 322 actor identity | Signed ZigZag64 unique ID, replacing an unsigned runtime-ID field that the bridge dropped | PDB `ActorUniqueID`, native writer's signed stream call; literal positive/negative IDs and a real sender with distinct runtime/unique IDs |
| Extended actor metadata flags | Exactly 64 bits in each of two words | Matched enum values through global bit 127; literal first, middle and top-bit fixtures in both directions |
| ClientboundUpdateSoundData 348 | Fixed little-endian uint64 handle, then seven complete event variants, each with an unsigned VarInt32 discriminator | Matched default mode 5, archive/property/writer paths, shared setter, and Mojang's later binding-fix note; independent fixtures for all seven operations |
| CameraInstruction 300 fade | Independently optional timing triple and color triple, with a presence byte for each | Matched property/optional writer paths; all four presence combinations and distinct absent/empty fade fixtures |
| CameraInstruction 300 FOV easing | Registered-label string, replacing a one-byte enum | Matched mode-5 archive traits and enum binder's string branch; independent nonlinear-name and case-handling fixtures |

For 348, each old property was bound to the entire native event variant. The normal native writer therefore emits seven identical copies. The version bridge delivers the seventh decoded value, matching the repeated assignment to the same native member; reverse conversion emits seven identical copies. Protocol 2225 retains its single uint8-tagged event. The previous bridge discarded the operation fields and could turn a valid incoming control into a default Stop. See the [continued evidence](evidence/v26_51/continued/README.md) for the full wire contract.

The native reader checks the complete unsigned 32-bit index against 6 before narrowing it and reports context error 8 for an invalid index. Perlin now checks all seven records before conversion: tag 256 cannot wrap into Stop, tags 257–262 cannot become other real controls, and an invalid earlier record cannot be hidden by a valid seventh record. Independent fixtures cover six invalid tags in every record position and verify that strict callbacks never run. The surrounding native rollback, continued parsing, and disconnect behavior remains unresolved.

The independent sound fixtures exercise field ordering and exact bytes. The real audio packet test additionally exercises both 2193 and 2225 from decoding through queued controls to mixed PCM output. The bridge inventory in [bridge-2193-gaps.md](bridge-2193-gaps.md) records remaining version-conversion limits.

The matched camera FOV reader recognizes 32 registered labels, case-insensitively. Its unknown/empty-label path reports a reflection-context error; the outer packet failure policy has not been recovered. Perlin now matches the known-label case handling for 2193 but retains its previous policy for unknown/empty names and the extra `inverse_lerp` curve. That remaining difference, full easing mathematics, and the camera/rendering lifecycle must not be hidden behind a claim of complete camera parity.

## Important behavioral details

### Input and movement

Analog input is used by the local simulation as well as the outgoing packet. Reporting a fraction while continuing to simulate full-speed movement would introduce another mismatch. Server directional locks filter the effective vectors; raw input remains the physical input before those locks. UI/text blocking and mod/plugin grabs continue to block gameplay input.

The chosen physical device policy is event-based. The exact native arbitration of mixed input, platform-specific deadzones and touch modes has not been reproduced in full. The implementation should not be described as proof of all native input behavior.

An encoded movement frame whose transport fails still advances Perlin's simulation tick. The pending protocol state survives, and successful transmission alone commits the last transmitted position. This is a concrete retry correction, not a complete reconstruction of native transport scheduling.

The four world-state packet `isValid` entries are shared true-return stubs in the inspected build. Their use of the shared finite `abs < 2^25` envelope is **Perlin numerical safety policy**, not a recovered validator for those four packet classes.

The native correction trace includes a `CorrectedWithoutRewind` path. Rejecting every correction without an exact retained frame would be incorrect. Native eligibility also compares against `ActorHistory::mOldestFrame`, but Perlin's deque is not equivalent to that history: it removes matched records, omits failed-send frames and has its own retention limit. The existing nonzero unmatched-correction fallback remains unchanged. Full history, replay ordering, and no-rewind scheduling remain unresolved.

### Codec ownership and connection transitions

The previous global protocol selector could make one session decode or encode with a different session's profile, even with simple interleaving on one thread. Atomic storage alone did not attach that value to its connection. An operation now captures protocol and shield item ID in a thread-local scope. Incoming dispatch, outgoing checked encoding, raw-send logging, recorded-client replay and deferred chunk work all use explicit scopes. Chunk entries carry the settings captured when enqueued. A replacement item registry clears an obsolete shield identity, and nested callbacks restore the enclosing context.

This fixes codec selection, not every world/transport transition. A detached chunk batch can still outlive a reset, and encoding plus transport replacement is not a single atomic operation. Process-wide palettes and dimension metadata retain their existing ownership. The exact coverage and remaining lifecycle issues are recorded in [session-context-gaps.md](session-context-gaps.md).

### Armor and recipe knowledge

PlayerArmorDamage is an absolute damage assignment. HurtArmor uses a distinct legacy damage/enchantment/randomness path and must not be routed through the same implementation. Only HurtArmor's proven wire defect is corrected here. Perlin represents four player armor slots; body slot 4 is not implemented and cannot alias offhand.

Recipe type 0 ensures that the knowledge component exists without adding or clearing IDs; types 1 and 2 add recognized IDs, type 3 removes and type 4 clears. Initial batches are additive. Unknown IDs are ignored at receipt, not deferred. AlwaysUnlocked recipes and the disabled `recipesUnlock` game rule bypass membership. Before the component exists, the previous full-catalog visibility is retained, matching the inspected consumer.

The native book tests whether any recipe for an output is unlocked, then considers available alternatives. Filtering individual recipes too early would lose a craftable variant of an unlocked output. Recipe toasts, category notifications, manual crafting and all `doLimitedCrafting` behavior remain outside this implementation. Absent optional recipe requirements retain the existing unrestricted default; that native constructor default was not independently established.

### GUI and locator

The popup producer replaces two strings, resets a single-precision age to zero, and marks the notice dirty. Each GUI tick adds `0.05f` while age is below `2.0f`; a notice can be consumed once while below that threshold. This availability window is separate from rendering. The reference UI combines title, a newline, and subtitle even when one string is empty.

Perlin feeds that content into its existing item-text HUD presentation. Its timer/fade and layout are not a proof of identical Bedrock rendering. The reference flash slot and timestamp are retained; the actual native flash consumer/duration has not been established and no arbitrary flash animation is claimed as exact.

PlayerLocation accepts any finite coordinates in the inspected validator, without the movement packet's `2^25` bound. Hidden updates retain a hidden entry even for unknown IDs. The locator keeps those positions separate from entity transforms. Perlin's locator color/layout, handling of other waypoint sources and all cross-dimension presentation are not established as identical.

Inventory preference storage preserves the copied values. Perlin's existing screen consumes only the supported preferences; native armor-tab behavior and a recipe-book-only full layout are not fully reproduced by its Java-style inventory UI.

### Server sound control

The matched update dispatcher calls the seven sound-interface operations without an outgoing success reply. Pause and resume use the script pause source. The local engine handles those operations on the existing voice, so pause/seek/resume does not create a replacement sound.

The named-sound callback's default gate is strict squared distance `< (16 * max(volume, 1))²`. Range bypass skips that gate while preserving original position and sound-definition spatial settings. Positive loop counts are extra repeats; zero is a single play and wire `0xFFFFFFFF` is the signed FMOD count `-1`, meaning infinite repetition. Other negative signed counts are not established as valid. The native forwarding path and [FMOD's loop-count contract](https://www.fmod.com/docs/2.04/api/core-api-channel.html#channel_setloopcount) support these meanings.

Every created server voice is owned by its session independently of the optional external handle. That prevents loops without a handle, or older voices after a reused handle, from escaping session cleanup. Handle reuse follows Perlin's latest successful mapping; native duplicate-handle semantics have not been established.

The mixer uses finite controls and double-precision interpolation to keep extreme finite fades from introducing infinities/NaNs into audio. Mixer interpolation, gain/pitch policies, stop-envelope timing, sound definitions, out-of-range seeking and FMOD scheduling remain implementation differences. The native Stop operation also publishes a record-stopped event; all of its listeners are not reproduced here.

## Established reference effects still incomplete in Perlin

These are examples, not the entire unresolved inventory:

| Packet/system | Established fact | Remaining work |
| --- | --- | --- |
| SimulationType 168 | Game and Test run normal entity systems; Editor runs a separate editor category. Invalid 3 is ignored. This is not a general pause switch. | Native entity-system category membership and editor simulation |
| ControlScheme 327 | Writes a control-scheme component, changes camera/input ECS state, and resets pitch for schemes 2 and 4. | Component lifecycle, option mutations and complete camera/input semantics |
| ContainerRegistryCleanup 317 | Cleans HUD dynamic containers and expires existing full container identities, including dynamic ID. | A real dynamic-container registry and its consumers; clearing ordinary inventory would be wrong |
| CompletedUsingItem 142 | Creates an item-use event for a valid item/method; does not itself consume the item or force stop-use. | Actor event listeners, achievements and other downstream effects |
| TickingAreasLoadStatus 179 | Only transitions BuildingTerrain to/from WaitingForTickingAreasPreload. | Equivalent loading-state lifecycle and its effects on initialization |
| AnimateEntity, Emote and data-driven UI | Native handlers route into substantial dependent systems. | Complete animation/controller/expression/UI behavior and resource dependencies |
| HurtArmor 38 and SetLastHurtBy 96 | Distinct legacy damage and player/level-dependent forwarding paths exist. | Exact conditional, random and downstream behavior |

The exhaustive per-packet inventories remain the record of every active ID. These targeted tests do not change the inventories' overall behavioral-equivalence status to established.

## Validation and practical acceptance

See [VALIDATION.md](VALIDATION.md) for the completed build/test results. The Windows workflow includes the new tests. A Windows workflow file is not evidence that a Windows build was run here.

No original Minecraft client was executed in this environment, and no custom server or anticheat acceptance test was performed. A useful next acceptance test requires the same server version/configuration, the matched reference client, Perlin's selected protocol, a repeatable action/state scenario, decoded server-side packet observations, and the server's rejection/flag log. Encrypted network captures from different sessions cannot simply be compared byte-for-byte without accounting for encryption, session identifiers and other variable state.

Passing a finite test corpus supports those scenarios. It does not prove equivalence for every game state, unknown server implementation, timing schedule, malformed input or future server update. This bundle must not be advertised as complete Bedrock parity or as guaranteeing zero anticheat kicks.
