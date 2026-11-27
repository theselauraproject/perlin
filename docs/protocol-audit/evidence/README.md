# Minecraft reference evidence

This directory records a static inspection of two user-supplied Windows executables and one PDB. The executables were read as data and were never executed. These files contain metadata, packet IDs, symbol names, memory-layout records, and evidence addresses. They contain no executable, PDB, reconstructed Minecraft source, or full disassembly.

The inspection establishes complete **packet catalog coverage for the observed getName/getId implementations** and a symbol-backed map of serializers and a verified client virtual-dispatch entry for every active packet in the matching 1.26.51.1 build. It does not establish complete handling semantics, wire compatibility, gameplay equivalence, or any anticheat outcome.

## Build identity and scope

| Property | 1.26.51.1 executable | 1.26.65.1 executable |
| --- | --- | --- |
| Architecture | PE32+, AMD64 | PE32+, AMD64 |
| File size | 311,249,032 bytes | 323,828,088 bytes |
| SHA-256 | `0245c20c4377d74a25f3644bf441b460d6b3b45a88617387edccaacd27418377` | `9138ed5c5a213c3de891f56693542c6de1f5258b04fac4889ac37233785a700e` |
| CodeView GUID | `0353b4a9-3eaa-e2e2-4c4c-44205044422e` | `5f6cf44c-5d5d-f27c-4c4c-44205044422e` |
| CodeView age | 1 | 1 |
| Supplied PDB matches | Yes, GUID/age and all nine section headers | No |
| Active packet implementations recovered | 231 | 239 |
| Network protocol evidence | Integer constant **2193** at RVA `0xE780858`, named `SharedConstants::NetworkProtocolVersion` in matching PDB | All 239 packet IDs and class names match Mojang's protocol 2225 catalog; a network protocol integer was not independently located |
| Corresponding Perlin schema | `protocol/legacy/bedrock-1.26.51.json` | `protocol/bedrock-1.26.60.json` |

Each packet catalog records `executable_sha256`. Address mapping requires this hash to equal the input executable hash, in addition to the PDB GUID/age and section-header checks. A catalog with identical packet names and addresses but a different or missing executable hash is rejected.

The supplied PDB has SHA-256 `1bddffbad2f0b8aa4701129a22b2a0abc9661a0c5b2cdb3890caac195b96f622`, size 60,108,800 bytes, 13 MSF streams, and 321,640 TPI records. A matching GUID does not permit applying its addresses to the newer executable. The scripts refuse that mapping.

Both PE files have the same embedded PDB path, which is insufficient to identify a matching PDB. Embedded version resources, CodeView records, hashes, and section comparisons are recorded separately. Presence of a certificate directory was recorded; the signatures were not cryptographically verified.

## What each artifact contains

`v26_51/packet_catalog.json` and `v26_65/packet_catalog.json` enumerate packet class name, numeric ID, getName/getId RVAs, and a vtable RVA. The extractor recognizes a string-view-returning getName implementation, finds its vtable entry, and reads the immediate integer returned by the preceding getId entry. The 239 newer entries exactly match the 239 packet records in the Mojang protocol JSON snapshot at commit `d011926b11583430de5d5c49f9c4e53eb7d1fe58`. Name comparison is against Minecraft class names; Perlin schema aliases such as entity versus actor are not assumed equivalent merely by spelling.

Both Perlin schemas contain all recovered active IDs. Each also contains the same 15 legacy IDs that were not recovered from that executable catalog: 20, 23, 24, 53, 55, 57, 117, 120, 126, 134, 163, 173, 197, 301, and 319. A schema-only definition is not, by itself, evidence of a defect. Likewise, a serverbound-only packet need not have a client receive handler.

`v26_51/matched_packet_manifest.json` associates all 231 recovered packet implementations with matching PDB symbols and selected class/payload memory layouts. All 231 have serializer symbols. There are public dispatch symbols for 198 packet types and validator symbols for 13. Public-symbol lookup alone leaves 33 packet types without a named handler. The subsequent vtable stage resolves the `LegacyClientNetworkHandler` virtual entry for all 231 active packets, including these 33; their downstream behavior remains unverified. A missing public symbol must not be labeled a missing Minecraft implementation.

