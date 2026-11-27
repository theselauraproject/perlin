#!/usr/bin/env python3
"""Compare two prebuilt chunk_visibility_bench binaries in ABBA order.

No compiler or build tree is invoked. Each binary must use the same benchmark
scene/source, shader pack and Conduit backend; only the intended renderer
implementation should differ. This measures CPU work on the null backend.
"""

import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import statistics
import subprocess


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("before", type=Path)
    parser.add_argument("after", type=Path)
    parser.add_argument("--shader-pack", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--radius", type=int, default=24)
    parser.add_argument("--frames", type=int, default=2000)
    parser.add_argument("--repeats", type=int, default=1)
    parser.add_argument("--cpu", type=int)
    args = parser.parse_args()
    if args.radius < 1 or args.frames < 1 or args.repeats < 1:
        parser.error("radius, frames and repeats must be positive")
    paths = {name: getattr(args, name).resolve() for name in ("before", "after")}
    pack = args.shader_pack.resolve()
    if any(not p.is_file() for p in (*paths.values(), pack)):
        parser.error("both binaries and the shader pack must exist")
    if args.cpu is not None:
        if not hasattr(os, "sched_setaffinity"):
            parser.error("explicit CPU affinity is not available on this platform")
        os.sched_setaffinity(0, {args.cpu})
    output = {
        "metadata": {
            "platform": platform.platform(),
            "cpu_affinity": sorted(os.sched_getaffinity(0)) if hasattr(os, "sched_getaffinity") else None,
            "radius": args.radius, "frames": args.frames, "repeats": args.repeats,
            "order": ["before", "after", "after", "before"] * args.repeats,
            "backend": "Conduit null (CPU submission only)",
            "binaries": {name: sha256(path) for name, path in paths.items()},
            "shader_pack_sha256": sha256(pack),
        },
        "records": [], "raw_runs": [],
    }
    checksums = set()
    for run, variant in enumerate(output["metadata"]["order"]):
        result = subprocess.run(
            [str(paths[variant]), str(pack), str(args.radius), str(args.frames)],
            check=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
        )
        checksum = re.search(r"^BENCH checksum=(\d+)$", result.stdout, re.MULTILINE)
        if not checksum:
            raise RuntimeError("benchmark did not emit its output checksum")
        checksums.add(int(checksum[1]))
        output["raw_runs"].append({"run": run, "variant": variant,
                                   "checksum": int(checksum[1]), "stdout": result.stdout})
        found = set()
        for line in result.stdout.splitlines():
            if not line.startswith("BENCH ") or "chunks=" not in line:
                continue
            parts = line.split()
            record = {"run": run, "variant": variant, "case": parts[1]}
            record.update((key, float(value)) for key, value in (part.split("=") for part in parts[2:]))
            output["records"].append(record)
            found.add(parts[1])
        if found != {"stationary", "moving", "mesh_churn"}:
            raise RuntimeError("benchmark did not emit all three workloads")
        print(f"RUN {run} {variant}\n{result.stdout}", flush=True)
    output["checksums_match"] = len(checksums) == 1
    output["summary"] = {}
    for case in ("stationary", "moving", "mesh_churn"):
        times = {
            variant: statistics.median(r["per_frame_us"] for r in output["records"]
                                       if r["variant"] == variant and r["case"] == case)
            for variant in paths
        }
        output["summary"][case] = {
            "before_us_per_frame": times["before"], "after_us_per_frame": times["after"],
            "reduction_percent": 100 * (1 - times["after"] / times["before"]),
            "speedup": times["before"] / times["after"],
        }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(output, indent=2) + "\n")
    if not output["checksums_match"]:
        raise RuntimeError("output checksums differ; timing comparison is invalid")


if __name__ == "__main__":
    main()
