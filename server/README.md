# Perlin server

Server software that lives inside the Perlin tree but builds on its own. One codebase, three ways to run it:

| Target | What it is |
| --- | --- |
| `perlin-server` | Dedicated server, like Bedrock Dedicated Server. Reads `server.properties` (the same keys as BDS), `allowlist.json` and `permissions.json`, loads plugins, takes console commands on stdin. |
| `perlin_server_embed` (static) | The same server behind a small C API (`include/perlin/server.h`), linked into the client on every platform for singleplayer. `perlin_server` (`.dll` / `.so` / `.dylib`, standalone builds only) exposes that API to other hosts. |
| `perlin_server_core` | Static library both of the above are made from. |

```
cmake -S server -B build-server && cmake --build build-server -j
./build-server/perlin-server --data run        # writes run/server.properties, allowlist.json and permissions.json on first start
ctest --test-dir build-server
```

From the root tree the server is built by default (`PERLIN_BUILD_SERVER`) on every platform the client runs on: Windows, Linux, macOS, Android, iOS and the web. Its sources compile once into an object library; `perlin_server_embed` (no wasm runtime of its own: it uses the client's WasmEdge or wasm3 behind the same wrapper) is linked straight into the client, so singleplayer needs no file next to the game and no library loading, and `perlin_server_core` (with wasm3) backs `perlin-server` on desktop. `cmake --install` puts `perlin-server` in the package with the block tables in `resources/`. The web build leaves out the NetherNet listener (no WebRTC stack in the page); singleplayer only needs the in-process loopback connection. Needs a C++23 compiler, Python 3 (the Bedrock packet codecs are generated from `protocol/` by the client's own `tools/protogen`, so the server speaks exactly what the client decodes), and network access for the first configure (libdatachannel, cpp-httplib, miniz, wasm3 through FetchContent). Options: `PERLIN_SERVER_NETHERNET` (default ON), `PERLIN_SERVER_GPU` (OFF, see below), `PERLIN_SERVER_BUILD_SHARED`, `PERLIN_SERVER_BUILD_EXE`.

## Configuration

`server.properties` is the BDS file: same keys, same order, same defaults (written in full on first start), plus a few `perlin-*` keys for what BDS does not have (`perlin-generator` flat or vanilla, `perlin-terrain-backend`, `perlin-plugin-dir`, `perlin-block-data-dir`, `perlin-flat-layers`, ICE settings). Keys this server does not act on are kept when it rewrites the file. A directory with only the old `server.json` still loads, and `server.properties` wins when both exist.

Applied: `server-name`, `gamemode`, `difficulty`, `max-players`, `online-mode`, `allow-list`, `server-port`, `server-ip`, `server-udp-ports` (a shared port, a range, or `public:local`; see below), `transport`, `enable-lan-visibility`, `view-distance`, `tick-distance`, `player-idle-timeout`, `max-threads`, `level-name`, `level-seed` (empty is random, a number is used as is, other text is hashed like Java's `String.hashCode`), `default-player-permission-level`, `compression-threshold` (default 1, as BDS), `chat-restriction`.

Read and kept, not acted on yet: `force-gamemode` and `allow-cheats` (nothing is saved to apply them to), `server-portv6`, the `server-authoritative-*` and position thresholds, ticking-area limits, content log, script debugging, `disable-persona`, `disable-custom-skins`, `enable-packet-rate-limiter` (and `packetlimitconfig.json`), `server-build-radius-ratio`.

Differences from BDS that are deliberate or forced: `transport` takes `nethernet` (the default: RakNet and NetherNet together), `both` (the same), `raknet` (RakNet only) or `nethernet-only`; `compression-algorithm=snappy` falls back to zlib with a warning; `block-network-ids-are-hashes=false` is ignored; `client-side-chunk-generation-enabled` is read but the server always sends it off, since the client would generate its own terrain outside the server's radius; the public half of a `server-udp-ports` mapping is parsed but WebRTC does not announce it, so forward the same port numbers; `online-mode` defaults to false and `allow-list` to false here, where BDS defaults both to true.

## Transport: RakNet and NetherNet

Regular Bedrock clients (Windows, consoles, phones) join over RakNet: add the server with its address and port, or find it in the Friends tab's LAN list. The server answers RakNet pings on UDP `server-port` (and `server-portv6` for IPv6) with its name, version and player count, runs the RakNet handshake (protocol 11, no cookies), and carries game batches as `0xfe` messages with reliable ordering, splitting, acks and resends. The Perlin client can use either transport.

NetherNet stays available for the Perlin client's direct join:

1. `GET /v1/join` returns the server info (name, protocol, version, players).
2. `POST /v1/join/<id>` carries the client's SDP offer; the answer comes back in the response.
3. Two WebRTC data channels, `ReliableDataChannel` and `UnreliableDataChannel`, carry game batches. Each message is `[fragments still to come][payload]`, 262143 bytes per fragment.

DTLS already secures NetherNet, and the Bedrock encryption handshake is not used on either transport, which every client accepts. Compression is raw deflate with a 256 byte threshold.

Logins: a client's identity is read from the multiplayer token (`Token`, issued by Minecraft's authorization service since 1.26.10) or from the older certificate chain. With `online-mode=true` the token's signature is checked against the authorization service's published keys, fetched at start and every 30 minutes.

