# iOS / iPadOS

Perlin builds for iPhone and iPad (arm64, iOS 15+) from the same CMake project. CI builds it in the `ios` job of
`.github/workflows/build.yml` and publishes `Perlin.ipa` next to the other packages.

## What the iOS build is

| Area        | iOS choice                                                                                   |
|-------------|----------------------------------------------------------------------------------------------|
| Graphics    | Metal (the same backend as macOS, drawing into SDL's `UIView` layer)                          |
| Mods        | wasm3 interpreter (as on the web); WasmEdge cannot run where apps may not map executable pages |
| Input       | Touch controls on by default (`PERLIN_MOBILE`), game controllers through SDL                   |
| Data        | `Documents/` in the app sandbox, visible in the Files app (packs, worlds, mods, imports)       |
| TLS         | `cacert.pem` shipped in the bundle (iOS has no trust store OpenSSL can read)                  |
| Assets      | `assets-manifest.json` in the bundle, one bulk download of the vanilla samples (as on Android)|
| Singleplayer| The server is linked into the app, as on every platform                                      |
| Importing   | The system document picker (Files, iCloud Drive) for skins, capes, packs and mods            |
| Updates     | Add the AltStore / SideStore source `…/releases/download/nightly/altstore-source.json`       |
| Not on iOS  | FFmpeg video export, the local control server                                                |

`src/util/Platform.hpp` defines `PERLIN_IOS` and `PERLIN_MOBILE`; code that is about phones rather than Android
specifically tests `PERLIN_MOBILE`.

## Building locally (on a Mac with Xcode)

```sh
# OpenSSL for iOS, once
git clone --depth 1 -b openssl-3.0.15 https://github.com/openssl/openssl openssl-src
(cd openssl-src && ./Configure ios64-xcrun no-shared no-tests no-engine no-dso -mios-version-min=15.0 \
    --prefix="$PWD/../ossl-ios" && make -j build_libs && make install_dev)

cmake -B build-ios -G Ninja -DCMAKE_SYSTEM_NAME=iOS -DCMAKE_OSX_ARCHITECTURES=arm64 \
    -DOPENSSL_ROOT_DIR="$PWD/ossl-ios" \
    -DOPENSSL_CRYPTO_LIBRARY="$PWD/ossl-ios/lib/libcrypto.a" -DOPENSSL_SSL_LIBRARY="$PWD/ossl-ios/lib/libssl.a"
cmake --build build-ios --target Perlin
```

The result is `build-ios/Perlin.app`. Options:

- `PERLIN_IOS_DEPLOYMENT_TARGET` (default `15.0`)
- `PERLIN_IOS_CA_BUNDLE` (default `/etc/ssl/cert.pem` of the build Mac)
- `PERLIN_ASSET_MANIFEST`: path to an `assets-manifest.json` made by `tools/web/gen_manifest.py`

## Installing

The CI `.ipa` is ad-hoc signed. Install it with AltStore or Sideloadly (they re-sign it with your Apple ID) or with
TrollStore. To sign it yourself for a device, open the `.app` in Xcode's "Devices and Simulators" window after
`codesign --force --sign "Apple Development: …" --entitlements <your.entitlements> Perlin.app`.