`v26_51/client_dispatch_vtables.json` records all 231 final client entries. The PDB declares 238 `NetEventCallback::handle` overload slots, covering every active packet. The tool uniquely identifies `ClientNetworkHandler` and `LegacyClientNetworkHandler` vtables using 97 and 50 exact public handler anchors, respectively, and verifies 112 and 183 inherited slots against their parent tables. The named `LegacyClientNetworkHandler` constructor at `0x132BDA0` references both tables. Manual inspection confirms its writes to the main object vptr: `0xE7AE8D0` at instruction `0x132BE29`, then final table `0xE7AF110` at `0x132C6F4`. This combines type signatures, function addresses, inheritance, and constructor evidence without relying on guessed RTTI names.

Every packet row in the matched manifest now has `client_dispatch`, containing the final target, virtual slot, nullable public name, and explicit `verified_ret_only` and `verified_mark_handled_only` booleans. There are 178 distinct final client targets. 53 packets target a single verified RET instruction at `0x76BC0`; one additional packet only marks itself handled. 149 packet entries have a matching public name. These counts describe this virtual dispatch route, including serverbound packets for which an empty client callback is expected; other listeners are not resolved.

`v26_51/handler_call_map.json` inventories direct and indirect call sites for both public dispatch symbols and the recovered client vtable entries. It records 252 distinct entrypoints, of which 250 have usable unwind ranges or a directly verified trivial leaf body; 3,077 direct call sites, 819 with exact public target symbols; and 2,517 unresolved indirect call sites. The two remaining functions without `.pdata` ranges are public server handlers; the mapped client entries have been decoded. Their unresolved boundaries remain explicit. Many indirect sites go through Control Flow Guard, virtual dispatch, or function pointers. A pointer-slot symbol does not identify the eventual target. Direct targets may be wrappers, and the tool does not recursively interpret callees. Possible external tail jumps are listed separately. This is a broad map for further inspection, not a recovered state machine.

`v26_51/matched_symbol_index.json` preserves the additional packet, inventory, compression, cooldown, and protocol-version symbols used for targeted inspection. `pdb/` contains the matching identity and selected packet class layouts/symbols. C++ object offsets describe memory, not field serialization order.

## Concrete behavioral findings

All addresses below are relative virtual addresses in **1.26.51.1** unless explicitly described as newer-build evidence. Windows ASLR does not alter the meaning of an RVA. The PE preferred image base is `0x140000000`.

### PlayerAuthInput flag 65

Both executables register the same 66 `PlayerAuthInputData` names and values, 0 through 65. Value 65 is `InternalUpdate`, not `SwapOffhand`. The newer executable's enum registration is at `0x4026C80`, with the label reference at `0x40330BC` and value getter at `0x40E39C0`; the getter writes 65 at `0x40E39FF`. In the older executable, the registration is at `0x3C9A8E0`, label reference at `0x3CA65E7`, and getter at `0x3D58BC0`. The machine-readable records are in each version's `player_auth_input_enum.json`.

This is evidence for correcting a misnamed and incorrectly emitted outgoing bit in the corresponding Perlin versions. It does not justify using a later protocol's flag meaning on these builds.

### MovePlayer mode 3 and raw movement input

The matched movement path has `LegacyClientNetworkHandler::handle(MovePlayerPacket)` at `0x133E990`, a rewind listener at `0x43591D0`, and `Player::handleMovePlayerPacket` at `0x200F20`. The rewind listener passes mode 3 through. The client handler applies the packet's ground state before dispatch. Mode 3 updates pitch/head orientation while preserving position and body yaw. Mapping the local camera behavior requires additional care because camera orientation can later feed body orientation. The static inspection does not establish the entire local camera, interpolation, rewind, or prediction pipeline.

The newer executable separately exposes a mode dispatcher at `0x2D91A0` with a four-case jump table at `0xEFFC050`. The mode 3 branch begins at `0x2D95C5` and also preserves position. This address is newer-build evidence and must not be combined with older PDB addresses.

For outgoing raw movement, the matched `LocalPlayer::sendInput` function at `0x47696A0` calls helper `0x668DE10`. At `0x668E0B3`, the helper copies the effective move vector to payload offsets 52/56. At `0x668E0C7`, it copies effective analog input to offsets 44/48. Starting at `0x668E1B9`, it uses raw analog state when nonzero; otherwise it calls `PlayerMovement::calculateButtonMoveVector` at `0x449420` with raw button state and writes payload offsets 80/84. Therefore the raw movement field represents unscaled raw button/analog input on this path, independently of a filtered movement vector.