Reachability: forward UDP `server-port` (default 19132) for RakNet. NetherNet uses TCP on the same port for signalling, and with RakNet on, its WebRTC traffic moves to UDP `server-port + 2` (19134) so the two do not collide; forward that too for NetherNet players. `ice_udp_mux` (default on) puts every NetherNet player on that one UDP port. `ice_servers` takes `stun:host:port` / `turn:` URLs, or `["none"]` for LAN only. `ice_bind_address` pins the interface (needed on hosts without IPv6).

The in-process `LoopbackListener` skips the network entirely and is what singleplayer uses.

## What works

- Bedrock 1.26.60 (protocol 2225) login: network settings, login, resource pack negotiation (no packs offered), start game, chunk radius, chunk streaming, spawn.
- Vanilla terrain by default (below); with `perlin-generator=flat`, a flat world (`perlin-flat-layers` in `server.properties`, default bedrock / 2 dirt / grass, surface at y -60), sections that are one block are stored and sent as a single value.
- Multiplayer: any number of sessions, player list, other players appear with their own skin, movement is relayed, join and leave messages.
- Chat (`Text`) and commands with Java's syntax, one overload list per command, tab completion through `AvailableCommands`, and Java's error messages (`Unknown or incomplete command, see below for error`, the `/cmd<--[HERE]` marker, `Expected integer`, `No player was found`, `Unknown item 'minecraft:x'`). Arguments are typed (integer, float, word, text, selector, position with `~` and `^`, item, block, biome, enum). Selectors: `@s @p @a @r @e` with `[name= distance= x= y= z= limit= sort= gamemode= type=]`. Commands: `/help /list /say /me /msg /tell /w /tp /teleport /kick /ban /pardon /stop /give /gamemode /defaultgamemode /op /deop /allowlist /whitelist /permission /tick /time /weather /difficulty /gamerule /setworldspawn /spawnpoint /seed /title /locate /flyspeed /plugins`. A command an ordinary player may not use answers with the unknown-command message, as in Java. Not there: `/kill /effect /xp /summon /fill /setblock /clone` and anything about entities, since the server has none.
- Spectator mode (`/gamemode spectator`, game type 6): the player flies through blocks, cannot build, break, fight or open things. While spectating the mouse wheel in the Perlin client cycles the fly speed 1x, 2x, 5x, 10x, 20x (`/flyspeed <1|2|5|10|20>` does the same from chat); the server sends it as the ability's `flySpeed`. Other players are not hidden from or by spectators.
- `/locate biome <biome>` and `/locate structure <structure>`: biomes are searched in expanding rings with the generator's own climate (on worker threads, so the tick does not wait); structures do not exist yet and say so.
- Permissions and the allowlist use BDS's files and formats. `permissions.json` is `[{"permission": "operator", "xuid": "2535..."}]` with `operator`, `member` or `visitor`; players not listed get `default-player-permission-level`. `allowlist.json` is `[{"ignoresPlayerLimit": false, "name": "Steve", "xuid": "2535..."}]`, used when `allow-list=true` (off by default here; BDS ships it on). As in BDS, a `whitelist.json` that exists is read instead of `allowlist.json`, and an old `ops.json` is imported into `permissions.json` once. An entry matches by Xbox id when both sides have one, otherwise by name; a name-only entry gets its xuid filled in on the first join. `/allowlist <on|off|add|remove|list|reload>` (`/whitelist` still works), `/permission <list|reload>`, `/op` and `/deop` (which write `operator` and `member`) manage them. Visitors cannot build, break, fight or open things. In embedded singleplayer the one player is an operator automatically but keeps the world's game mode (survival by default).
- Creative inventory with every block item, grouped into the vanilla tabs; survival attributes (health, hunger, experience) so the client's HUD has something to show; arm swings, emotes, skin changes and sounds are relayed to other players.
- Building: the creative menu (every block the client knows, from its own `block_states.json`), placing, breaking, pick block, the 36-slot inventory with cursor, hotbar selection, swap / take / place / drop / destroy through the server-authoritative item stack requests (with stack ids and atomic rollback). Reach is checked, plugins can cancel a place or break, and a refused action sends the real block back so the client undoes its guess. In survival, placing spends one item and breaking pays the block straight into the inventory; bedrock does not break.
- Dimensions: the Nether and the End, generated with Java 26.3's own noise settings, legacy random, biome sources (the five-point nether table, the End's island erosion), material rules and nether caves, block for block equal to the real generator on every compared chunk. Each dimension has its own world; players, dropped items and containers belong to one, chunks are sent with the dimension's height (Nether 0..127 in 8 sub chunks, End 0..255), and moving between them uses `ChangeDimension` and waits for the client's acknowledgement. Dying elsewhere respawns in the overworld; falling 64 blocks below a dimension's floor hurts.
- Nether portals: an obsidian frame (inside 2x3 up to 21x21) lit with flint and steel or a fire charge fills with portal blocks; breaking the frame or a portal block clears it. Standing in one for 80 ticks (1 in creative, or `playersNetherPortalDefaultDelay`) travels at 1:8, to a known portal within 128 (overworld) or 16 (nether) blocks, or to a new one built on dry ground nearby (on an obsidian ledge when there is none).
- End portals: twelve eyes of ender in a ring of end portal frames open the 3x3 portal; it leads to the obsidian platform at 100 48 0 in the End, and the End's portal back leads to the world spawn (there is no dragon fight, so the way home is open from the start).
- `/execute` with `as`, `at`, `positioned [as]`, `rotated [as]`, `facing [entity] [eyes|feet]`, `anchored`, `align`, `in <dimension>`, `if|unless block|entity|dimension` and `run`, nestable, running the command with the original sender's permission. `/tp`, `/setblock`, `/fill` and `/locate` act in the dimension the command runs in, and `@p` and selectors with a position or distance only see that dimension. Block arguments take any block state, with or without an item (`end_portal_frame[direction=2]`).
- Falling blocks: sand, red sand, gravel, suspicious sand and gravel, every concrete powder, anvils and the dragon egg fall as `minecraft:falling_block` entities when the block under them is air, liquid or a replaceable plant, two ticks after a change next to them, and land as a block again.
- TNT: flint and steel or a fire charge on TNT primes it (80 ticks, it hops and falls); the explosion casts Java's 16x16x16 rays at power 4 against each block's blast resistance, primes other TNT it reaches with a 10 to 30 tick fuse, hurts and knocks back players and mobs within 8 blocks, and does not break blocks under water or when `tntExplodes` is off. Creepers use the same explosion at power 3 (blocks only with `mobGriefing`). Both are overworld only for now.
- WASM plugins.
- Xbox authentication (see below), GPU terrain (see below).

