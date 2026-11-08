# Perlin

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

Plugins declare the API revision they were built for and keep working on later games: see `docs/versioning.md` in perlin-api. The old plugin binaries in `tests/data/plugins` and `plugins/prebuilt` are loaded by `mod_selftest` on every build and must not be rebuilt when the API changes.

The first-party plugins (waila, minimap, barrel roll) and the plugin examples are in `plugins/`; see `plugins/README.md`.