The validator entrypoints `MovePlayerPacket::isValid` at `0x2ECE5A0` and `PlayerAuthInputPacket::isValid` at `0x3C8B9C0` offer separate structural validation evidence. The movement agent inspected coordinate bound 33,554,432, delta bound 500, mode/teleport-data consistency, and interaction/block-action/stack-request/vehicle optional consistency. These checks do not prove all higher-level handling semantics.

### GuiDataPickItem is a presentation effect

`ClientNetworkHandler::handle(GuiDataPickItemPacket)` at `0x1308820` reads the packet's item name, effect name, and slot. It calls `GuiData::showPopupNotice` at `0x1563400` and stores the slot in `GuiData::mFlashSlotId` at offset `0x88`, with `mFlashSlotStartTime` at `0x90`. This path does not select the inventory hotbar slot or send equipment. A behavior change that turns this presentation packet into item selection would contradict the inspected handler.

### Incoming item cooldowns

`PlayerStartItemCooldownPacket` is ID 176. Its client handler at `0x130B0C0` reads the category at packet offset `0x30` and tick count at `0x50`, then calls `Player::startItemCooldown` at `0x20DD30` with the send flag false. This path does not echo the cooldown packet.

`ActorItemCooldownsComponent::startItemCooldown` at `0x2A3F810` normalizes unqualified categories with namespace `minecraft`; qualified custom categories are retained. Vanilla short/qualified names are associated through a separate alias map. The component has `mItemCooldowns` at offset 0 and `mVanillaItemCooldowns` at offset `0x40`. It replaces remaining/initial counts with the supplied duration at `0x2A4016F`–`0x2A40175`; no maximum-of-existing-duration behavior is present at those assignments. `getItemCooldownLeft` at `0x2A40660` normalizes the query and returns the remaining count or zero if absent. `Player::isItemOnCooldown` at `0x20E700` tests whether the remaining count is greater than zero.

`Player::normalTick` at `0x1E8570` retrieves the same component through type key `0xC099C998`. Its loop at `0x1E86B2`–`0x1E86D4` decrements each positive remaining count by exactly one. Zero and negative counts are left unchanged. These are simulation tick counts; the inspected loop does not use network input tick values or wall time.

The built-in raw category strings were recovered from the actual static initialization writes rather than inferred from item identifiers:

| Item class | Raw category | Evidence |
| --- | --- | --- |
| EnderpearlItem | `ender_pearl` | 11 bytes at `0xEC3792B`, copied by `0x8BB2FF7`–`0x8BB300A` into the object at `0x11EAA550` |
| GoatHornItem | `goat_horn` | Immediate character writes at `0x24BD72D` and `0x24BD73E`, object at `0x11D67FE0` |
| IceBombItem | `ice_bomb` | Immediate character write at `0x2536591`, object at `0x11D65E08` |
| ShieldItem | `shield` | 6 bytes at `0xEC86618`, copied at `0x304CC8C`–`0x304CCA0`, object at `0x11DBFD80` |

`ItemStack::useOn` at `0x2483260` checks the category's remaining cooldown and skips the item's useOn method when it is positive, returning a failed interaction result. `GameMode::useItem` at `0x259F050` gates the actual item use method for a Use-type cooldown. The same `GameMode::useItemOn` path calls the named `Block::use` function at `0x259E1FC` first. A successful block interaction exits that path without reaching the later `ItemStack::useOn` calls at `0x259E5B1` or `0x259E648`. Thus a held-item cooldown does not prevent a successful block interaction on this path. These observations should not be generalized to suppression of the whole transaction: a failed predicted item use can still have a transaction.

The ordinary air-use transaction lambda at `0x25B6150` and block-use lambda at `0x25B50D0` do not write `ItemUseInventoryTransaction::mClientCooldownState` at offset `0x109`; the field is initialized Off. The use-as-attack lambda at `0x25B6350` does write On at `0x25B64AB` after an Attack-type cooldown query. Thus the existence of this field is not evidence that all air/block transactions should carry On while cooling down. `Actor` offset `0x269`, used in these checks, is `mRemoved`; it is not a client/server discriminator.

### Normal-path empty handlers and other limited cases

The public client handler for ID 16, `ServerPlayerPostMovePositionPacket`, at `0x13216A0`–`0x1321729` performs connection/feature guards and does not read packet payload. The handler for ID 336, `ClientboundTextureShiftPacket`, at `0x132A670`–`0x132A70A` performs assertion guards and returns without reading payload. No gameplay mutation was found in these specific normal paths. This does not rule out alternate packet listeners.

