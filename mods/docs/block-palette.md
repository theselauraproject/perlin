# Sequential block palettes

`resources/block_states.json` contains the canonical
`6.9.0+bedrock-1.26.50` palette, source commit
`508b8791188868f2d92a55be8b2be2e771a6a171` (CC0-1.0).
Each record contains `[state hash, block name, typed states, data-driven]`.
The final flag is derived from names in the matching `block_definitions.nbt`.
NBT byte states remain booleans, integer states remain integers.

A sequential server only assigns IDs to data-driven vanilla blocks that it
announces in StartGame. Exclude undeclared definitions, merge the declared
states, and sort blocks by unsigned FNV-1 name hash, preserving state order.
Kestrel's `BlockAssets::sequentialMap` provided the reference for this rule.

The complete 1.26.50 palette contains 22,091 states with air at 17,025.
Excluding its 98 data-driven definitions leaves 20,674 states with air at
15,844. Lifeboat uses the latter air ID. Its StartGame reports sequential IDs,
version `*`, a zero palette checksum and a PocketMine engine, so the advertised
server version alone does not identify the actual palette membership.

The regression test checks both memberships and switching between servers.
