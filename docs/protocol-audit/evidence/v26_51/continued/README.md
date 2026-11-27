# Continued matched-client inspection

These findings extend the catalog and dispatch map with bounded, manually reviewed contracts for 21 packet types, followed by movement, effect, item-use and creative-inventory investigations in the supplied **1.26.51.1 / protocol 2193** executable. The executable and PDB were inspected as data and never executed. Native-code addresses are RVAs in the exact executable identified below. The separately identified creative UI assets come from pinned public resource-pack revisions.

| Input | Identity |
| --- | --- |
| Executable SHA-256 | `0245c20c4377d74a25f3644bf441b460d6b3b45a88617387edccaacd27418377` |
| PDB SHA-256 | `1bddffbad2f0b8aa4701129a22b2a0abc9661a0c5b2cdb3890caac195b96f622` |
| CodeView GUID / age | `0353b4a9-3eaa-e2e2-4c4c-44205044422e` / 1 |
| Address basis | All nine PE/PDB section headers match; preferred image base `0x140000000` |

`packet_contracts.json` separates observed effects, evidence addresses, and remaining limits for every reviewed packet. A partial trace is explicitly labeled partial. In particular, `SetLastHurtBy`, `UpdateEquipment`, `AnimateEntity`, and parts of `Emote` and `ClientboundControlsScheme` remain unresolved. These findings do not establish complete gameplay parity or any anticheat outcome.

## Sound update wire format: a version-specific defect matters

The matched client writes `ClientboundUpdateSoundDataPacket` (348) in `SerializationMode::CerealOnly`, value 5. Its factory stores that value at `0x16C1D34`, and the writer at `0x38C6870` selects the cereal path for value 5. The fixed-width server handle precedes seven registrations of the same whole event variant.

| Protocol 2193 component | Encoding established by the inspected writer |
| --- | --- |
| Server sound handle | Little-endian unsigned 64-bit integer |
| Event copies | Seven consecutive complete variants; no array count and no seven option-presence bytes |
| Each event discriminator | Unsigned VarInt32; active values 0 through 6 |
| 0 Stop | No following event fields |
| 1 SetVolume | Float volume |
| 2 SetPitch | Float pitch |
| 3 Fade | Float duration, then float target volume |
| 4 SeekTo | Float seconds |
| 5 Pause | No following event fields |
| 6 Resume | No following event fields |

The native writer emits seven identical copies because each registered getter returns the same `mEvent` member. This is a real old binding defect. It must not be replaced with the later protocol's single event merely because the in-memory C++ payload contains one variant.

The evidence chain is:

- `0x38CC498` through `0x38CC57B`: seven member registrations, in order Stop, SetVolume, SetPitch, Fade, SeekTo, Pause, Resume.
- `0x38CED45`: each registration installs the same variant binding; `0x39A7F10` returns the whole member at payload offset 8.
- `0x4117050` / `0x4117630`: generic object/member writer; `BinarySchemaWriter::openObject` returns 3, so the JSON omit-default path does not suppress these members.
- `0x39A75E0`, especially `0x39A7683`: each variant writes its index through `SchemaWriter::writeAdditionalData(uint32_t)`; `0x40F2FC0` selects the unsigned VarInt32 stream writer with compression enabled.
- `0x38CA758` and `0x38CA87F`: Fade registers Duration before Target Volume. The float binding calls archive slot `0x20`.
- `0x316810` / `0x40F2DE0`: handle's unsigned64 binding reaches fixed-width stream slot `0x50` with compression flags clear.
- `0x39A7D00`: all seven setters overwrite the same event member; there are no seven independent runtime event states.

Mojang's own change note corroborates the duplicate serialization. The repository snapshot used here is `Mojang/bedrock-protocol-docs` commit `d011926b11583430de5d5c49f9c4e53eb7d1fe58`, with these primary source paths:

- [`developer_notes/r26u6/clientbound_update_sound_data_packet_tagged_variant.md`](https://github.com/Mojang/bedrock-protocol-docs/blob/d011926b11583430de5d5c49f9c4e53eb7d1fe58/developer_notes/r26u6/clientbound_update_sound_data_packet_tagged_variant.md): explains the old seven-whole-variant defect and the later single `Event` with a `uint8` tag.
- [`json/ClientboundUpdateSoundDataPacketPayload.json`](https://github.com/Mojang/bedrock-protocol-docs/blob/d011926b11583430de5d5c49f9c4e53eb7d1fe58/json/ClientboundUpdateSoundDataPacketPayload.json): protocol 2225 handle/event structure.
- [`json/Fade.json`](https://github.com/Mojang/bedrock-protocol-docs/blob/d011926b11583430de5d5c49f9c4e53eb7d1fe58/json/Fade.json): protocol 2225 ordinals are Type 0, Duration 1, Target Volume 2. Its float order agrees with the matched older executable.
- [`json/ServerSoundHandle.json`](https://github.com/Mojang/bedrock-protocol-docs/blob/d011926b11583430de5d5c49f9c4e53eb7d1fe58/json/ServerSoundHandle.json): unsigned64 without a compressed serialization option.

A focused follow-up recovered the normal binary variant reader's range guard: it loads the full unsigned32 index at `0x39A5EE5`, compares it against 6 at `0x39A5EE9`, and takes the context-error path at `0x39A6142` when it is larger. Byte narrowing occurs only after that guard. Thus a legacy index such as 256 must not wrap to the current one-byte Stop value. `sound_variant_reader_findings.json` records the path and error anchors. This establishes the per-variant guard, not complete outer error recovery or disconnect behavior.

The normal writer path and shared event setter are established. The generic reader's complete error paths for malformed or inconsistent repeated events were not exhaustively traced. Packet-level wire corrections do not establish exact audio-backend timing, invalid-argument handling, duplicate handle ownership, or every resource sound behavior.

## Camera FOV easing is a string on protocol 2193

`camera_fov_findings.json` records the complete narrow serialization chain. The packet factory stores mode 5 at `0x16B80AC`, and the mode-5 writer zeroes its archive traits at `0x83BD251`. The `fovEaseType` property installs the EasingType binder while leaving its serialization traits at `None`. A nearby value 4 is the separate `MemberTraits::IsMemberLevelSetterGetter` flag; it is not `SerializationTraits::EnumAsValue`.

The enum writer at `0x678BB40` consequently takes the registered-label path at `0x678C2EC`, through the archive string slot and `BinaryStream::writeString`. The matched `SharedTypes::bindEasing` at `0x7B42B10` registers 32 labels: `linear`, `spring`, then `in_`, `out_`, and `in_out_` forms of `quad`, `cubic`, `quart`, `quint`, `sine`, `expo`, `circ`, `bounce`, `back`, and `elastic`. Native enum values `Count` (32) and `Invalid` (33) are not registered labels, and neither is `inverse_lerp`.

The enum reader at `0x678A360` lowercases input bytes via the verified CRT `tolower` import before registered-member lookup. Unknown and empty labels reach its context error path at `0x678B1AE`. This does not by itself establish the outer packet's disconnect policy. Preserving a string in a codec is also separate from accepting it as a valid camera easing operation. Full camera rendering, timing, and absent-option behavior were not recovered by this narrow trace.

## Other concrete contracts

| Packet | Established contract | Significant remaining limit |
| --- | --- | --- |
| PlayerAction (36) | ChangeDimensionAck clears the local dimension acknowledgment flag | Separate listeners for other actions |
| HurtArmor (38) | Signed VarInt32 cause/damage; unsigned VarInt64 mask | Random/cause/enchantment and break behavior |
| GuiDataPickItem (54) | Replaces popup text, marks it dirty, stores flash slot/time; popup text is title + newline + subtitle | Flash visual curve and actual presentation duration |
| PlaySound (86) | Position divided by eight; bypass skips only initial distance gate; signed loop count passed unchanged to FMOD | Resource selection, de-duplication, exact attenuation/timing |
| CompletedUsingItem (142) | Builds a temporary count-one, aux-zero item and publishes an actor-use event | Event subscribers; no inventory mutation inferred |
| PlayerArmorDamage (149) | Absolute sign-extended int16 Damage NBT update; slot is signed VarInt32 | Body slot and invalid-slot container behavior |
| SimulationType (168) | Game/Test select normal entity-system tick; Editor selects editor tick | Editor system category membership and subscribers |
| TickingAreasLoadStatus (179) | Only transitions BuildingTerrain and WaitingForTickingAreasPreload | Other loading gates |
| UnlockedRecipes (199) | Initial/New are additive; unknown IDs skipped; recipe-book gate consumes rule/component state | Toast/categories, manual crafting, limited-crafting behavior |
| SetPlayerInventoryOptions (307) | Replaces the local inventory option record | Complete UI behavior |
| ContainerRegistryCleanup (317) | Expires existing exact full-container-name entries; does not clear normal inventory | Perlin dynamic container registry consumers |
| ClientMovementPredictionSync (322) | Member is ActorUniqueID; manual writer uses signed ZigZag64 | Complete reflected serializer path |
| PlayerLocation (326) | Finite-coordinate validator; independent unique-ID locator map; Hidden clears optional position | Native locator presentation/subscribers |
| SetPlayerFurnaceOptions (351) | Replaces the corresponding furnace/blast/smoker options | Complete screen/resource behavior |

Loop-count interpretation comes from the actual unchanged call to `FMOD::Channel::setLoopCount` at `0xB3A9E6A` and the [FMOD API documentation](https://www.fmod.com/docs/2.04/api/core-api-channel.html#channel_setloopcount): zero is one shot, positive N means N additional repetitions, and -1 means continuous looping. Other negative values were not established as valid.

## Camera fade and FOV wire fields

The matched CameraInstruction factory selects reflection mode 5. Its fade
instruction registers independent optional time and color values, in that order.
The callbacks at `0x67C88F0` and `0x67CA150` each write their presence bool through
archive slot `0x88`, whose binary callback `0x40F2FA0` calls
`BinaryStream::writeBool`. Each present payload contains three floats. The time
order is fade-in, hold, fade-out; the color order is red, green, blue. The previous
flat 2193 schema omitted both nested presence bytes. See
`camera_fade_findings.json` and its type/reference query files.

FOV easing is a registered-label string in the normal 2193 writer, despite its
in-memory `EasingType` enum. The property and archive leave `EnumAsValue` clear,
so the enum binder writes through the binary string callback. The reader
lowercases names before looking up the 32 registered labels. Unknown/empty names
set reflection context error `0x1000`; the complete outer failure/disconnect
policy remains untraced. `inverse_lerp` is not a registered name in this build.
See `camera_fov_findings.json` for the trait propagation, writer/reader anchors,
and exact registered labels.

These findings justify the two 2193 camera schema corrections and scoped known
name matching. They do not establish full visual timing, default/invalid-value
behavior, other CameraInstruction fields, or newer/older version semantics.

## Metadata and reproduction

### Movement, item-use and creative follow-up

| Evidence report | Bounded subject |
| --- | --- |
| `slow_falling_findings.json` | Gravity sign condition, effect IDs, movement-system ordering and levitation interaction |
| `mob_effect_contracts.json` | Recognized events, merge semantics, duration normalization and expiry |
| `consumable_use_findings.json` | Retained input, use timer, client/server completion split and ordinary item-use forwarding |
| `item_use_movement_findings.json` | Default/configured movement modifier and the explicit squared-input consumer |
| `item_use_identity_findings.json` | Item/slot/hand continuity, count refresh, comparison options and stop-only cancellation |
| `ranged_item_contracts.json` | Loaded crossbows, Quick Charge, trident startup, damage and active enchantments |
| `projectile_item_use_findings.json` | Pearl/generic throwable use and cooldown paths; server-side mace/Wind Burst branches |
| `air_use_transaction_findings.json` | Pre-use held-item snapshot, old/new inventory actions and legacy request scope; acknowledgement lifecycle remains open |
| `creative_catalog_findings.json` | Registry/group insertion order, expando headers and retained unassigned entries |
| `creative_ui_assets_findings.json` | Official desktop tab order and fixed composite sprites; distinct pocket layout |
| `creative_ui_assets_sources.json` | Pinned official URLs and content hashes for the compared resource-pack revisions |

The corresponding `*_query.json` and `*_reference.json` files retain reproducible
type/symbol selections where available. Compact manually reviewed reports remain
explicitly scoped. The creative UI files identify the surrounding published
26.50.4 and 26.60.5 assets; neither is presented as a supplied matching 26.51.1
resource pack. No game artwork was added to the source bundle.

`continued_type_reference.json` and `wire_type_reference.json` contain focused type, enum, method, virtual-slot and public-symbol metadata. `continued_query.json`, `wire_query.json`, `camera_fade_query.json`, and `camera_fov_query.json` specify their reproducible selections and expected executable hash. Other reference JSON files preserve supporting armor, recipe, GUI, sound, pause and interface facts used during the inspection. Memory offsets alone do not establish wire order; virtual overrides need their base-slot correlation.

Run from the Perlin repository root, with your own matching files:

```sh
python3 tools/bedrock_reference/query_pdb_reference.py "/path/to/1.26.51/Minecraft.Windows.exe" \
  "/path/to/Windows_Client_26.51.pdb" \
  --query docs/protocol-audit/evidence/v26_51/continued/continued_query.json \
  --out reference-output/continued_type_reference.json

python3 tools/bedrock_reference/query_pdb_reference.py "/path/to/1.26.51/Minecraft.Windows.exe" \
  "/path/to/Windows_Client_26.51.pdb" \
  --query docs/protocol-audit/evidence/v26_51/continued/wire_query.json \
  --out reference-output/wire_type_reference.json

python3 tools/bedrock_reference/query_pdb_reference.py "/path/to/1.26.51/Minecraft.Windows.exe" \
  "/path/to/Windows_Client_26.51.pdb" \
  --query docs/protocol-audit/evidence/v26_51/continued/camera_fov_query.json \
  --out reference-output/camera_fov_reference.json

python3 tools/bedrock_reference/query_pdb_reference.py "/path/to/1.26.51/Minecraft.Windows.exe" \
  "/path/to/Windows_Client_26.51.pdb" \
  --query docs/protocol-audit/evidence/v26_51/continued/camera_fade_query.json \
  --out reference-output/camera_fade_reference.json
```

The new query exporter uses Python's standard library and the adjacent PE/PDB parser scripts. It rejects an executable/PDB GUID or age mismatch, a section-header mismatch, and a supplied query whose expected executable SHA-256 differs. Its explicit virtual-slot fields reflect CodeView declarations; a null slot on an override does not mean the method is nonvirtual.

`packet_contracts.json` is a manually reviewed report, not generated disassembly or a fully automatic decompiler. Its named anchors allow further local review with a static disassembler. No proprietary executable/PDB bytes, reconstructed source, or raw disassembly are included in this directory.

## Additional implemented behavior references

The current source changes and tests are described in [BEHAVIOR_FIXES.md](../../../BEHAVIOR_FIXES.md). These additional groups use the same executable/PDB identity checks above:

| Subject | Query/reference/findings prefix | Applied consumer |
| --- | --- | --- |
| Layered ability replacement and fixed priority | `ability_layers` | Local ability resolution, speed values and reset |
| Levitation during gliding | `glide_levitation` | Physics and movement-packet regressions |
| Attribute history and acknowledgement handling | `attribute_replay` | Local prediction history, modifier state and live updates |
| Legacy item-use identities and descriptor serialization | `item_prediction_reconciliation` | Pearl/wind-charge transactions, prediction and following stack requests |
| Hotbar and local equipment selection | `inventory_selection` | Selection bounds and authoritative item ownership |
| Container-close manager lifecycle | `container_close` | Screen-close requests, responses and warning packets |
| Camera preset/runtime consumers | `camera_runtime` | Camera transforms, effects, defaults and easing |
| Camera controls and orientation | `camera_controls` | View/movement transforms, pitch reset and auth-input orientation |
| Named/inline camera splines | `camera_spline` | Registry lookup, playback and keyframe interpolation |

`crossbow_use_reset_findings.json` records event 74's input-reset continuation and the verified downstream default handlers. Each findings file retains its version and evidence scope. The common checksum manifest is regenerated after the final implementation and validation record are written.
