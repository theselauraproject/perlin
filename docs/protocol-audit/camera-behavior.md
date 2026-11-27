# Camera packet behavior and evidence

This work connects received camera state to Perlin's rendered camera, view effects,
audio listener, and input controls. It does not establish equivalence with every
Minecraft camera mode or every protocol revision. The native behavioral reference
is the supplied **Minecraft Windows 1.26.51.1 executable and matching PDB**, protocol
**2193**. Current protocol **2225** and older profile **944** also have packet and
application regressions, but passing the same tests does not prove that all native
behavior is identical across these versions.

The matching executable SHA-256 is
`0245c20c4377d74a25f3644bf441b460d6b3b45a88617387edccaacd27418377`.
The PDB GUID is `0353b4a9-3eaa-e2e2-4c4c-44205044422e`, age 1. Evidence files
contain metadata, addresses, and summarized contracts; the proprietary executable,
PDB, and disassembly are not distributed with this source tree.

## Packet-to-consumer coverage

| Packet | Fields and resulting behavior | Qualification |
| --- | --- | --- |
| `CameraPresets` (198) | Preset identity and parent, independent position/rotation axes, view/entity offsets, orbit and tracking settings, limits, starting rotation, audio listener, player effects, and control scheme reach `CameraEffects`. | Inheritance preserves an absent axis independently of present axes. Aim-assist metadata is retained separately; retaining it does not implement target selection. |
| `CameraInstruction` (300), Set | Preset selection, position, rotation, facing, easing, view/entity offsets, `default`, and `removeIgnoreStartingValues` feed the active camera controller. | An invalid preset index leaves existing pose state intact; other valid fields in the same packet still apply. |
| `CameraInstruction`, Clear | Present `true` clears active camera overrides. Present `false` does nothing. | Stored per-preset command position/rotation overrides survive Clear and can be selected again. |
| `CameraInstruction`, Fade | Present fade starts an animation, even when both nested fields are absent. Received times and RGB reach the screen tint. | The matching native default is black with times `(1, 0.5, 1)` seconds. The old flat representation is bridged explicitly. |
| `CameraInstruction`, FOV | Field of view, transition duration and easing, and clear reach the projection consumer. | The 2193 normal archive stores easing names as strings; byte fixtures cover the verified representation. |
| `CameraInstruction`, Target | Signed unique entity ID and optional offset reach live entity lookup and camera orientation. `removeTarget=false` preserves a target. | Tracking, limits, and fallback presentation are implemented by Perlin's controller; their complete native numeric behavior is not established. |
| `CameraInstruction`, Attach/Detach | Attachment is retained and applied by boom camera modes; detach requires present `true`. | The matched path operates on cameras with a third-person boom component. It does not justify translating an ordinary stationary free camera. Exact actor-locator and boom-avoidance behavior remains bounded. |
| `CameraSpline` (338) | Definitions replace the named spline registry, preserving definition order and full path/keyframe data. | Named lookup and subsequent playback are real consumers, not a metadata-only cache. |
| `CameraInstruction`, Spline | `loadFromJson=true` selects a registered definition; false/absent selects inline data. | A named path uses its registered duration and points, even when the instruction also includes inline fields. Missing names and explicit unknown types preserve the active path. |
| `CameraShake` (159) | Add starts a shake and Stop removes it. Unknown actions leave it unchanged. | Stop removes all shake state in the matched handler. Perlin's noise and shake transform are not a recovered native numeric implementation. |
| `ClientboundControlsScheme` (327) | With a local player, updates the input scheme and queues the native strafe pitch-reset event for schemes 2 and 4. | Repeated equal packets still request a reset. The request is consumed once by the player update. |

`CameraEffects::Reset()` is invoked on a new connection, transfer/reset of player
state, and `BeginReplayMode`. Camera state, preset/spline registries, attachments,
targets, view effects, and control overrides therefore do not intentionally carry
over from a previous server. Preset and named-spline accessors return snapshots
under the camera mutex, avoiding an index lookup followed by an unrelated registry
read.

