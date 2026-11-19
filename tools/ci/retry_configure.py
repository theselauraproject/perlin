#!/usr/bin/env python3
"""Runs a configure (or any) command up to N times for CI.

CPM treats an existing package folder in CPM_SOURCE_CACHE as downloaded. A configure that fails halfway
through a download leaves an empty or partial folder behind, and the retry would then skip it and build
with a missing tool. So package folders that appear during a failed attempt are deleted before the next
one, and after a successful attempt the downloaded tools are checked before the cache gets saved.

usage: retry_configure.py [--attempts N] -- command args...
"""
import os
import shutil
import stat
import subprocess
import sys
import time
from pathlib import Path


def cache_root() -> Path | None:
    root = os.environ.get("CPM_SOURCE_CACHE")
    return Path(root) if root else None


def package_dirs(root: Path | None) -> set[Path]:
    if not root or not root.is_dir():
        return set()
    return {version for package in root.iterdir() if package.is_dir()
            for version in package.iterdir() if version.is_dir()}


def broken_packages(root: Path | None) -> list[Path]:
    """Empty package folders, and tool packages without their executable."""
    broken = []
    for d in package_dirs(root):
        if not any(d.iterdir()):
            broken.append(d)
            continue
        if d.parent.name == "slang" and not any((d / "bin").glob("slangc*")):
            broken.append(d)
    return broken


def remove_tree(d: Path) -> None:
    def writable(func, path, _):
        os.chmod(path, stat.S_IWRITE)
        func(path)
    for _ in range(3):
        if not d.exists():
            return
        if sys.version_info >= (3, 12):
            shutil.rmtree(d, onexc=writable)
        else:
            shutil.rmtree(d, onerror=writable)
        if d.exists():
            time.sleep(2)
    if d.exists():
        print(f"::warning::could not fully remove {d}")


def build_dir(args: list[str]) -> Path:
    for i, a in enumerate(args):
        if a == "-B" and i + 1 < len(args):
            return Path(args[i + 1])
        if a.startswith("-B") and len(a) > 2:
            return Path(a[2:])
    return Path("build")


def report_failed_checks(args: list[str]) -> None:
    log = build_dir(args) / "CMakeFiles" / "CMakeConfigureLog.yaml"
    if not log.is_file():
        return
    lines = log.read_text(encoding="utf-8", errors="replace").splitlines()
    hits = [l.strip() for l in lines if "error" in l.lower() and "werror" not in l.lower()]
    seen = []
    for h in hits:
        if h not in seen:
            seen.append(h)
    if seen:
        print("::group::errors from CMakeConfigureLog.yaml")
        for h in seen[:40]:
            print(h)
        print("::endgroup::")


def main() -> int:
    args = sys.argv[1:]
    attempts = 3
    if args[:1] == ["--attempts"]:
        attempts = int(args[1])
        args = args[2:]
    if args[:1] == ["--"]:
        args = args[1:]
    if not args:
        print(__doc__)
        return 2
    root = cache_root()
    for attempt in range(1, attempts + 1):
        before = package_dirs(root)
        code = subprocess.call(args)
        if code == 0:
            bad = broken_packages(root)
            if not bad:
                return 0
            for d in bad:
                print(f"::warning::{d} is incomplete, removing it")
                remove_tree(d)
            code = 1
        for d in package_dirs(root) - before:
            print(f"::warning::removing {d}, created by the failed attempt")
            remove_tree(d)
        if attempt == 1:
            report_failed_checks(args)
        if attempt < attempts:
            delay = 60 * attempt
            print(f"::warning::attempt {attempt} failed (exit {code}), retrying in {delay}s")
            sys.stdout.flush()
            time.sleep(delay)
    print(f"::error::all {attempts} attempts failed")
    return 1


if __name__ == "__main__":
    sys.exit(main())
