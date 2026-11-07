# TODO

Open work on the `ui/json-ui` branch, roughly in priority order.

## JSON UI (server screens)
- [ ] Hive in-match scoreboard does not show (sidebar during games). Needs a capture: join a match, then check
      `perlin-run.log` and `ui_generated/hud.rml`.
- [ ] Hive upgrade / shop screens and the remaining Hive forms: replay each from `ui_generated/forms/<title>.json`
      (`PERLIN_TEST_FORM` + `PERLIN_TEST_SERVER_PACKS` + `PERLIN_SHOW_SCREEN=server_form.third_party_server_screen`).
- [ ] "HIVE GAMES" title position: compare against a real Bedrock screenshot at the same GUI size; MinecraftTen
      baseline was tuned by eye (`TenFont::Ascent` = 6 x scale).
- [ ] Glyph pages (private-use icons) may render too small compared with Bedrock.
- [ ] Hive / CubeCraft chest and upgrade screens: verify item slots after the `$binding` substitution fix.
- [ ] NPC dialogues (`NpcDialoguePacket`) are parsed but never shown; needs `NpcRequestPacket` to answer.
- [ ] Perlin UI scale vs Bedrock GUI scale (affects where every server screen lands).

## Rendering
- [ ] Custom blocks with their own geometry: texture mapping still wrong (flower pot shows misplaced UVs). Check
      `ui_generated/custom_blocks.json` (server definitions as received) against `BlockGeometry::ParseComponentSet`.
- [ ] Custom entities (e.g. Hive logo sign) render with the right shape but wrong texture mapping.
- [ ] Snow and rain look wrong.
- [ ] Arrows fly facing the wrong way.

## Gameplay / parity
- [ ] Bedrock first-person swing: take the real curve from `ItemInHandRenderer` in the 1.19 IDA database (current
      swing is the old Java-style one without the dip).
- [ ] Held placement / bridging: verify on a live server (port of `_tickBuildAction` / `continueBuildBlock`).
- [ ] Slime and bow-slowdown corrections on server-authoritative servers.
- [ ] Reach: entity/block pick range was set to the creative value by an earlier session; confirm it matches vanilla.
- [ ] CubeCraft "Blocked Connection" (C626956) on login, with and without VPN: find out whether it is the account
      or the client (try the official client with the same account).

## Engine
- [ ] Perlin hangs on shutdown joining SDL's USB input thread (`AppPlatform_SDL::Shutdown`); needs SIGKILL.
- [ ] Remove or document the dev hooks before release (`PERLIN_SHOW_SCREEN`, `PERLIN_SHOT`, `PERLIN_TEST_*`).