## Preset inheritance and command state

Position and rotation components remain optional throughout packet conversion.
For example, a child with only `pos_x` and `rot_y` inherits its parent's `pos_y`,
`pos_z`, and `rot_x`. This follows the documented camera hierarchy and fixes the
former all-or-nothing vector conversion. Built-in free camera defaults are a fixed
world-space position and rotation; they are independent of the player's changing
pose. The standard perspective, follow-orbit, and fixed-boom modes use separate
controller paths. See Microsoft's [camera-system introduction][camera-intro] and
[control-scheme rules][controls] for the creator-facing concepts.

The matching native Set consumer preserves prior command position/rotation when
those fields are absent. `default=true` clears those command overrides **after**
applying explicit fields, so default wins over simultaneous position and rotation
in the same instruction. `removeIgnoreStartingValues` affects the original view
and entity offsets only inside this default-true branch; the flag alone does not
reset them. The packet regression includes the combined default-plus-position
case, rather than testing only mutually exclusive commands.

The native anchors are core consumer `0x66F1230`, default presence/value checks at
`0x66F1C4F`/`0x66F1C63`, and removal of the position/rotation instruction component
at `0x66F1E92`. The input preprocessor `0x5679AD0` rejects only an out-of-range Set
preset at `0x5679BF3`/`0x5679C7D`, preserving other instruction fields.
It also clamps Set pitch to −90…90 degrees and each view/entity offset component
to −64…64; these bounds are applied by Perlin's camera controller.

## Fade, spline, and action contracts

The native preprocessor forwards every present Fade at `0x567B3BE`, including an
empty one. The consumer path `0x8FF440` to `0x66EE7D0` supplies default times and
black color. Fade times are constrained to 0–10 seconds. The animation builder
`0x6778300` enforces at least half a second of total animation by increasing the
wait portion. Overlapping fades retain the existing color while animation keys
remain; normalized received RGB is not divided by 255.

Spline storage and playback are separate. The matched PDB identifies
`CameraInstructionOptions::SplineInstruction::mLoadFromJson` as a **bool** at
offset 37; core `0x66F2ABF` branches on its true value. Named callback `0x66F05C0`
searches definitions using exact identifier length and byte comparison. Failure
logs and returns before replacing the path. It reads duration from the registered
definition at `0x66F0E7E` and stores it at `0x66F0E89`; inline callback `0x66EF950`
reads instruction duration at `0x66F0225`. Thus an identifier is not itself a
request for named playback, and inline fields do not override a named definition.

Named curve types are converted to lowercase with `Util::toLower` before checking
`catmullrom` and `linear`. The corresponding inline enum is CatmullRom 0 and
Linear 1; other enum values are rejected by its consumer. Perlin rejects explicit
unknown/empty strings and preserves its existing CatmullRom fallback for an
omitted type. The omitted inline wire-field initialization has not been recovered
independently. Named definition construction supplies `catmullrom` explicitly at
`0x567DC08`–`0x567DC2B`.

Native keyframe fields are named `mTimeSeconds` and copied unchanged into the
animation layers. Callback `0x66FD0C0` advances `mCurrentTime` by delta time and
uses the **starting** keyframe's easing for a segment (`0x66FD64F` and
`0x66FD86C`). It directly interpolates all rotation components. When elapsed time
exceeds duration without looping, it removes animation data/storage; the exact
subsequent pose selection is not yet fully recovered. That remaining transition
must not be described as proven endpoint behavior.

The matched CameraShake handler `0x131C3A0` handles only action 0 (Add) and 1 (Stop).
Other actions return at `0x131C46E`–`0x131C470`. Stop removes the entire shake
component at `0x131C5C7`–`0x131C656` without consulting the packet's type. The
controls handler `0x1319120` requires a local player and requests pitch reset for
schemes 2 and 4; see the separate control/input regressions for movement behavior.

## Rendering, controls and movement output

Camera orientation is carried separately from player orientation into the movement packet. The view/movement-vector transforms for the recovered schemes are applied before reporting input, and native strafe schemes request the one-shot pitch reset. Camera view roll reaches the rendered view; the up-vector construction stays stable at vertical pitch.

