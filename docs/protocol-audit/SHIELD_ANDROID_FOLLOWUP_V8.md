# Perlin v8: Java shield and Android follow-up

This update is based on the standalone Perlin v7 archive. Conduit is a separate repository and is **not** bundled into Perlin. The matching standalone Conduit v8 source is distributed separately.

## Source changes

- Shield first-person rendering no longer selects Bedrock attachable geometry. It uses the Java 26.3 client model's 12x22x1 plate, 2x6x6 handle, and the distinct right-hand, left-hand and blocking display transforms from `assets/minecraft/models/item/shield.json` and `shield_blocking.json`. In particular, the JSON left-hand translation is not mirrored a second time.
- Third-person shield rendering now sends that Java-shaped model through the existing held-item layer instead of evaluating the Bedrock attachable graph. Java model transforms are applied for idle and blocking states.
- Shield GUI preview samples the 14..26 by 1..23 UV region for the visible plate face, matching the JavaBox model's texture unfolding instead of sampling the wrong atlas rectangle.
- HUD paperdoll remains inside the HUD presentation path, but its feet anchor moves lower relative to the `hud_player_renderer` control so its head does not sit on the top screen boundary.
- Android's Unlimited/disabled-VSync configuration no longer substitutes a hidden 60 FPS cap when SDL reports an unknown display refresh rate. Any user-configured finite cap still applies.
- The Android activity requests the highest same-resolution display mode on launch and resume and logs the preference. This is a request, not an override of OEM refresh limits.
- Android device-code sign-in now opens the verification URL in the system browser first, retaining the embedded WebView only as a fallback when no browser can open. The native OAuth polling logic is unchanged. The code is copied to the clipboard by the SDL bridge.

## Related Conduit v8 change (separate download)

The OpenGL ES presenter attempts swap interval 0 for requested non-FIFO modes on the actual window surface, even when the startup pbuffer capability probe was inconclusive. An EGL implementation that rejects interval 0 falls back to interval 1. Compositor-enforced 60 Hz pacing remains outside application control.

## Validation limits

- Standalone C++ tests: shield icon coverage and UV region, creative catalog ordering, creative scrollbar math, and built-in camera name checks passed.
- A full Perlin build and Android APK build were not completed in this environment: the required external repository dependencies cannot be fetched because GitHub DNS resolution is unavailable.
- Conduit CMake configuration was also blocked while fetching SPIRV-Cross.
- No Android phone/device, graphics driver, Microsoft account login, or frame-time benchmark was available. FPS and login improvements are therefore not measured or confirmed.
- The original Minecraft Java shield JSON/model were inspected from the user-provided 26.3 client JAR. The JAR and Minecraft textures are **not** redistributed.

## Work that is not verified or completed

- Exact vanilla Bedrock creative category contents and ordering: the client still renders the catalog supplied by the server, and the embedded Perlite server's creative-content data has not been proven identical to Mojang's. Collapsible headers can intentionally hide items until expanded.
- `/camera` commands in embedded singleplayer: the recorded Perlite session had an empty preset catalog and no subsequent CameraInstruction for a typed `/camera` command. The preset fallback does not make the server implement missing commands. Full multiplayer camera effect equivalence is also unverified.
- Per-device Android EGL/native surface limits and full GLES performance tuning.
- Shield rendering has not had a pixel-by-pixel visual comparison in Windows, Android, or the Java reference client. Applying Java model metadata does not itself guarantee identical rendering in a different engine.

## Import order

Push standalone Conduit v8 into `theperlinproject/conduit` first. Push Perlin v8 into `theperlinproject/perlin` independently. Do not copy the Conduit folder into Perlin. Then compile and test through their normal CI workflows.
