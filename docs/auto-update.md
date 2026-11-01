# Nightly builds and auto update

Every push to `main` builds all platforms and replaces the assets of the `nightly` release:

| Asset | Platform |
|-------|----------|
| `Perlin-windows-x64.zip`, `Perlin-windows-arm64.zip` | Windows (x64, ARM) |
| `Perlin-linux-x64.zip`, `Perlin-linux-arm64.zip` | Linux (x64, arm64) |
| `Perlin-linux-x64.AppImage`, `Perlin-linux-arm64.AppImage` | Linux, single file (replaces itself on update) |
| `Perlin.flatpak` | Linux Flatpak bundle (updates through Flatpak) |
| `Perlin-freebsd-x64.zip` | FreeBSD |
| `Perlin-macos-arm64.zip`, `Perlin-macos-x64.zip` | macOS 12+ (signed app bundle, zipped with `ditto` on the Mac) |
| `Perlin.apk` | Android phones, tablets, Android TV / Fire TV, Chromebooks |
| `Perlin.ipa`, `altstore-source.json` | iOS / iPadOS 15+ |
| `Perlin-web.zip` | the web build (also deployed to GitHub Pages) |

Builds of `main` carry their commit and repository (`PERLIN_BUILD_COMMIT`, `PERLIN_UPDATE_REPO`, CMake cache
variables set by CI). At start the game asks GitHub which commit the `nightly` tag points at; when it differs:

- **Windows, Linux, macOS**: the platform's archive is downloaded and unpacked beside the game in the background
  (`.perlin-update/`). The title screen shows **Restart to update**; otherwise the new build is moved into place when
  the game closes. Replaced files are renamed `*.perlin-old` and deleted on the next start. A folder the game cannot
  write to (macOS App Translocation, a system-wide install) shows the update as available instead.
- **Android**: the apk is downloaded and the title screen offers to install it; Android shows its own confirmation.
- **iOS**: AltStore / SideStore update the app from `altstore-source.json`; the title screen links the release.
- **Web**: the page compares `version.json` with its own commit and offers a reload that skips the caches.

Options → **Auto Update** (main page) turns it off; `PERLIN_NO_UPDATE=1` does too. Local builds never update.

## Repository secrets

| Secret | Why |
|--------|-----|
| `ANDROID_KEYSTORE_BASE64`, `ANDROID_KEYSTORE_PASSWORD`, `ANDROID_KEY_ALIAS`, `ANDROID_KEY_PASSWORD` | Android installs an update only over an apk signed with the same key. Without them every run signs with a throwaway key and updates fail. |
| `MACOS_CERTIFICATE_P12` (base64), `MACOS_CERTIFICATE_PASSWORD`, `MACOS_SIGNING_IDENTITY` | Developer ID signing. |
| `APPLE_ID`, `APPLE_TEAM_ID`, `APPLE_APP_PASSWORD` | Notarization: with it a downloaded Perlin opens with no Gatekeeper warning at all. |

Without the Apple secrets the app is ad-hoc signed: macOS opens it after **System Settings → Privacy & Security →
Open Anyway** (or `xattr -cr Perlin.app`). It no longer reports the app as damaged. Updates installed by the game
itself are never quarantined, so the warning only ever appears for the first download.
