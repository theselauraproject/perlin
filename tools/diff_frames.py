#!/usr/bin/env python3
import pathlib
import sys

from PIL import Image, ImageChops


def main():
    if len(sys.argv) < 3:
        print("usage: diff_frames.py <baseline dir> <new dir> [max mean difference]")
        return 2
    base = pathlib.Path(sys.argv[1])
    new = pathlib.Path(sys.argv[2])
    limit = float(sys.argv[3]) if len(sys.argv) > 3 else 1.0
    bad = 0
    for path in sorted(new.glob("*.jpg")):
        other = base / path.name
        if not other.exists():
            print(f"missing baseline {path.name}")
            bad += 1
            continue
        a = Image.open(path).convert("RGB")
        b = Image.open(other).convert("RGB")
        if a.size != b.size:
            print(f"FAIL {path.name}: size {a.size} vs {b.size}")
            bad += 1
            continue
        diff = ImageChops.difference(a, b).convert("L")
        hist = diff.histogram()
        total = a.size[0] * a.size[1]
        mean = sum(i * c for i, c in enumerate(hist)) / total
        changed = sum(c for i, c in enumerate(hist) if i > 24) / total
        status = "ok  " if mean <= limit else "FAIL"
        bad += 0 if mean <= limit else 1
        print(f"{status} {path.name}: mean {mean:.3f}, {changed * 100:.2f}% pixels differ")
        if mean > limit:
            diff.point(lambda v: 255 if v > 24 else 0).save(new / (path.stem + "_diff.png"))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