The final client vtable points `SpawnExperienceOrbPacket` ID 66 and `ClientboundDebugRendererPacket` ID 164 to the same single-RET stub, among 53 active packets using that stub. `ClientboundAttributeLayerSyncPacket` ID 345 targets `0x130E3D0`, whose complete two-instruction body writes true to `Packet::mIsHandled` at offset `0x11` and returns; it performs no attribute-layer update. These are separately recorded as return-only versus mark-handled-only, not conflated with a missing public symbol.

Conversely, `SimulationTypePacket` ID 168 is not an empty compiled function in this build: `0x130B740` reads the payload mode byte, skips value 3, and otherwise can update a state byte and call a change callback at `0x130B83A`. Documentation describing a feature as unsupported does not itself establish that its compiled handler is a return-only stub.

The call map also points to real effects for several packets Perlin previously ignored, for example `HurtArmorPacket` calling `Mob::hurtArmor`, `PlayerArmorDamagePacket` calling `LocalPlayer::handleArmorDamage`, and `ClientboundUpdateSoundDataPacket` calling `LevelSoundManager::handleUpdateSoundData`. The broader effects and all dependent state transitions were not fully reconstructed here.

## Continued packet contracts

`v26_51/continued/README.md` and `packet_contracts.json` add bounded static contracts for 21 packet types, including sound update wire quirks, absolute armor damage, recipe unlock consumers, inventory/furnace options, locator state and simulation tick selection. Their claims and unresolved branches are separate from the broad dispatch catalog. The additional `query_pdb_reference.py` exporter reproduces selected matched type, method and symbol metadata from the saved queries.

## Reproduce with your own reference files

The six scripts in `tools/bedrock_reference/` require Python 3.9 or later and use the Python standard library. `map_handler_calls.py` additionally requires GNU `objdump` on `PATH`; this run used GNU Binutils 2.44. No capstone, PDB package, Windows runtime, or proprietary tool is required. Run from the repository root, replacing the quoted EXE/PDB paths with your local inputs. Do not commit those input files.

```sh
python3 tools/bedrock_reference/analyze_minecraft_static.py "/path/to/1.26.51/Minecraft.Windows.exe" \
  --out reference-output/v26_51 \
  --perlin-schema protocol/legacy/bedrock-1.26.51.json

python3 tools/bedrock_reference/pdb_inspect.py "/path/to/Windows_Client_26.51.pdb" \
  --exe "/path/to/1.26.51/Minecraft.Windows.exe" \
  --out reference-output/pdb

python3 tools/bedrock_reference/map_matched_pdb.py "/path/to/1.26.51/Minecraft.Windows.exe" \
  "/path/to/Windows_Client_26.51.pdb" \
  --catalog reference-output/v26_51/packet_catalog.json \
  --out reference-output/v26_51

python3 tools/bedrock_reference/map_client_dispatch.py "/path/to/1.26.51/Minecraft.Windows.exe" \
  "/path/to/Windows_Client_26.51.pdb" \
  --manifest reference-output/v26_51/matched_packet_manifest.json \
  --out reference-output/v26_51

python3 tools/bedrock_reference/map_handler_calls.py "/path/to/1.26.51/Minecraft.Windows.exe" \
  "/path/to/Windows_Client_26.51.pdb" \
  --manifest reference-output/v26_51/matched_packet_manifest.json \
  --out reference-output/v26_51/handler_call_map.json

python3 tools/bedrock_reference/analyze_minecraft_static.py "/path/to/1.26.65/Minecraft.Windows.exe" \
  --out reference-output/v26_65 \
  --perlin-schema protocol/bedrock-1.26.60.json \
  --mojang-json-dir "/path/to/minecraft-bedrock-protocol-docs/json"
```

Keep the scripts next to each other because they import their local PE/PDB parsers. The signature patterns and CodeView record subset were implemented for these supplied builds; another compiler or build can require additional decoding. Counts must be checked against an independent catalog before treating extraction as complete.

## Remaining uncertainty

Catalog equality establishes packet identity coverage. Serializer symbols establish inspection entrypoints. Memory offsets establish in-process layouts. A first-level call map establishes some control-transfer edges. None individually or together proves the full resulting state, ordering, timing, outgoing messages, physics, prediction, inventory rollback, custom content behavior, or all server-dependent branches.

No reference-client network traces, differential replay against an original client, Windows gameplay session, server compatibility run, or anticheat test is represented by these files. The reference inputs are version-specific. The repository's separate audit and tests describe what Perlin actually implements and what was validated; this evidence directory should not be treated as a completed equivalence certificate.
