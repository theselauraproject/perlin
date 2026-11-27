# Source follow-up v6

This is a targeted follow-up to the v5 visual/camera source. It is not a claim of complete Bedrock parity.

## Changes

* The creative category projector now builds one packed index buffer instead of a separately growing vector for each group every frame. Its output remains in **server group order**, with the original item order preserved *inside* each group. Invalid group IDs remain excluded. This addresses allocation churn when browsing large creative catalogs; it does **not** replace an incorrectly ordered catalog supplied by Perlite or another server.
* A new standalone regression checks interleaved item entries, separately ordered groups, category filtering, expansion, and invalid group IDs. The existing creative-catalog, camera-preset, and shield-icon regressions pass.
* `PERLIN_TRACE_CAMERA=1` enables packet-side diagnostics for incoming preset catalogs, set/clear/fade/FOV/target/spline instructions, and invalid runtime preset indices. These diagnostics help isolate whether a nonworking `/camera` originates in server command handling, wire IDs, or client effects. They do not fabricate packet replies or apply camera effects without a server instruction.

## Known constraints of this follow-up

* The Perlite singleplayer log supplied earlier includes an empty `CameraPresets` packet but no captured `/camera` command execution and its resulting instruction. The client cannot infer a command effect that the server never sent. Perlite is a separate source dependency; editing its `/camera` command is not possible using only this client archive.
* Shield and stair icon rendering, Java shader GPU performance, and actual Windows DX12 visuals still require live visual and performance comparison. No new visual equivalence or FPS figure is asserted here.
* The complete application remains unbuilt in this environment because CMake cannot fetch the required `perlin-api` source. The standalone C++ regressions compile and pass.

## Tests run

```
c++ -std=c++23 -O2 -Isrc tests/shield_item_icon_selftest.cpp -o /tmp/shield && /tmp/shield
c++ -std=c++23 -O2 -Isrc tests/builtin_camera_presets_selftest.cpp -o /tmp/presets && /tmp/presets
c++ -std=c++23 -O2 -Isrc tests/creative_catalog_selftest.cpp -o /tmp/catalog && /tmp/catalog
c++ -std=c++23 -O2 -Isrc tests/creative_catalog_order_smoke.cpp -o /tmp/order && /tmp/order
```
