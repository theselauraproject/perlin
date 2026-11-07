# Local client control

Build Perlin, then start a background client with a loopback-only HTTP endpoint:

```sh
build/Perlin --hidden --control-port 19180 --offline --name PhysicsLab \
  --join 127.0.0.1:19142 --raknet
```

`--hidden` does not show the SDL window or capture the desktop mouse. The normal
client renderer, network session, input handling and player physics still run.
The interface is disabled unless `--control-port` is supplied. Browser origins
other than this localhost endpoint are rejected; mutation requests require JSON.

Each start generates a random token, printed on startup:

```
[control] http://127.0.0.1:19180/?token=<token> (send X-Perlin-Token: <token> with API requests)
```

Every route requires it in the `X-Perlin-Token` header and returns `401`
otherwise. The page (`/`) also accepts it as a `token` query parameter so the
printed URL can be opened directly; the page then sends the header itself.

`SetControlActions` only takes over action state in `--hidden` mode. A visible
window started with `--control-port` keeps its physical keyboard and mouse
until the first `input` request arrives.

Set `PERLIN_DATA_DIR` to a separate data directory to isolate test settings,
accounts, logs and replay files from your usual client profile. Vanilla assets
must be available there (asset directories can be symlinked).

Open the printed `http://127.0.0.1:19180/?token=…` URL for the live rendered view, keyboard movement,
mouse look, state telemetry and server command entry. The page controls the
native hidden client; its connection to BDS still uses RakNet. Keyboard holds
expire after six simulation ticks unless refreshed, and losing tab focus releases
input. HTTP requests remain available for precise automated tests.

The state endpoint includes `movementSpeed`, `serverSprinting`, and
`movementAttributeJson` (the complete received movement attribute, including
modifiers) for diagnosing speed transitions. These describe received server
state, which can lag the locally predicted input.
`effectIds` lists active effects from the received effect cache. Finite durations
age by movement ticks from the packet stamp (or receipt tick for unstamped
packets); only duration -1 is infinite. Full native effect scheduling outside
ordinary player movement remains unverified.
`sleeping`, `dimension` and `predictionHistorySize` expose sleep state, the
received dimension ID and the actual retained movement-record count for replay
lifecycle checks. History size is not a physics-accuracy score.

## Requests

```sh
TOKEN=<token printed on startup>

# Connection, pose, velocity, simulation tick, last applied command, corrections.
curl -H "X-Perlin-Token: $TOKEN" http://127.0.0.1:19180/state

# Hold forward and sprint for exactly 40 local simulation ticks (two seconds).
curl -H "X-Perlin-Token: $TOKEN" -H 'Content-Type: application/json' http://127.0.0.1:19180/command \
  -d '{"op":"input","actions":["forward","sprint"],"ticks":40}'

# Look uses the same camera angles as the native client (degrees).
curl -H "X-Perlin-Token: $TOKEN" -H 'Content-Type: application/json' http://127.0.0.1:19180/command \
  -d '{"op":"look","yaw":90,"pitch":-20}'

# Stop all held actions. Omitting ticks holds input until replaced/released.
curl -H "X-Perlin-Token: $TOKEN" -H 'Content-Type: application/json' http://127.0.0.1:19180/command \
  -d '{"op":"release"}'

# Send a normal server command; server permissions remain authoritative.
curl -H "X-Perlin-Token: $TOKEN" -H 'Content-Type: application/json' http://127.0.0.1:19180/command \
  -d '{"op":"command","text":"/time set day"}'

# Request a rendered frame. The first request returns 202 while capture runs;
# repeat to receive JPEG bytes. Each request schedules an updated frame; JPEG
# encoding runs off the render thread and keeps only the newest pending frame.
curl -H "X-Perlin-Token: $TOKEN" http://127.0.0.1:19180/frame -o frame.jpg

# The most recent 24,000 sent simulation ticks as newline-delimited JSON.
curl -H "X-Perlin-Token: $TOKEN" http://127.0.0.1:19180/trace -o movement.ndjson
```

`input` accepts the action IDs from `KeyMapping.cpp`, with or without `key.`.
This includes movement, jump, sneak, sprint, use, attack, inventory and hotbar
actions. Requests also support `chat` (`text`), `key` (`key`, `down`), `mouse`
(`x`, `y`, `button`, `down`) and `shutdown`.

For menus and native text fields, `pointer` (`x`, `y`) moves the cursor without
changing button state, `scroll` (`y`) sends a wheel delta, and `character` (`key`)
sends a Unicode scalar value to the normal text input handler. Raw `key` requests
use Perlin's native key codes; these are distinct from Unicode character values.
`state.cursorCaptured` and `state.textInput` expose the current input mode.
Fluid/contact flags and `health`, `hunger`, `air`, `vitalsKnown` and `dead` allow
scenario runners to detect drowning or death rather than treating subsequent
command failures as movement results.

Commands return `202` and a monotonically increasing `id`. They are applied on
the game thread; wait until `/state.applied` reaches that ID. `commandError`
reports the last failed `command` or `chat` send; other requests leave it unchanged. Bad requests return `400`, wrong content types
`415`, a missing or wrong token `401`, and a full pending queue `429`. Raw key and mouse requests are useful for
menus; gameplay movement should use `input` so durations follow simulation ticks.

For server commands, `commandReplies`, `lastCommandSuccess` and
`lastCommandOutput` report actual `CommandOutputPacket` replies separately from
the local send acknowledgement. A locally accepted request does not prove the
server executed it. The player's server-visible name can acquire a suffix when
another player already uses the requested name; check command replies rather
than assuming a name uniquely identifies the controlled actor.

`{"op":"probe","x":0,"y":100,"z":0}` selects a block to inspect. State then
includes `probeLoaded`, `probeName` and local collision boxes in `probeCollision`.
Check `probeLoaded` before interpreting the shape: unloaded terrain is blocked
by the normal collision adapter.

## Physics validation

`corrections` counts actual BDS player prediction correction packets; it is not
a synthetic pass counter. Capture a baseline after spawning or teleporting and
compare it after each movement scenario and a settling interval. A result is
valid only if the client stayed connected and initialized and actually sent the
requested movement. The trace records the resulting positions, velocities,
inputs and collision flags for each sent tick. It also includes the scaffolding,
ascendable/descendable and fall-through input flags and local fall distance.
These are the inputs used for that tick; `/state` actor flags are received
metadata and can lag locally predicted contacts.

Zero corrections on a test route does not establish complete native BDS parity.
Keep the server's strict movement checks enabled and record which terrain,
attributes, movement modes and multi-tick transitions were exercised. Do not
hide corrections, disable authoritative movement or increase tolerances to make
a test pass.

The HTTP/input self-test covers action expiry and repeat presses, server input
locks, blocked input, token checks, command validation and origin checks, returned state,
trace delivery, and JPEG colour conversion. Live checks also exercise GPU frame
readback from the hidden Metal renderer and shutdown with a pending capture.

For block-update timing investigations, block packet diagnostics include
`afterAuthTick`, the last player input tick sent before that packet is decoded.
This is a client application timestamp, not the packet's server mutation tick or
socket arrival time. With `PERLIN_BLOCK_DEBUG=1`, the session also logs when its
per-frame receive time budget is reached. A correction decoded before a block
update and no budget-limit log are evidence of late world-state information;
they do not justify changing movement thresholds or suppressing correction counts.
