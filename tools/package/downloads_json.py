#!/usr/bin/env python3
"""Writes the list of downloads of a release as JSON, for a website's download page.

    downloads_json.py <release folder> <owner/repo> <tag> <commit>

Each file becomes an entry with the platform, architecture, kind (installer, portable, ...), size,
SHA-256 and its download link on the GitHub release.
"""
import hashlib
import json
import os
import re
import sys

KINDS = [
    (r"-setup\.exe$", "installer", "Windows installer"),
    (r"-portable\.zip$", "portable", "Portable (no install)"),
    (r"\.dmg$", "disk-image", "Disk image"),
    (r"\.AppImage$", "appimage", "AppImage"),
    (r"\.deb$", "deb", "Debian / Ubuntu package"),
    (r"\.flatpak$", "flatpak", "Flatpak bundle"),
    (r"\.apk$", "apk", "Android package"),
    (r"\.ipa$", "ipa", "iOS app (sideload)"),
    (r"-web\.zip$", "web", "Web build"),
    (r"-symbols\.zip$", "symbols", "Debug symbols"),
    (r"\.zip$", "archive", "Zip archive"),
]


def describe(name):
    lower = name.lower()
    platform = next((p for p in ("windows", "macos", "linux", "freebsd", "android", "ios", "web") if p in lower), None)
    if platform is None:
        platform = "android" if lower.endswith(".apk") else "ios" if lower.endswith(".ipa") else "linux" if lower.endswith(".flatpak") else "other"
    arch = "arm64" if ("arm64" in lower or "aarch64" in lower) else "x64" if ("x64" in lower or "x86_64" in lower or lower.endswith(".flatpak")) else None
    for pattern, kind, label in KINDS:
        if re.search(pattern, name):
            return platform, arch, kind, label
    return platform, arch, "file", "File"


def main():
    folder, repo, tag, commit = sys.argv[1:5]
    files = []
    for name in sorted(os.listdir(folder)):
        path = os.path.join(folder, name)
        if not os.path.isfile(path) or name in ("downloads.json", "SHA256SUMS.txt", "altstore-source.json"):
            continue
        digest = hashlib.sha256()
        with open(path, "rb") as f:
            for block in iter(lambda: f.read(1 << 20), b""):
                digest.update(block)
        platform, arch, kind, label = describe(name)
        files.append({
            "name": name,
            "platform": platform,
            "arch": arch,
            "kind": kind,
            "label": label,
            "size": os.path.getsize(path),
            "sha256": digest.hexdigest(),
            "url": f"https://github.com/{repo}/releases/download/{tag}/{name}",
        })
    json.dump({"tag": tag, "commit": commit, "files": files}, sys.stdout, indent=2)
    sys.stdout.write("\n")


if __name__ == "__main__":
    main()
