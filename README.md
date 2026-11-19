# Perlin

A cross-platform Minecraft: Bedrock Edition client written in C++23. It speaks the Bedrock protocol (generated from `protocol/`), renders through [conduit](https://github.com/theperlinproject/conduit) and SDL3, and runs WebAssembly plugins through [perlin-api](https://github.com/theperlinproject/perlin-api).

## Layout

| Path | Contents |
|------|----------|
| `src/client` | the game client: screens, input, player, entities, UI, resource packs, plugin manager |
| `src/network` | transports (RakNet, NetherNet), authentication, packet handling |
| `src/world` | level, chunk and block data |
| `src/render`, `src/shaders` | chunk meshing and shader sources |
| `src/util` | shared helpers: strings, paths, file I/O, logging, JSON |
| `server/` | dedicated server and its plugin host, see `server/README.md` |
| `plugins/` | first-party plugins and plugin examples |
| `protocol/`, `tools/protogen` | protocol specs and the codec generator run at build time |
| `tests/` | self-tests, built with `-DPERLIN_BUILD_TESTS=ON` |
| `docs/` | user and platform references; `platforms.md` lists supported platforms, `plugin-host.md` describes the plugin host |

Platforms and what each one ships are in `docs/platforms.md`. To contribute, see `CONTRIBUTING.md`.

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

## Rendering

Perlin renders through [conduit](https://github.com/theperlinproject/conduit). CMake fetches it automatically.

| Cache variable       | Default | Effect                                                   |
|----------------------|---------|----------------------------------------------------------|
| `PERLIN_CONDUIT_DIR` | empty   | build a local conduit checkout instead of fetching one   |
| `PERLIN_CONDUIT_TAG` | `main`  | the conduit revision to fetch                            |

```sh
cmake --preset <preset> -DPERLIN_CONDUIT_DIR=../conduit
```

## Mod API

The plugin SDK, the schema it is generated from and the host core that runs plugins live in [perlin-api](https://github.com/theperlinproject/perlin-api). CMake fetches its `main` branch automatically, so Perlin always builds against the current API.

| Cache variable          | Default | Effect                                                    |
|-------------------------|---------|-----------------------------------------------------------|
| `PERLIN_API_SOURCE_DIR` | empty   | build a local perlin-api checkout instead of fetching one |
| `PERLIN_API_TAG`        | `main`  | the perlin-api revision to fetch (a branch, tag or commit) |

```sh
cmake --preset <preset> -DPERLIN_API_SOURCE_DIR=../perlin-api
```

Plugins declare the API revision they were built for and keep working on later games: see `docs/versioning.md` in perlin-api. The prebuilt plugin binaries in `tests/data/plugins` and `plugins/prebuilt` are loaded by `mod_selftest` (part of `PERLIN_BUILD_TESTS`).

The first-party plugins (waila, minimap, barrel roll) and the plugin examples are in `plugins/`; see `plugins/README.md`. How Perlin hosts plugins (lifecycle, states, permissions, engines) is in `docs/plugin-host.md`.
