# Validation record

Base source: `0329b0cc4b1c21482b46cf275bd8a5eafc8aaeab`.

## Final continued bundle

The final modified **Linux Perlin application compiled and linked successfully**. All **30 selected CTests passed, with zero failures and zero skips**, in 1.90 seconds on this host. A separate `handler_dispatch_selftest` run with `PERLIN_TRACE=1` and `PERLIN_OFFHAND_EXPERIMENT=1` also passed, in 0.02 seconds.

The final source includes the full-width sound variant guard and the corrected input/movement fixtures. The retained output is [test-results-v2.txt](test-results-v2.txt). The earlier [test-results.txt](test-results.txt) is the historical first-bundle 13-test record, not the current test selection.

| Test | Result | Scope |
| --- | --- | --- |
| `actor_lerp_selftest` | Passed | 63 checks, including head-only rotations preserving active and queued position interpolation |
| `audio_selftest` | Passed | Real PCM mixing: pause, seek, pitch, repeat counts, range, distinct voices and finite extreme fades |
| `hud_feed_selftest` | Passed | Locator projection, nonfinite inputs and huge finite distances |
| `handler_dispatch_selftest` | Passed | Real generated dispatch, validation, inventory responses/rollback, offhand transactions and cooldowns |
| `armor_packets_selftest` | Passed | Live armor packet dispatch, raw stack preservation, request mode gating and invalid slot isolation |
| `recipe_packets_selftest` | Passed | Live recipe knowledge, grouped visibility, rule bypasses and stale-selection rejection |
| `server_ui_packets_selftest` | Passed | GUI notices, independent inventory/furnace options, locations, real 2193 camera state and no invented replies |
| `audio_packets_selftest` | Passed | Both 2193 and 2225 packet decoding through actual voice controls and mixed PCM; session cleanup |
| `input_reporting_selftest` | Passed | 41 checks for device/mode reporting, raw/effective/analog vectors and full/quarter/zero physical acceleration |
| `movement_protocol_selftest` | Passed | Encoding failures, invalid input, transport retry, pending-state snapshots, reset/initialization, coordinates and 2193 metadata/identity |
| `session_codec_context_selftest` | Passed | Interleaved/nested/concurrent profiles, shield registries, raw send logging, recorded-client replay and deferred packet snapshots |
| `audio_director_selftest` | Passed | Existing sound dispatch and listener/range behavior |
| `player_interaction_selftest` | Passed | 43 checks, including actual cooldown-gated held-item use and release behavior |
| `hud_screen_selftest` | Passed | 43 checks, including actual popup presentation, inventory preferences and unloaded-actor locator state |
| `executable_boot_resources` | Passed | Linked application starts its command-line path with required embedded resources |
| `session_policy_selftest` | Passed | Compression threshold zero and boundary choices |
| `packet_selftest` | Passed | Generated current codecs and retained 944/975/1001/2168/2193 bridge round trips |
| `wire_selftest` | Passed | Integer width/overflow/truncation, callback isolation and scoped codec restoration |
| `move_input_selftest` | Passed | Finite/bounded values, vehicle pairs, serialized eye height and input enums |
| `inventory_slots_selftest` | Passed | Furnace variants, horse inventory/equipment and slot bounds |
| `item_cooldown_selftest` | Passed | Category normalization, replacement, countdown and definition parsing |
| `gui_data_selftest` | Passed | Popup replacement, single consumption, float age boundary, flash state and reset |
| `armor_damage_selftest` | Passed | Independent armor wire fixtures and atomic Damage NBT rewriting |
| `recipe_unlocks_selftest` | Passed | Knowledge creation, recognized IDs, add/remove/clear and bypass rules |
| `bridge2193_selftest` | Passed | 19 literal-byte checks for extended metadata bits and signed unique IDs |
| `sound_packets_wire_selftest` | Passed | 207 checks: seven operations, exact versioned layout, fade order, every invalid-tag position and strict callback isolation |
| `camera_wire_selftest` | Passed | 168 checks: independent fade presence, literal FOV strings, 32 known labels and scoped case handling |
| `protocol_audit_selftest` | Passed | Six Python tests for complete enumeration, classifications, replay separation and identity/address guards |
| `physics_selftest` | Passed | Existing physics suite plus distant-coordinate numerical safety |
| `client_control_selftest` | Passed | Raw/effective input under server locks, UI restrictions and mod/plugin grabs |

The Windows workflow was reviewed against the actual CMake registrations: all **18 requested build targets** and **30 selected CTest names** exist. This is a source and workflow review; **no Windows build was executed here**.

## Independent regression evidence

