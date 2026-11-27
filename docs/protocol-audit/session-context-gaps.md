# Session codec context: coverage and remaining limits

The packet codec now binds protocol and shield item identity to the session
performing each operation. This addresses codec selection leaking between
connections or replay sessions. It does not establish complete concurrent-world
or transfer equivalence.

## Verified source coverage

`net::context::ScopedSettings` in `src/network/Stream.hpp` installs a thread-local
override and restores the previous override when the scope exits. The existing
process-wide atomic defaults and their setters remain available to standalone
codec callers. Changing a default while a scope is active does not change that
operation's selected protocol or shield identity.

The following production entry points bind settings explicitly:

| Entry point | Covered work |
| --- | --- |
| `BedrockSession::Send` | Checked encoding, outgoing packet logging, record/sent taps, transmission |
| `BedrockSession::SendUnreliable` | The same work for unreliable transmission |
| `BedrockSession::SendMoveInput` | Its direct PlayerAuthInput encoding and transmission |
| `BedrockSession::SendRaw` | Logging and callbacks for the supplied bytes; callers still supply correctly encoded bytes |
| `BedrockSession::OnGamePacket` | Incoming taps, packet logging, ordinary packet decoding and handlers |
| `BedrockSession::ChunkWorkerMain` | Deferred packet decoding and handlers using settings captured when each packet was enqueued |
| `BedrockSession::InjectClientPacket` | Decoding outgoing packets recorded in a replay |

Private `Transmit` inherits its caller's binding. It does not reselect a profile
after the body has already been encoded. Nested entry into a different session
temporarily binds that session and restores the caller's context on return.

The session protocol and shield runtime ID are stored atomically. ItemRegistry
updates the session's shield ID and the active registry callback's binding. A
replacement registry without a shield sets the session ID to `-1`. Replay setup,
connection setup, and the player reset used during transfer also clear it.

Regression coverage is in `tests/wire_selftest.cpp` and
`src/client/selftest/SessionCodecContextSelfTest.cpp`. It includes differing wire
profiles, reliable/unreliable/raw sending, interleaved incoming packets, nested
callbacks, simultaneous active contexts, exception restoration, independent
shield registries, replay-client decoding, and deferred packet snapshots. A
profile change during encoding also checks that outgoing callbacks retain the
profile used to produce those bytes.

## Remaining lifecycle limits

### A detached chunk batch can outlive a world reset

`ChunkWorkerMain` swaps the shared `chunkIn` queue into a local batch and processes
that batch after releasing `chunkMutex`. `ResetWorldForDimension` clears only the
shared queue before clearing world state. It does not cancel or join an already
detached batch. `ResetForTransfer` calls this reset without first waiting for the
chunk worker to finish.

Capturing codec settings ensures that those old bytes retain their original wire
interpretation. It does not prevent an old handler from applying world changes
after the reset. A complete lifecycle fix needs a world/connection generation
check at application time or a carefully ordered worker quiescence barrier.
`WaitChunkWorkIdle` currently has a bounded polling loop and is not used by that
reset path. The context regression changes a replay profile to test deferred
decoding; it does not prove cancellation of stale world updates.

### Context binding does not serialize a connection transition

The selected codec remains stable for one send, including its logging and taps.
The change does not make encoding, connection replacement, and transport sending
one atomic operation. `Send` encodes before `Transmit` acquires `txMutex`; the
transport layer does not validate a captured connection generation. Concurrently
resetting the same session still requires lifecycle coordination to ensure an old
operation cannot reach a replacement transport. Separate atomic protocol and
shield fields also do not constitute an atomic registry/connection transition.

### Other shared state and standalone codecs

Block palettes, dimension metadata, and other world/resource registries retain
their existing ownership models. For example, StartGame selects a protocol in
the process-wide `BlockPalette`, and DimensionData updates `ChunkCodec` dimension
metadata. Session codec scopes do not isolate those registries.

Standalone calls to `net::Encode`, `Read`, or packet JSON conversion still use the
process defaults unless their caller supplies `ScopedSettings`. Those defaults
can change when another connection or replay is initialized. They are retained
for API compatibility and are not a substitute for an explicit operation scope.