Player visual effects are controlled by the active preset. An explicit player-effects setting can enable overlays while using a free camera. Camera and player audio-listener selection follows the preset, and environmental sound sampling uses the resulting camera state. Preset selection, reset and interrupted transitions are tested through those consumers rather than only by checking stored packet fields.

The controller uses the recovered spring easing and native sine-table calculation, with strict floating-point compilation for the camera tests. Dedicated deterministic clock hooks exercise mid-transition values, interruption continuity, persistent command state, fade overlap and FOV cleanup without relying on wall-clock sleeps. The final executable test totals are recorded in [VALIDATION.md](VALIDATION.md).

## Validation and scope

`camera_wire_selftest` uses literal byte bodies, independently of encode/decode
round trips, for partial preset axes, Set options, nested fade optionals, and FOV
strings. Additional literal fixtures cover the legacy flat fade representation
and every schema FOV easing enum in profiles 944, 975, 1001, and 2168. The original
bridge fails 280 of 493 assertions, including lost nondefault durations and
nonlinear easing values. Compiling the four corrected bridge translation units
and linking them with the existing generated library yields **493 checks, zero
failures**. The new `camera_effects_packets`
application test injects actual encoded incoming packets through `BedrockSession`
for profiles 944, 2193, and 2225 and reads the resulting camera consumers. It covers
pose inheritance, persistent/default state, malformed-packet atomicity, target and
attachment behavior, spline selection, effect start/stop, repeated control-reset
events, session reset, and absence of invented packet replies. Final build/CTest
results belong in the bundle's validation report; syntax checks alone are not an
application run. A Windows executable has not been run in this environment.

Remaining limits are material:

- Aim-assist packets 316, 320, and 339 do not yet implement native target selection,
  priorities, exclusions, or aiming correction. Preserving preset aim-assist
  fields is not completion of those packets.
- The old 944/975/1001/2168 camera bridges now preserve the existing schema's flat
  fade times and all FOV easing labels. Where the old representation cannot
  express absent fade timing, conversion materializes the established default
  `(1, 0.5, 1)`. Unknown legacy FOV enums and outgoing unrepresentable easing
  strings fail conversion instead of silently selecting Linear. This rejection
  is **Perlin's conversion policy**, not a proven native validator policy for the
  older profiles. These checks do not independently establish each older
  schema's native wire encoding. Current 2193/2225 generated output is unchanged.
- Full native spline curve construction, endpoint transition, degenerate input
  handling, and every easing/angle boundary require additional evidence. Tested
  path rendering is narrower than complete mathematical equivalence.
- The native preprocessor queues Set, Target, RemoveTarget, Clear, Fade, FOV,
  Spline, Attach, and Detach into separate typed queues. This enqueue order alone
  does not establish final ordering for a packet that combines conflicting
  operations. That case remains unproven until the queue-consumer schedule is
  closed.
- Third-person collision avoidance, actor-specific camera locators, native shake
  noise, VR/editor modes, and all visual player-effect interactions have not been
  exhaustively matched. Tests cover concrete consumers and do not certify every
  visual frame or multiplayer behavior.

Relevant metadata is under `evidence/v26_51/continued/`, including
`camera_fade_findings.json`, `camera_fov_findings.json`, and
`camera_spline_findings.json`, with a reproducible query and
`camera_spline_reference.json`. Microsoft's [camera command reference][camera-command]
describes the public command surface; it does not replace version-matched wire
or consumer evidence.

[camera-intro]: https://learn.microsoft.com/en-us/minecraft/creator/documents/CameraSystem/CameraCommandIntroduction?view=minecraft-bedrock-stable
[controls]: https://learn.microsoft.com/en-us/minecraft/creator/documents/controlschemes?view=minecraft-bedrock-stable
[camera-command]: https://learn.microsoft.com/en-us/minecraft/creator/reference/content/commandsreference/examples/commands/camera?view=minecraft-bedrock-stable
