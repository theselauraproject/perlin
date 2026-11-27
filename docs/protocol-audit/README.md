# Bedrock protocol compatibility work

## Status

This source bundle contains verified static analysis, targeted compatibility fixes, regression tests, and an inventory of **every active packet ID in both supplied executables**. It is **not a completed reproduction of every Minecraft handler**, and it does not establish that all anticheat flags have been eliminated. Packet catalog coverage, successful codec generation, and regression tests are separate kinds of evidence.

The camera command, animation, fade/FOV and control changes are described in [camera-behavior.md](camera-behavior.md). The creative category, item ordering and header-icon changes are described in [creative-inventory.md](creative-inventory.md). The latest movement/item repair pass is described in [BEHAVIOR_FIXES.md](BEHAVIOR_FIXES.md). The earlier continued implementation is described in [CONTINUATION.md](CONTINUATION.md), including packet wire corrections, analog input, send-state retry handling, armor and recipe consumers, GUI/location state, server sound playback, and session-specific codec selection. Chunk loading and rendering changes, including the bundled Conduit source, are described in [the performance report](../performance/README.md). See [VALIDATION.md](VALIDATION.md) for completed checks and explicit limits.

No original Minecraft executable or PDB is included in the source bundle.

## Reference builds

The Perlin base is `0329b0cc4b1c21482b46cf275bd8a5eafc8aaeab` from `theperlinproject/perlin`.

| Reference | Version | Protocol evidence | Active packet classes | Debug symbols |
| --- | --- | --- | ---: | --- |
| First uploaded executable | 1.26.65.1 | All 239 recovered IDs and names match Mojang's protocol 2225 catalog | 239 | The supplied PDB does not match this executable |
| Second uploaded executable | 1.26.51.1 | Matched `SharedConstants::NetworkProtocolVersion` data is 2193 | 231 | PDB GUID, age, and all section headers match |

The 26.51 executable and PDB share GUID `0353b4a9-3eaa-e2e2-4c4c-44205044422e`, age 1. The 26.65 executable instead expects GUID `5f6cf44c-5d5d-f27c-4c4c-44205044422e`, age 1. Symbol addresses from the 26.51 PDB must not be applied to the 26.65 executable.

| Input | SHA-256 |
| --- | --- |
| 26.51 executable | `0245c20c4377d74a25f3644bf441b460d6b3b45a88617387edccaacd27418377` |
| 26.65 executable | `9138ed5c5a213c3de891f56693542c6de1f5258b04fac4889ac37233785a700e` |
| 26.51 PDB | `1bddffbad2f0b8aa4701129a22b2a0abc9661a0c5b2cdb3890caac195b96f622` |

Mojang reference metadata was inspected at commit `d011926b11583430de5d5c49f9c4e53eb7d1fe58`. Its JSON labels the current schema protocol 2225 / `1.26.60-beta.0`. Matching packet IDs does not prove that every field or behavioral detail remained unchanged in later 26.6x builds.

## Complete packet inventory

Read [packet-inventory-2193.md](packet-inventory-2193.md) for the matched 26.51 reference and [packet-inventory-2225.md](packet-inventory-2225.md) for the 26.65 reference. Each also has a JSON companion containing schema field definitions, source locations, handler registration functions, direct send-call names inside handler bodies, codec generation mode, and independent executable evidence.

The current schema contains 254 packet definitions: all 239 active 26.65 packet IDs plus 15 retained legacy definitions. The 26.51 schema contains 246 definitions: all 231 active 26.51 packet IDs plus those same 15 legacy definitions. There are no missing active IDs in these two comparisons and no opaque current or 2193 codec fallbacks.

The source scanner reports these live receive registrations for the active executable IDs:

| Reference protocol | Typed handler present | Partial handler present | Explicit no-op | No receive registration found | Active IDs |
| --- | ---: | ---: | ---: | ---: | ---: |
| 2193 | 117 | 2 | 21 | 91 | 231 |
| 2225 catalog comparison | 117 | 2 | 21 | 99 | 239 |

These counts describe source registrations, not a count or percentage of fully supported packets. Many client-to-server packets correctly have no incoming handler, and a registered incoming handler can still have missing behavior. Recorded-client replay callbacks are excluded from these live receive counts.

