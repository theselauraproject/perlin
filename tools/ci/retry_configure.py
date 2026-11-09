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
                shutil.rmtree(d, ignore_errors=True)
            code = 1
        for d in package_dirs(root) - before:
            print(f"::warning::removing {d}, created by the failed attempt")
            shutil.rmtree(d, ignore_errors=True)
        if attempt < attempts:
            delay = 60 * attempt
            print(f"::warning::attempt {attempt} failed (exit {code}), retrying in {delay}s")
            sys.stdout.flush()
            time.sleep(delay)
    print(f"::error::all {attempts} attempts failed")
    return 1


if __name__ == "__main__":
    sys.exit(main())
