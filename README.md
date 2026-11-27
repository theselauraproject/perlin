# Perlin

A cross-platform Minecraft: Bedrock Edition client written in C++23. It speaks the Bedrock protocol (generated from `protocol/`), renders through [conduit](https://github.com/theperlinproject/conduit) and SDL3, and runs WebAssembly plugins through [perlin-api](https://github.com/theperlinproject/perlin-api).

## Compatibility and performance source bundle

This tree includes the Bedrock movement, item, camera, inventory and packet corrections documented in [the compatibility notes](docs/protocol-audit/BEHAVIOR_FIXES.md), together with the [chunk and rendering optimizations](docs/performance/README.md). Conduit is maintained and distributed separately in its own repository. [Validation and Windows build instructions](docs/protocol-audit/VALIDATION.md) distinguish completed tests from platform checks.

## Layout

| Path | Contents |
|------|----------|
| `src/client` | the game client: screens, input, player, entities, UI, resource packs, plugin manager |
| `src/network` | transports (RakNet, NetherNet), authentication, packet handling |
| `src/world` | level, chunk and block data |
| `src/render`, `src/shaders` | chunk meshing and shader sources |
| `src/util` | shared helpers: strings, paths, file I/O, logging, JSON |
| `protocol/`, `tools/protogen` | protocol specs and the codec generator run at build time |
| `tests/` | self-tests, built with `-DPERLIN_BUILD_TESTS=ON` |
| `docs/` | user and platform references; `platforms.md` lists supported platforms, `plugin-host.md` describes the plugin host |

Platforms and what each one ships are in `docs/platforms.md`. To contribute, see `CONTRIBUTING.md`.

## Singleplayer and the server

Singleplayer worlds run on [Perlite](https://github.com/theperlinproject/perlite), the Bedrock server software that lives in its own repository. The build fetches it and links its `perlite_embed` library into the client, so singleplayer needs no extra file next to the game. Set `PERLITE_SOURCE_DIR` to a local checkout to build against it instead, `PERLITE_TAG` to pin a revision, or `-DPERLIN_SINGLEPLAYER=OFF` to leave singleplayer out. Perlin no longer ships a dedicated server executable; run Perlite for that.

## Building

| Preset | Use it for |
|--------|------------|
| `windows-clang-cl-fast` | everyday Windows builds: Ninja, every core busy, no link-time optimisation |
| `windows-clang-cl` | Windows release builds with LTO and the extra optimisation flags (much slower to link) |
| `windows-vs-clangcl`, `windows-vs2026-clangcl` | a Visual Studio 2022 or 2026 solution |
| `unix-ninja-fast` | everyday Linux and macOS builds |
| `unix-ninja` | Linux and macOS release builds with LTO |

```sh
cmake --preset windows-clang-cl-fast
cmake --build build/windows-clang-cl-fast
```

Run the Windows presets from a Developer PowerShell (or let Visual Studio / VS Code set up the environment). Use the `clang-cl` that ships with Visual Studio ("C++ Clang tools for Windows" in the installer): a standalone LLVM that is older than your Visual Studio's C++ library fails during configure with "Cannot compile simple program using std::filesystem".

## Android storage

Perlin keeps all of its files in **Internal storage › games › Perlin**. On first launch it asks for "All files access", which puts worlds, texture packs, shader packs, screenshots, logs and settings where any file manager or a PC over USB can reach them, and moves anything already in the private folder there. Perlin always uses this folder and asks again on launch until the access is granted.

## Rendering / separate Conduit dependency

Perlin renders through the independent [Conduit repository](https://github.com/theperlinproject/conduit). **No Conduit source is bundled in Perlin.** Push the companion Conduit archive to that repository first, then build this Perlin archive. By default CMake fetches Conduit from `main`. For reproducible builds, pin `PERLIN_CONDUIT_TAG` to the pushed Conduit commit.

| Cache variable | Default | Effect |
| --- | --- | --- |
| `PERLIN_CONDUIT_DIR` | empty | Build from a separate local Conduit checkout |
| `PERLIN_CONDUIT_TAG` | `main` | Revision fetched from the Conduit GitHub repository |
| `CONDUIT_BUILD_TESTS` | `OFF` | Also build Conduit's tests |

Local sibling checkout example:

```sh
cmake --preset windows-clang-cl-fast -DPERLIN_CONDUIT_DIR=../conduit
```

This option does **not** copy or vendor Conduit into Perlin. If your build directory previously cached `PERLIN_USE_BUNDLED_CONDUIT`, it is ignored; use a fresh build directory when switching sources.

## Mod API

The plugin SDK, the schema it is generated from and the host core that runs plugins live in [perlin-api](https://github.com/theperlinproject/perlin-api). CMake fetches its `main` branch automatically, so Perlin always builds against the current API.

| Cache variable          | Default | Effect                                                    |
|-------------------------|---------|-----------------------------------------------------------|
| `PERLIN_API_SOURCE_DIR` | empty   | build a local perlin-api checkout instead of fetching one |
| `PERLIN_API_TAG`        | `main`  | the perlin-api revision to fetch (a branch, tag or commit) |

```sh
cmake --preset <preset> -DPERLIN_API_SOURCE_DIR=../perlin-api
```

Plugins declare the API revision they were built for and keep working on later games: see `docs/versioning.md` in perlin-api. The prebuilt plugin binaries in `tests/data/plugins` are loaded by `mod_selftest` (part of `PERLIN_BUILD_TESTS`).

How Perlin hosts plugins (lifecycle, states, permissions, engines) is in `docs/plugin-host.md`.