The table's receive classifications deliberately describe what was observed:

- **Typed handler:** a normal `On<Packet>` callback is present. Its complete behavior has not thereby been verified.
- **Partial handler:** the source explicitly permits partial decoding for that callback.
- **No-op:** a callback is registered but performs no work. Some official packets are themselves disabled or obsolete, so this alone does not establish a difference.
- **No receive handler found:** no concrete registration was found by the source scanner. This is expected for many packets sent only from the client. Direction and the required action still need to be considered individually.

The matched PDB index identifies serializer symbols for all 231 active 26.51 packet classes, and public dispatch-handler symbols for 198 of them. A separate comparison of the callback type layouts, constructors, and virtual tables resolves the final `LegacyClientNetworkHandler` dispatch entry for **all 231 active packets**. Of those entries, 53 point to a verified single-return stub. One further entry, `ClientboundAttributeLayerSync` (345), only marks the packet handled and returns. These are findings about this dispatch route; other listeners and dependent behavior still require analysis. Missing public symbols are never treated as proof that vanilla ignores a packet.

## Changes in this bundle

### Movement packets and input

- `MovePlayer` mode 3 (`OnlyHeadRot`, called `Rotation` in Perlin's schema) no longer overwrites local or remote player position or body yaw. It updates head rotation and pitch, including local view pitch. Remote position interpolation and queued movement keep their existing progress. This is supported by both the packet metadata and the inspected binary dispatch branch.
- Movement and correction data are checked before state changes. This covers nonfinite values, mismatched vehicle optionals, invalid movement modes, and teleport data that does not agree with the mode. The matched 26.51 `PlayerAuthInput` validator also supports the 500-unit per-component delta bound.
- Local/remote `MovePlayer` acceptance, outgoing movement positions, and pose updates now use the reference's strict per-axis position bound of 33,554,432 instead of Perlin's previous 100,000 horizontal / 10,000 vertical limit. Outgoing validation includes the eye-height offset actually serialized on the wire. The continued work also removes the separate spawn/respawn/dimension/prediction limits in favor of the same finite simulation envelope; those four handlers use it as Perlin numerical safety policy, not as a recovered native validator.
- `rawMoveVector` now reads input before server movement locks, while actual movement still respects those locks. UI/text blocking and mod freeze remain effective. The protocol describes raw input and permission-filtered movement as different values.

### Transport and decoding

- A negotiated compression threshold of zero now selects an uncompressed batch with the correct compression marker. Previously it selected compression for every batch.
- `OnPartial` is now a per-callback policy. A diagnostic trace listener can no longer make strict state-changing callbacks run after a decode failure.
- Unsigned 32-bit, 64-bit, and 128-bit variable-length integers reject values that overflow their width rather than truncating high bits or accepting an extra terminator.

### Inventory state

- Item-stack responses now update the furnace, blast-furnace, smoker, horse equipment, and horse chest slots that Perlin already sends in requests. Counts and server stack IDs are applied to the corresponding local slots.
- Rejected horse inventory predictions restore both the visible inventory and raw network stack data.
- Container slot bounds prevent invalid armor or hotbar indices from updating unrelated local slots.
- Protocol 2225 input flag 65 and player action 38 are named `InternalUpdate`, consistent with the supplied executable and official metadata. The experimental offhand shortcut no longer reinterprets flag 65 as a private swap instruction. The existing item-stack transaction path handles the player's swap action.

### Incoming item cooldowns

- The incoming cooldown packet applies its category and duration without an outgoing echo. Unqualified categories use the `minecraft` namespace; a new duration replaces the previous one. Positive durations decrement once per local simulation tick, independently of the outgoing input tick counter.
- Item registry metadata connects shared custom categories to their items, including component metadata supplied for vanilla item identifiers. The four built-in category overrides recovered from the reference are also recognized.
- Cooldowns suppress the local held-item use and placement predictions covered by this implementation. An ordinary air-use attempt still sends its transaction. A blocked placement sends a failed prediction without consuming the item, while the block's own successful interaction remains available.
- Ordinary air/block transactions retain the reference builder's `Off` cooldown field. The field is not blindly changed to `On` because a category is active. The reference's separate use-as-attack path requires different handling.

This implements the inspected server-driven cooldown path. It does not implement every item-specific rule for starting a cooldown locally, every attack action, or the complete cooldown UI.

Custom cooldown component parsing accepts the documented JSON category/type structure through Perlin's existing item-registry NBT-to-JSON adapter. The tests exercise real encoded registry packets with that structure. The precise registry representation emitted by the reference Minecraft build has not been independently established, so this part is not a claim of exact registry-wire equivalence.

These are concrete fixes. Robustness fixes, schema corrections, and input/transport behavior fixes should not all be described as independently proven causes of the user's anticheat reports.

The exact native failure, cancellation, and disconnect behavior for malformed packets was not reconstructed in full. In particular, the stricter variable-integer handling is a robustness correction; it is not a proof of identical behavior for every invalid byte sequence. The outgoing movement guards use the recovered packet-validity bounds, without establishing every native send-path failure or retry rule.

## Important distinctions discovered during analysis

`SetMovementAuthority` is a retained legacy definition, not a current active packet in either supplied executable. Mojang documents removal of the old movement-authority switching path. Adding an obsolete mode switch merely because Perlin ignores this ID would not establish parity.

The matched `GuiDataPickItem` handler displays an item popup and flashes a hotbar slot. Its `mSlot` field updates GUI flash state; it does not itself select an inventory slot or send an equipment reply. Treating its name as a command to select a slot would introduce another difference.

`PlayerStartItemCooldown` has an incoming client handler in the matched executable. It applies a category cooldown without echoing the packet. Its behavior cannot be inferred solely from an abbreviated packet name or description.

## Windows build

The repository's existing Windows build presets and main Build workflow are retained. A separate manually triggered **Protocol compatibility - Windows** workflow builds the client, runs the focused tests and handler tests, and uploads `Perlin-protocol-compatibility-windows-x64.zip`. It does not publish a GitHub release.

To use it, extract the source archive and place the contents of its `perlin` folder at the root of your GitHub repository, including `.github`, `cmake`, `protocol`, `resources`, `src`, `tests`, `third_party`, and `tools`. Commit the workflow on the repository's default branch so GitHub exposes its manual trigger. Open **Actions → Protocol compatibility - Windows → Run workflow**. Download the artifact only after the build and tests succeed. See [GitHub's manual-workflow instructions](https://docs.github.com/en/actions/how-tos/manage-workflow-runs/manually-run-a-workflow).

For a local Windows build, use a Visual Studio Developer PowerShell with **C++ Clang tools for Windows**, CMake 3.28 or later, Ninja, Python, and a static OpenSSL installation:

```powershell
cmake --preset windows-clang-cl -DPERLIN_BUILD_TESTS=ON -DCONDUIT_BUILD_TESTS=ON -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DPERLIN_FAST_MATH=OFF
python tools/check_simulation_compile_options.py build/windows-clang-cl/compile_commands.json
cmake --build --preset windows-clang-cl --parallel 4 --target Perlin packet_selftest wire_selftest move_input_selftest inventory_slots_selftest item_cooldown_selftest item_use_wire_selftest chunk_wire_selftest session_policy_selftest client_control_selftest actor_lerp_selftest physics_selftest status_effect_selftest heldbuild_selftest gui_data_selftest armor_damage_selftest item_nbt_selftest creative_catalog_selftest recipe_unlocks_selftest bridge2193_selftest sound_packets_wire_selftest camera_wire_selftest camera_effects_selftest audio_selftest hud_feed_selftest chunk_selftest chunk_ingest_selftest perlin_chunkbench cells16_selftest level_undo_selftest section_connectivity_selftest chunk_visibility_selftest perlin_meshbench common_cpu_selftest null_selftest render_profile_test vendor_test
ctest --test-dir build/windows-clang-cl --output-on-failure -R "^(packet_selftest|wire_selftest|move_input_selftest|inventory_slots_selftest|item_cooldown_selftest|item_use_wire_selftest|chunk_wire_selftest|session_policy_selftest|client_control_selftest|actor_lerp_selftest|physics_selftest|protocol_audit_selftest|handler_dispatch_selftest|player_interaction_selftest|executable_boot_resources|gui_data_selftest|armor_damage_selftest|item_nbt_selftest|ranged_item_selftest|item_use_packets_selftest|creative_catalog_selftest|creative_inventory_selftest|recipe_unlocks_selftest|bridge2193_selftest|sound_packets_wire_selftest|camera_wire_selftest|camera_effects_selftest|camera_effects_packets_selftest|audio_selftest|hud_feed_selftest|armor_packets_selftest|recipe_packets_selftest|server_ui_packets_selftest|audio_packets_selftest|input_reporting_selftest|movement_protocol_selftest|movement_effects_selftest|attribute_replay_selftest|status_effect_selftest|status_effect_replay_selftest|heldbuild_selftest|session_codec_context_selftest|audio_director_selftest|hud_screen_selftest|chunk_selftest|chunk_ingest_selftest|chunk_bench_correctness|cells16_selftest|level_undo_selftest|section_connectivity_selftest|chunk_visibility_selftest|mesh_exactness|common_cpu_selftest|null_selftest|render_profile_test|vendor_test|chunk_pipeline_selftest)$"
```

If OpenSSL is not discovered automatically, add `-DOPENSSL_ROOT_DIR=C:/path/to/x64-windows-static` to configure. Use the Visual Studio-supplied `clang-cl`, as described in the main README.

The Windows workflow is provided for running the actual Windows validation. A Linux compile or syntax check is not a completed Windows build or in-game compatibility test. See [VALIDATION.md](VALIDATION.md) for the exact checks run on this bundle.

## Reproduce the source inventory

Without any external binary or metadata, the following inventories every packet in the selected schema and its source registrations:

```sh
python tools/protocol_audit.py --output packet-inventory.json --markdown packet-inventory.md
python tools/protocol_audit.py --spec protocol/legacy/bedrock-1.26.51.json --output packet-inventory-2193.json
```

The optional `--binary-inventory` and `--official-dir` inputs add independent catalog comparisons. `--reference-handler-manifest` adds the matched client dispatch targets after checking the executable SHA-256, packet IDs, names, and implementation addresses against the selected executable catalog. The binary/PDB scripts and retained evidence are described in `evidence/README.md`.

## How to interpret validation

The regression suite exercises actual packet decoding, state consumers, input/physics, inventory transactions and outgoing packet bodies. Version-matched static findings are linked from the implementation reports. The source inventory accounts for all active packet IDs and records concrete handler registrations; it is not a percentage of completed gameplay.

The latest changes include local cooldown/consumption prediction for pearls and wind charges, attribute history, fixed-priority ability layers, configured item-use movement modifiers, crossbow completion input reset, inventory selection and container-close lifecycle. See [BEHAVIOR_FIXES.md](BEHAVIOR_FIXES.md) for their exact behavior and tests.

No controlled Windows run of the supplied Minecraft build and modified Perlin client against the user's server configuration was performed here. The validation report separates completed Linux tests, source-backed native contracts and the supplied Windows workflow. Passing those tests does not certify universal anticheat acceptance or an external service's official-client classification.

## Primary protocol references

- [Mojang protocol documentation source](https://github.com/Mojang/bedrock-protocol-docs/tree/d011926b11583430de5d5c49f9c4e53eb7d1fe58)
- [PlayerAuthInput, protocol 2225](https://mojang.github.io/bedrock-protocol-docs/1.26.60/packets/player-auth-input-packet/)
- [MovePlayer, protocol 2225](https://mojang.github.io/bedrock-protocol-docs/1.26.60/packets/move-player-packet/)
- [Movement protocol deprecation](https://mojang.github.io/bedrock-protocol-docs/guides/movement-protocol-deprecation/)
- [NetworkSettings schema](https://github.com/Mojang/bedrock-protocol-docs/blob/d011926b11583430de5d5c49f9c4e53eb7d1fe58/json/NetworkSettingsPacketPayload.json)
- [Microsoft item cooldown component definition](https://learn.microsoft.com/en-us/minecraft/creator/reference/content/itemreference/examples/itemcomponents/minecraft_cooldown?view=minecraft-bedrock-stable)
