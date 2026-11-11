# Platforms

Every platform builds from the same CMake project and ships in the `nightly` release (see `auto-update.md`).

| Platform | Package | Graphics | Mods | Singleplayer | Updates | CI job |
|----------|---------|----------|------|--------------|---------|--------|
| Windows x64 | `Perlin-windows-x64.zip` | D3D12, D3D11, Vulkan | WasmEdge | built in | self-installs | `windows-x64` |
| Windows on ARM | `Perlin-windows-arm64.zip` | D3D12, D3D11, Vulkan | wasm3 | built in | self-installs | `windows-arm64` (cross-compiled) |
| Linux x64 / arm64 | `Perlin-linux-*.zip`, `Perlin-linux-*.AppImage` | Vulkan | WasmEdge | built in | self-installs (AppImage replaces itself) | `linux-x64`, `linux-arm64` |
| Flatpak (x64) | `Perlin.flatpak` | Vulkan | WasmEdge | built in | through Flatpak | `flatpak-x64` |
| SteamOS / Steam Deck | AppImage or Flatpak | Vulkan | WasmEdge | built in | as above | — |
| FreeBSD x64 | `Perlin-freebsd-x64.zip` | Vulkan | wasm3 | built in | self-installs | `freebsd-x64` |
| macOS 12+ (Apple silicon, Intel) | `Perlin-macos-*.zip` | Metal | WasmEdge | built in | self-installs | `macos-arm64`, `macos-x64` |
| iOS / iPadOS 15+ | `Perlin.ipa` | Metal | wasm3 | built in | AltStore / SideStore source | `ios` |
| Android 8+ phones and tablets | `Perlin.apk` | Vulkan | WasmEdge | built in | downloads, system installer | `android` |
| Android TV, Google TV, Fire TV | the same `Perlin.apk` | Vulkan | WasmEdge | built in | as Android | `android` |
| Chromebooks (Android apps) | the same `Perlin.apk` | Vulkan | WasmEdge | built in | as Android | `android` |
| Web (any WebGPU browser) | `Perlin-web.zip`, GitHub Pages | WebGPU | wasm3 | built in | reload | `web` |

## Bedrock's platforms

Bedrock runs on Windows, Android (including Fire OS and Chromebooks), iOS and iPadOS, Xbox, PlayStation and Switch.
Perlin covers all of them except the consoles, and adds macOS, Linux, FreeBSD, Windows on ARM natively and the web.

Consoles are out of reach for an unofficial client: Xbox, PlayStation and Switch software has to be built with the
platform holder's SDK under a publishing agreement. Xbox developer mode can sideload apps for testing, but SDL3 no
longer targets UWP, and the GDK route needs Microsoft's approval.

## Input by device class

- **Phones and tablets** start with touch controls. A controller hides them (Bedrock does the same); touching the
  screen brings them back. In menus a controller moves a pointer the game draws.
- **TVs** (detected with `SDL_IsTV`) start without touch controls. The remote's D-pad moves the menu pointer and the
  centre button clicks; Back is Escape. A controller works as on any other platform.
- **Chromebooks and Samsung DeX** start as a desktop: mouse and keyboard first, touch controls when the screen is
  touched.
- **The web** draws its own pointer for controllers too, since a page cannot move the system pointer.

## Notes per platform

- **Windows on ARM** is cross-compiled on the x64 runner with clang-cl. NVIDIA Streamline (DLSS, Reflex) is x64
  only and is left out. Mods run on wasm3.
- **FreeBSD** builds in a FreeBSD 14 VM. Slang publishes no FreeBSD shader compiler, so the build takes the Vulkan
  shader pack from the Linux job (`-DPERLIN_SHADER_PACK_PREBUILT=...`). Same backends, so the same file.
- **AppImage** puts the game in `usr/lib/perlin` inside the image; data stays in `~/.local/share/Perlin`. The
  updater downloads `Perlin-linux-<arch>.AppImage` beside the running one and swaps it in on exit.
- **Flatpak** data lives in `~/.var/app/io.perlin.client/data/Perlin`. The in-game updater is off there.
  `flatpak install --user Perlin.flatpak` installs the bundle. Flathub is not targeted yet: its builders have no
  network access, and this build fetches pinned sources during configure.
- **Android TV / Fire TV**: the APK has a TV banner and a `LEANBACK_LAUNCHER` entry, and asks for no touchscreen.
  Fire TV models without Vulkan 1.0 are not supported.
