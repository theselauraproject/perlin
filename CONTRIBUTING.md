# Contributing

## Building

See the [README](README.md) for dependencies and CMake options, and `docs/platforms.md` for what each platform ships.

Tests are off by default:

```sh
cmake --preset <preset> -DPERLIN_BUILD_TESTS=ON
cmake --build build/<preset>
ctest --test-dir build/<preset>
```

## Code

- C++23. `.clang-format` is the formatter.
- Shared helpers have one home: string handling is `src/util/Strings.hpp`, paths `src/util/Paths.hpp`, file I/O `src/util/FileIo.hpp`. Before writing a local `Trim`, `Lower` or `StartsWith`, check those and the standard library (`std::string_view::starts_with`, `ends_with` and `contains` cover the prefix and substring cases).
- Comments only where the code cannot say it: non-obvious algorithms, platform requirements, thread-safety and lifetime rules, protocol behavior.
- A change that fixes a bug or adds behaviour comes with a test in `tests/`; add the target to `CMakeLists.txt` next to the others.

## Plugins and the API

Plugin-facing behaviour is defined in [perlin-api](https://github.com/theperlinproject/perlin-api), not here. Change the API there (see its `docs/contributing.md`); change how Perlin implements it in `src/client/systems/mods/` and `src/client/features/mods/`. Perlin-specific host behaviour is documented in `docs/plugin-host.md`.
