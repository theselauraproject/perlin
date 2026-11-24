#!/usr/bin/env python3
"""Compare two builds of perlin_meshbench (or `Perlin --meshbench`).

Runs the two binaries in interleaved rounds, so machine load hits both alike, and reports medians.
With --instructions each round runs under valgrind's callgrind and counts the instructions spent in
meshing only (BuildChunkMesh and Tessellator::End): a count stays the same under load where timings
do not, so use it to judge small changes.

    tools/bench_compare.py build-old/perlin_meshbench build-new/perlin_meshbench --rounds 3
    tools/bench_compare.py old new --instructions --radius 2

The checksum is of the vertices the mesher made. It changes only when the output changes, so a pure
speed-up must keep it; a change to what is drawn (such as merging more faces) changes it, and then
the bench's own exactness check ("merge: ... sample points differ") is what says the picture is the same.
Set PERLIN_SAMPLES to mesh with textures, tints and block models.
"""

import argparse
import os
import re
import statistics
import subprocess
import sys
import tempfile


def run_timed(binary, radius, rounds):
    out = subprocess.run([binary, str(radius), str(rounds)], capture_output=True, text=True)
    text = out.stdout + out.stderr
    if out.returncode != 0 or "FAIL" in text:
        sys.exit(f"{binary} failed:\n{text[-2000:]}")
    median = re.search(r"median ([0-9.]+) ms", text)
    result = re.search(r"RESULT mesh=([0-9.]+) checksum=([0-9a-f]+)", text)
    merge = re.search(r"merge: (\d+) -> (\d+) vertices", text)
    if not median or not result:
        sys.exit(f"{binary}: no RESULT line")
    return {
        "value": float(median.group(1)),
        "checksum": result.group(2),
        "vertices": int(merge.group(2)) if merge else None,
    }


def run_counted(binary, radius):
    with tempfile.TemporaryDirectory() as tmp:
        out_file = os.path.join(tmp, "callgrind.out")
        cmd = [
            "valgrind", "--tool=callgrind", "--collect-atstart=no",
            "--toggle-collect=perlin::BuildChunkMesh(*", "--toggle-collect=perlin::Tessellator::End(*",
            f"--callgrind-out-file={out_file}", binary, str(radius), "1",
        ]
        out = subprocess.run(cmd, capture_output=True, text=True)
    text = out.stdout + out.stderr
    collected = re.search(r"Collected : ([0-9,]+)", text)
    result = re.search(r"RESULT mesh=([0-9.]+) checksum=([0-9a-f]+)", text)
    merge = re.search(r"merge: (\d+) -> (\d+) vertices", text)
    if out.returncode != 0 or not collected or not result:
        sys.exit(f"{binary} failed under callgrind:\n{text[-2000:]}")
    chunks = (2 * radius + 1) ** 2
    builds = chunks * 4 + 21  # warm-up, one round, the exactness pair, and the edit checks
    return {
        "value": int(collected.group(1).replace(",", "")) / builds,
        "checksum": result.group(2),
        "vertices": int(merge.group(2)) if merge else None,
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("before")
    parser.add_argument("after")
    parser.add_argument("--rounds", type=int, default=3, help="interleaved rounds (medians are reported)")
    parser.add_argument("--radius", type=int, default=4, help="chunks around the centre to mesh")
    parser.add_argument("--instructions", action="store_true", help="count instructions with callgrind")
    args = parser.parse_args()

    samples = {"before": [], "after": []}
    for _ in range(args.rounds):
        for label, binary in (("before", args.before), ("after", args.after)):
            if args.instructions:
                samples[label].append(run_counted(binary, args.radius))
            else:
                samples[label].append(run_timed(binary, args.radius, 3))

    unit = "instructions per build" if args.instructions else "ms per chunk (median)"
    before = statistics.median(s["value"] for s in samples["before"])
    after = statistics.median(s["value"] for s in samples["after"])
    change = (after - before) / before * 100.0 if before else 0.0
    print(f"{unit}: before {before:,.3f}, after {after:,.3f} ({change:+.1f}%)")
    vb, va = samples["before"][0]["vertices"], samples["after"][0]["vertices"]
    if vb and va:
        print(f"vertices after merging: before {vb:,}, after {va:,} ({(va - vb) / vb * 100.0:+.1f}%)")
    cb, ca = samples["before"][0]["checksum"], samples["after"][0]["checksum"]
    print("checksum: same (output unchanged)" if cb == ca else f"checksum: {cb} -> {ca} (output changed)")


if __name__ == "__main__":
    main()