Not modelled on purpose: entities other than players, so no item entities (a dropped stack is simply gone), no mobs, no crafting grid or recipes, no containers, no block states chosen by the placer (a placed block has its default state: logs face up, stairs do not rotate), no armour.

## Xbox authentication

`online-mode=true` in `server.properties` (`"xbox_auth": true` in the older `server.json`) makes the server accept only Xbox Live identities. Every login's certificate chain is always checked (ES384 signatures, each link signed by the key the previous link named, expiry, the client data signed by the last key); `xbox_auth` decides what a valid chain is worth:

- **off (default):** the chain only proves the player holds the key it names. The name is a label anyone can pick; no Xbox id is trusted.
- **on:** the chain must pass through a trusted root key (Mojang's, or the keys in `auth_root_keys`), the name and Xbox id come from the links after the root's signature only, and over NetherNet the offer's `a=identity` assertion must be signed by the player's key and cover this connection's DTLS fingerprints, so a captured login cannot be replayed from another machine. Anything else is disconnected with "not authenticated" (the client words it as needing an Xbox login).

Operators can then be listed by Xbox id (an xuid in `permissions.json`); with `online-mode` on, a name in the list no longer counts, since names are not unique to an account.

Checked with locally generated keys against a custom root, including a forged self-signed link, tampering, expiry and a replayed login. **Not checked against a real Microsoft-issued chain**: the built-in Mojang root keys are two constants (current and pre-2020); they parse as valid P-384 keys, but I could not confirm the current one against a live login. If real players are refused, set `auth_root_keys` to the key the client's chain is signed with. The newer OpenID `Token` is not verified; the client still sends the certificate chain, which is what is checked.

## Plugins

Same shape as client plugins: a `.wasm` in `plugins/` (optional `name.json` with `name`, `version`, `author`, `description`), the same wrapper the client uses, on wasm3 (the client itself runs plugins on WasmEdge on desktop; both load the same modules), one import `perlin.call(op, in, in_len, out, out_cap)` and the exports `perlin_alloc`, `perlin_free`, `perlin_subscriptions`, `perlin_event`. The contract is the shared ABI table, `mods/sdk/perlin/abi_table.h` (C names in `mods/sdk/perlin.h`), the same one client plugins use: events (enable, disable, tick, join, quit, chat, command, block change, block break, block place) and ops (log, broadcast, message, kick, register command, players, teleport, set/get block, give item, server info).

`examples/hello_server` is a complete plugin in freestanding C: welcome message, a chat filter that cancels a message, and a `/hello` command. `examples/hello_server/build.sh` needs only clang with the wasm32 target and `wasm-ld`.

A plugin that traps is disabled and logged; the server keeps running.

## Singleplayer in the client

The title screen has a **Singleplayer** button. Hovering it shows a note that singleplayer is in very early development and not playable yet. Clicking it starts the server inside the client (`client_bridge/EmbeddedServer` calls the linked-in C API, loopback only, you are the operator), then joins it through `BedrockSession` over an in-process transport. The client writes its own `block_states.json` and `block_destroy.tsv` into `<data dir>/singleplayer/` for the server to read.

It is part of the normal build; configure with `-DPERLIN_BUILD_SERVER=OFF` to leave it out, and the button then explains that this build has no singleplayer. The bridge and the linked-in server are tested end to end (`server_embed_test`); the button itself and the full client join were **not run**, only compiled.

## Terrain backends and the GPU

World generation goes through `TerrainBackend` (`src/world/Generator.hpp`): a batch of chunk positions in, columns out, same result for the same seed and position. `ChunkManager` runs it on a worker thread and asks for `PreferredBatch()` positions at a time.

`perlin-terrain-backend` in `server.properties` (`terrain_backend` in `server.json`): `cpu`, `gpu`, or `auto`. The GPU backend (`src/gpu/GpuTerrainRhi.cpp` and `src/gpu/shaders/terrain_flat.slang`) runs a compute pass on the client's RHI: one thread per cell for up to 16 chunks per dispatch, one readback, the block a function of (x, y, z, seed). `auto` takes it only if it starts on a hardware device **and** matches the CPU reference on a probe batch; otherwise it falls back and logs why.

Build it with `-DPERLIN_SERVER_GPU=ON` (needs `slangc`, fetched unless `PERLIN_SLANGC` is set). Only the Vulkan shader is compiled; D3D12 and Metal need their shader targets added to the pack and their bindings checked.

**Verified on a software Vulkan device (SwiftShader), not on a real GPU:** the output equals the CPU's for 108 chunks across several batches and negative coordinates cell by cell, and a full two-player session runs on it. Its speed on real hardware is unmeasured.

**For a flat world the GPU does not help**: the CPU fills a whole uniform section with one store, so dispatch, readback and copy-out only add time (on SwiftShader, 127 ms against 0.5 ms for the same 108 chunks). That is why `auto` keeps flat terrain on the CPU. The backend exists for generators that do real work per block (noise, caves); `terrain_backend: "gpu"` forces it, and `BackendsAgree` is the check every future generator has to pass against its CPU twin.

**Vanilla terrain** (`perlin-generator=vanilla`) needs no files and no setting: its data is compiled in. Where each part comes from (looked up in this order: bedrock-samples, the dedicated server package, then Java):

- **bedrock-samples**: what each biome is made of (`behavior_pack/biomes/*.biome.json`, `minecraft:surface_builder`: top, mid, foundation and sea floor materials, sea floor depth, builder type), with Mojang's documentation for what those fields mean. Baked in by `tools/gen_biome_tables.py`.
- **the dedicated server package**: nothing usable for terrain. Its versioned behavior packs hold the ore features and feature rules (used, below); it has no terrain noise, density or placement data.
- **Java 26.3**, hardcoded: the shape of the overworld (density functions, noises, noise settings, 49 KB deflated, `tools/gen_overworld_shape.py`) and where biomes go (the 7,594 climate boxes of `OverworldBiomeBuilder`, dumped by `tools/javaref/BiomeDump.java`, which runs the game's own class). Neither bedrock-samples nor the server package has these; Bedrock's own live inside the closed binary. Java's biome names are mapped to Bedrock's (56 biomes, `tools/gen_biome_tables.py`). These generated files hold Mojang's data, so check the Minecraft EULA before redistributing builds that contain them.

The pipeline has four stages, each with a GPU implementation (`src/gpu/shaders/terrain_vanilla.slang`, host in `src/gpu/GpuVanillaRhi.cpp`, `VanillaGpuProgram.*`) and a CPU reference (`src/world/vanilla`):

1. **density**: `final_density`, in two passes per batch of up to 16 chunks: the 214 expensive nodes at every cell corner (5 x 5 x 49 per chunk), then the 17 per-block nodes with the trilinear blend from those corners.
2. **biome**: per column, the ground height from the densities, the six climate functions at the column's quart cell at that height, and the nearest of the 7,594 boxes.
3. **surface**: Java's aquifers decide water, lava, air and stone barriers below the surface (`VanillaAquifer.cpp`), then Java's material rules (`worldgen/material_rule/overworld`, run by `VanillaMaterials.cpp`) choose every surface block: grass, dirt, sand and gravel by depth and water, stone and snow on peaks, terracotta bands in badlands, deepslate below y 0 with its gradient, the bedrock floor gradient, eroded badlands pillars, icebergs in frozen oceans, and the copper and iron ore veins with their granite and tuff. Biome conditions use the column's surface biome. Then Java's cave and canyon carvers cut through (`VanillaCarvers.cpp`), filling with what the aquifer says, and dirt exposed under carved grass gets its surface material. The old Bedrock-material path is still used by the GPU surface stage and with `PERLIN_NO_SURFACE_RULES`.
4. the host turns block ids into chunk sections, after the **ore features** below have been placed into them.

**What runs on the GPU now, and what is still on the CPU.** With the Java-exact surface (the default), the GPU computes `final_density` for every block and the six climate values at every quart cell of the chunk (phase 4), and the CPU takes them from there: the biome search with Java's tie order, aquifers, material rules, carvers and decoration. The GPU's old Bedrock-material surface stage is only used with `PERLIN_NO_SURFACE_RULES`. The Nether and the End use the same kernel: the cell grid, height and floor are push constants, and the kernel knows `slice`, `distance_to_point` and `end_outer_islands` (a 2D simplex in doubles, so the islands match the CPU exactly). `perlin-terrain-backend=gpu` or `auto` puts each dimension on the GPU when its probe chunks match the CPU bit for bit (density and climate); otherwise that dimension stays on the CPU. Checked on llvmpipe (a software Vulkan device): density, climate and every block of the overworld, Nether and End test chunks identical to the CPU. Still CPU-only: aquifers, material rules, carvers and features.

**Ores** come from Java's `underground_ores` decoration step (`ore`, `scattered_ore` and `underwater_magma` features in `JavaDecorator.cpp`, with the 26.3 rule tests). The Bedrock-rule ores below are only used when decoration is turned off (`PERLIN_NO_DECORATION`).

**Ores from Bedrock's feature rules** (`src/world/vanilla/VanillaFeatures.cpp`). The dedicated server's behavior packs hold the modern `ore_feature` definitions (block counts, `discard_chance_on_air_exposure`, which blocks each ore replaces: stone gets the plain ore, deepslate the deepslate ore) and their `feature_rules` (iterations per chunk, uniform or triangle y ranges, `scatter_chance`, coordinate order, `has_biome_tag` filters). `tools/extract_bds_features.py` unpacks and merges the packs' brarchive files (newer versions win), `tools/gen_features.py` bakes the 24 ore features and 30 active rules (rules with 0 iterations are deprecated and dropped) into `FeatureData.cpp`, with each rule's biome filter already evaluated against bedrock-samples' biome tags into one bit per biome. That covers coal, iron (upper, middle, small), copper (and the dripstone variant), gold (and badlands, lower), redstone, lapis, diamond (four rules), emerald in mountains, clay in lush caves, gravel, granite, diorite, andesite, tuff and silverfish blocks in mountains. A blob that starts in a neighbouring chunk is placed in this one too; each blob draws all its random numbers before any test, so neighbours agree on the seams. Runs on the CPU for both backends (about 20 ms a chunk); the GPU returns the biome of each column so the filters can be applied.

The kernel is float32 for values and float64 for coordinates (as Java), every float operation is `precise` (checked in the compiled SPIR-V: no multiply can fuse with an add), and divisions go through double so they round exactly (Vulkan allows 2.5 ulp for a float divide). It needs a device with 64-bit shader floats, which many integrated GPUs lack. The graph must fit the kernel (at most 512 nodes, splines nested at most 7 deep, 4 x 8 cells); the overworld's is 233.

**GPU first, CPU when a stage can't.** `perlin-terrain-backend`: `auto` and `gpu` try the GPU, `cpu` forces the reference. At start the GPU backend generates five probe chunks (including (-2048, 2047) and (100000, -100000)) and checks each stage against the CPU on its own: densities bit for bit (chunk (0,0) in full, the rest sampled), then every column's ground height and biome, then every block. A stage that disagrees or cannot run drops to the CPU while the earlier stages stay on the GPU, and the log says which (`gpu: density, biome; cpu: surface`). If even the density stage fails, or there is no device, `auto` uses the CPU entirely and logs why, and `gpu` refuses to start. If the device stops answering mid-run (30 s per submit) everything falls back to the CPU for the rest of the session.

**What was verified, and what was not.**
- The CPU biome search matches Java's own `findValueBruteForce` on all 30,000 reference samples (`tools/javaref/biome_lookup_ref.txt`, half placed on box edges), and the density graph matches the real engine on all reference values.
- The kernel source was compiled by Slang to C++ and run against the CPU reference on 8 chunks (including far ones) for 10 seeds: 0 differences in 126,304 density positions, 2,048 columns (ground height and biome) and 786,432 blocks per seed. That checks the algorithm, the table packing and the four-phase scheme (`tools/kernel_on_cpu/run.sh` repeats it).
- It was **not run on a GPU**: this build environment has no Vulkan device with 64-bit shader floats (SwiftShader does not). The buffer upload, barriers, push constants and readback of the host code, and the float behaviour of real hardware (fused operations, denormals, divide), are untested; the per-stage start-up check exists to catch exactly that. Speed on a real GPU is unmeasured. Only the Vulkan shader is in the pack.

**Where it departs from Bedrock and Java** (so nobody mistakes it for either):
- Terrain shape is Java's, which Bedrock's resembles but does not reproduce for the same seed.
- Java's R-tree breaks ties between climate boxes by what it found last on that thread, so exact edge hits can't be matched; the first box in the table wins here. Real noise values rarely land exactly on an edge.
- Java blends biomes at block level with a seeded jitter; here each column reads the biome of its 4x4 cell, so biome borders are straight.
- Surface depth is a flat 1 + 3 layers, not Bedrock's or Java's noise-varied depth; badlands have no terracotta strata; `surface_material_adjustments`, mountain parameters and the frozen-ocean icebergs from the biome files are not applied; there are no aquifers, carvers, trees, flowers, grass, structures or other features.
- Ores: the counts, heights, rules and biome filters are Bedrock's data, but Bedrock's ore code is closed, so the blob shape is the classic capsule chain and the random numbers are our own: veins are not the same as a real Bedrock world's, and the air-exposure test only looks at neighbours inside the chunk. A rule limited to some biomes tests the biome at the blob's start, or at the nearest column of the chunk being built when the start lies in a neighbour, so such an ore can be cut off at a chunk edge. Everything else in the packs (trees, flowers, grass, cacti, kelp, structures) is native code in Bedrock, not data, so it cannot be ported from Bedrock's files.

## Not done / known limits

- LAN discovery (UDP 7551, so the world shows up in a vanilla client's Friends tab) is not implemented: its packet layout is only partly documented and could not be checked against a real client here.
- Terrain: `perlin-generator` is `vanilla` (default, also in singleplayer) or `flat`. The shape of the overworld is Java's, checked bit for bit against the real engine; the biome and surface stages are new (above). Not ported: Bedrock's own terrain (closed), biome blending, structures, Nether and End decoration (glowstone, fungi, chorus, end cities) and the remaining overworld features.

- No world persistence: chunks are regenerated each run, edits are lost.
- Resource packs are not served; `BiomeDefinitionList` and `AvailableEntityIdentifiers` are sent empty. Everything is tested against the client's own packet codecs and chunk decoder, **not against the running Perlin client or a vanilla Bedrock client in a game**.
- Player position is the eye position the client reports; it is relayed as is.
- Survival block breaking trusts the client's stop-break message (no dig-time check).

## Tests

`ctest --test-dir build-server` runs: units (JSON, block hashes checked against every state in the client's `block_states.json`, chunk encoding decoded with the client's own `ChunkCodec`, batches, fragments, config, the inventory rules, login verification, the plugin host with the real example plugin); a scripted two-player session over loopback (handshake, 49 chunks, chat, commands, plugin, movement, creative menu, place, break, pick, survival rules, leave); the C API; the client's embedding bridge against the linked-in server; and a real WebRTC client against the NetherNet listener (probe, SDP offer, data channels, login, chunk delivery, and the `xbox_auth` cases). With `PERLIN_SERVER_GPU=ON` there is also `server_gpu_test`; it needs a Vulkan device (for software Vulkan, `VK_ICD_FILENAMES` can point at a SwiftShader or lavapipe ICD) and skips itself without one. `PS_TEST_BACKEND=gpu` runs the whole session test on the GPU backend.
