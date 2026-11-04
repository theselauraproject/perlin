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
