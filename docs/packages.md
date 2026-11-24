# Downloads and packages

Every build of `main` (the `nightly` release) and every `v*` tag publishes the same program in
these shapes:

| File | What it is |
|---|---|
| `Perlin-windows-x64-setup.exe`, `Perlin-windows-arm64-setup.exe` | Installer. Installs for the current user by default (no administrator prompt) into `%LOCALAPPDATA%\Programs\Perlin`, adds a Start menu entry and an uninstaller; the built-in updater keeps it current. |
| `Perlin-windows-x64-portable.zip`, `Perlin-windows-arm64-portable.zip` | Portable copy. Unzip anywhere and run `Perlin.exe`; `portable.txt` beside it makes Perlin keep its settings, worlds and downloads in a `data` folder next to it instead of `%APPDATA%`. Delete `portable.txt` to make it an ordinary copy. |
| `Perlin-windows-x64.zip`, `Perlin-windows-arm64.zip` | The plain program folder (what the updater downloads). |
| `Perlin-macos-arm64.dmg`, `Perlin-macos-x64.dmg` | Disk image: drag Perlin to Applications. |
| `Perlin-macos-*.zip` | The app bundle zipped (what the updater downloads). |
| `Perlin-linux-x64.AppImage`, `Perlin-linux-arm64.AppImage` | Runs on most distributions without installing; updates itself. |
| `Perlin-linux-x64.deb`, `Perlin-linux-arm64.deb` | Debian and Ubuntu package (`sudo apt install ./Perlin-linux-x64.deb`). Installs to `/opt/perlin` with `perlin` on the PATH; apt updates it, so the built-in updater stays off. |
| `Perlin.flatpak` | Flatpak bundle (`flatpak install Perlin.flatpak`); needs the Flathub remote for the runtime. |
| `Perlin-linux-*.zip`, `Perlin-freebsd-x64.zip` | The plain program folder. |
| `Perlin.apk`, `Perlin.ipa` | Android and iOS. |
| `Perlin-web.zip` | The web build, ready to host. |

Each release also has `SHA256SUMS.txt` and `downloads.json`, a list of every file with its
platform, architecture, kind, size, SHA-256 and download link, for a website's download page:

```json
{ "tag": "nightly", "commit": "…", "files": [
  { "name": "Perlin-windows-x64-setup.exe", "platform": "windows", "arch": "x64", "kind": "installer",
    "label": "Windows installer", "size": 123, "sha256": "…",
    "url": "https://github.com/<owner>/<repo>/releases/download/nightly/Perlin-windows-x64-setup.exe" } ] }
```

The latest nightly's list is always at
`https://github.com/<owner>/<repo>/releases/download/nightly/downloads.json`.

Building them locally: `tools/package/make-appimage.sh`, `make-deb.sh` and `make-dmg.sh` take the
folder `cmake --install build --prefix dist` writes; `packaging/windows/perlin.iss` is an Inno
Setup 6 script (`iscc /DSourceDir=<dist> packaging\windows\perlin.iss`).
