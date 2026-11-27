# Protocol 2193 bridge audit

This audit compares the generated conversions between protocol 2193 (1.26.51)
and Perlin's current protocol 2225 facade in both directions. The initial generated
snapshot contained 25 packet conversion pairs and 190 nested converter functions.
The complete field paths, affected consumers, evidence, and follow-up actions are
recorded in [bridge-2193-gaps.json](bridge-2193-gaps.json).

The audit establishes concrete conversion losses. It does not establish exact
Minecraft behavior for every packet. Supplied schema differences are separated
from matched executable/PDB evidence, and an unused conversion is separated from
a field consumed by a live handler or sender.

## Resolved with matched 1.26.51 evidence

| Area | Defect | Correction and evidence |
| --- | --- | --- |
| Prediction sync, packet 322 | The old schema called the actor identity an unsigned runtime ID. The generic bridge dropped the current builder's unique ID and serialized zero. | Only the 2193 schema now declares `entity_unique_id` with signed ZigZag64 encoding. The matched PDB identifies `ActorUniqueID`; the native writer calls `BinaryStream::writeVarInt64`. `SendPredictionSync` already supplies the actual unique ID. |
| Extended actor metadata, packets 12, 13, 15, 39 | The old schema put 65 flags in its first 64-bit word. The bridge shifted incoming extended flags left by one and outgoing flags right by one. | Only the 2193 schema now uses the matched 64 + 64 split. PDB values include LayingDown 64, Sneezing 65, Sleeping 76, PushTowardsClosestSpace 109, and UsesLegacyFriction 127. |
| Sound updates, packet 348 | The old schema described seven optional operation fields, and the bridge discarded their payloads. | The matched writer actually emits seven complete variants, each with an unsigned VarInt32 tag. The corrected 2193 schema and explicit bridge preserve the final decoded value; reverse conversion emits seven identical copies. Fade is duration then target volume in both corrected profiles. All seven full-width tags are validated before conversion; invalid values cannot become real controls through narrowing. |
| Camera fade, packet 300 | The old schema omitted both inner option-presence bytes and the bridge then dropped the durations. | The matched optional writers emit a bool for time and a bool for color, with each three-float payload conditional on presence. Only the 2193 schema was corrected to the same nested optional fields used by the current facade. |
| Camera FOV easing, packet 300 | The old schema declared a byte enum, and the bridge omitted enum/string conversion. | The matched writer sends a registered-label string. Only the 2193 schema was corrected to string. Its live FOV consumer now accepts ASCII case variants of the 32 verified names. |

The new `bridge2193_selftest` uses literal payloads, not only codec round trips.
It checks extended-word bits 0, 1, 12, 45, and 63 in both directions, plus complete
prediction-sync bodies with unique IDs +70 and -70. It reproduced 14 failures in
19 checks against the old generated library, then passed all 19 checks against
isolated codecs regenerated from the corrected schemas.

The `MovementProtocol` app selftest additionally distinguishes runtime ID 7 from
unique ID 70 in a real `SendPredictionSync` call and checks that literal extended
metadata reaches local and remote state without shifting. The final full-app test passes; the integration results are recorded in [VALIDATION.md](VALIDATION.md).

The `camera_wire_selftest` compares independent literal payloads in both
directions for 2193 and 2225. It covers all time/color presence combinations,
nondefault values, easing strings, clear state, and truncation. It also checks the
32 registered name/index mappings and limits case-insensitive handling to 2193.
The original 2193 camera schema produced 22 failures in 168 checks; the corrected
schemas pass all 168. The final `ServerUiPackets` app regression also passes. It injects literal 2193 fade and FOV packets and checks public camera state without sleeping.

The `sound_packets_wire_selftest` checks all seven valid events and independent literal encodings. It also places six invalid full-width tags in every record position, checking decode failure and strict-callback isolation. Final results are recorded in [VALIDATION.md](VALIDATION.md). See the [sound wire evidence](evidence/v26_51/continued/README.md),
[camera fade evidence](evidence/v26_51/continued/camera_fade_findings.json), and
[camera FOV evidence](evidence/v26_51/continued/camera_fov_findings.json).

### Camera behavior still outside these fixes

The native FOV enum reader lowercases its registered labels before lookup.
Unknown or empty labels produce reflection context error `0x1000`; the complete
outer packet failure policy was not traced. Perlin keeps its existing linear
fallback for those strings. Its existing lowercase `inverse_lerp` curve also
remains, although that name is absent from the matched registration. The new
case-insensitive matching applies only to the 32 verified names and does not
extend the extra curve. Codec tests that preserve unknown/empty strings establish
wire representation only, not native acceptance.

Fade defaults, invalid duration/color handling, visual transitions, and other
CameraInstruction fields have not been established as fully equivalent. No
speculative disconnect behavior or renderer change was added.

## Remaining conversion gaps

| Packet / area | Direction | Observed loss and current use | Status |
| --- | --- | --- | --- |
| 345, environment attributes | Both | Noise name, transition timing/easing, and mode information are lost. The new discriminator defaults to Constant. | Structural loss; the receive packet is currently registered as a no-op. |
| 137, education settings | Both | Two old presence flags are not preserved by the current nested optional representation. | Structural loss; no semantic receive handler or sender found. |
| 63 / 93, PlayFab ID | Both | The old skin member is dropped rather than relocated to the current player record. Standalone current skin has no equivalent member. | Representation loss; rendering/tab consumers do not read the ID. Server impact is unverified. |
| 315, diagnostic categories | Both | Unknown category labels fall back numerically and can become a different category, such as current `blobs=7` becoming old `cereal=7`. | No live diagnostics sender found. |
| 144 / 147, CraftReserved | Current to 2193 | Unsupported current action 16 falls back to old NonImplemented 16 and loses its payload. | No current live builder emits CraftReserved; it needs explicit version gating before use. |

## Version additions and limits

Some absent fields are expected from the supplied versioned schemas: editor
migration state, block passengers, client biome-update state, cloud rendering
fields, animation hand, and the hand on entity-use/release transactions. These
are documented separately in the JSON inventory. Live offhand entity-use/release
builders still need version-specific feature verification. The ordinary
`TransactionUseItem.hand` field exists in both schemas and is preserved.

The inspected ordinary PlayerAuthInput fields and ItemStackResponse fields have
no detected field loss in this bridge snapshot. That result does not prove their
handlers or response timing are equivalent to Minecraft. The converter audit also
found no unmatched or ambiguous non-void switch alternatives; flattened payloads
still need discriminator review, as the environment-attribute finding shows.

Protocols 944, 975, 1001, and 2168 were not changed from evidence for 2193. Their
suspect metadata split and prediction-sync identity schemas require their own
version-appropriate validation. Transmission of ActorFlags above global bit 127
and the matched PDB/schema discrepancy in high ActorDataIDs also remain unproven.

Exact compatibility requires version-matched field semantics, handler state
transitions, ordering, and controlled differential execution. A successful
generated codec round trip or one passing regression cannot establish all of
those properties.