- The original 2193 prediction/metadata bridge failed **14 of 19 literal-byte checks**. Corrected generated codecs pass all 19, and the final app test exercises the actual sender with distinct runtime ID 7 and unique ID 70.
- The original 2193 camera schema failed **22 of 168 checks**. Corrected codecs pass all 168, and the final app test injects literal camera packets into real session state.
- The initially reconstructed sound bridge accepted invalid uint32 tags that narrowed into real uint8 operations. The expanded sound test reproduced **126 failures in 207 checks** before the guard. The final generated bridge passes all 207, including invalid tags in every one of the seven records and strict-callback isolation.
- The audio packet test exercises both 2193 and 2225 through file decoding, queued controls and deterministic PCM mixing. This verifies Perlin's implementation path; it is not a comparison with a running FMOD/Minecraft client.

Two initial app-test failures were corrected without changing production behavior. The input fixture had not marked its world column settled, so simulation correctly did not run. It now asserts readiness, and the quarter/full acceleration assertion is unchanged. The movement fixture incorrectly expected a missing transaction to fail serialization under 2225. The codec's required-payload failure is now exercised under 944, while invalid-input and retry-state checks run under 944, 2193 and 2225. A position-bearing AbortBreak action preserves the queue-order and coordinate checks across those formats.

## Source and evidence checks

- `git diff --check` and `tools/check_portability.py` passed.
- Both exhaustive packet inventories match the final schema SHA-256 digests.
- All **51 entries** in the retained evidence checksum manifest verify. The continued reference report contains **21 bounded packet contracts**, alongside the complete catalog/dispatch maps.
- The matched executable/PDB/catalog guards reject incorrect identities. No original executable was run.
- Protocol generation produces **254 current definitions** and **246 protocol-2193 definitions**, with no opaque fallbacks in those two profiles. These include 15 retained legacy definitions. Final generation requires 21 packet bridge pairs for 2193; same-schema packets do not require a bridge.

Generation, round trips, independent byte fixtures, state assertions, and native equivalence are separate results. No test count is a percentage of complete Bedrock support.

## Local build configuration

GNU C++ 14.2.0, CMake 4.4.5, Ninja 1.13.2, Linux x86-64. The application uses the existing `unix-ninja-fast` preset with `PERLIN_BUILD_TESTS=ON`, `PERLIN_SINGLEPLAYER=OFF`, and `SDL_UNIX_CONSOLE_BUILD=ON`. The console-only SDL setting accommodates this host's missing X11/Wayland development packages. Windows defaults are unchanged. Dependency updates were disabled in the already-populated local FetchContent cache during the final configure.

```sh
cmake --preset unix-ninja-fast -B build/compatibility \
  -DPERLIN_BUILD_TESTS=ON -DPERLIN_SINGLEPLAYER=OFF -DSDL_UNIX_CONSOLE_BUILD=ON
cmake --build build/compatibility --parallel 4 --target \
  Perlin packet_selftest wire_selftest move_input_selftest inventory_slots_selftest \
  item_cooldown_selftest session_policy_selftest client_control_selftest actor_lerp_selftest \
  physics_selftest gui_data_selftest armor_damage_selftest recipe_unlocks_selftest \
  bridge2193_selftest sound_packets_wire_selftest camera_wire_selftest audio_selftest hud_feed_selftest
```

The exact focused CTest selection is also in `.github/workflows/protocol-compatibility-windows.yml`. The Windows build and manual-run instructions are in [README.md](README.md).

An attempt with eight compiler jobs exhausted the host's memory during compilation; the build completed with four jobs. A historical corrupt Ninja dependency-log record also caused repeated rebuilding because of the [upstream recovery-offset bug](https://chromium.googlesource.com/external/github.com/ninja-build/ninja/+/3f910ed8ce2ae611d31ee0ea7ea560c10f084393). With the build stopped, the original cache was backed up and the single verified invalid record removed after validating the complete retained suffix. Subsequent incremental builds completed normally. This repaired local build metadata, not project source or test expectations.

## Earlier targeted checks retained

The first bundle's unchanged variable-integer/callback regression code passed with AddressSanitizer and UndefinedBehaviorSanitizer, and its added checks failed 17 assertions against the original reader/dispatcher. LeakSanitizer was disabled because of this container's tracing setup. The targeted physics suite also passed with undefined-behavior and float-cast-overflow sanitizers. These are historical targeted checks; the full continued application was **not** built with sanitizers.

## Not established

- Actual Windows compilation, graphical game execution, or testing on the user's machine.
- Differential execution against either supplied Minecraft executable.
- Acceptance by the user's custom servers or anticheats.
- Complete serialization/validation of every packet, every handler/listener effect, every gameplay state or every connection/timing schedule.
- Full behavior for older retained protocol profiles without their own matched reference evidence.

The Minecraft executables and PDB were inspected as data only. No passing local test or static catalog match establishes complete Bedrock equivalence or guarantees zero kicks.
