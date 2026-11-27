#!/usr/bin/env python3
"""Compare prebuilt full-mesher executables in ABBA order, with exactness checks.

The two binaries must use identical benchmark/dependency sources and build flags,
apart from the intended mesher change. This runner does not compile anything.
PERLIN_SAMPLES is cleared unless --samples explicitly selects the same pack for
both binaries. Results are CPU measurements, not a renderer or GPU FPS test.
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
import time


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("before", type=Path)
    parser.add_argument("after", type=Path)
    parser.add_argument("--workdir", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--radius", type=int, default=2)
    parser.add_argument("--rounds", type=int, default=8)
    parser.add_argument("--repeats", type=int, default=1)
    parser.add_argument("--cpu", type=int)
    parser.add_argument("--samples", type=Path)
    args = parser.parse_args()
    if args.radius < 0 or args.rounds < 1 or args.repeats < 1:
        parser.error("radius must be nonnegative; rounds and repeats must be positive")
    binaries = {name: getattr(args, name).resolve() for name in ("before", "after")}
    if any(not path.is_file() for path in binaries.values()) or not args.workdir.is_dir():
        parser.error("both binaries and the working directory must exist")
    if args.cpu is not None:
        if not hasattr(os, "sched_setaffinity"):
            parser.error("explicit CPU affinity is not available on this platform")
        os.sched_setaffinity(0, {args.cpu})
    env = dict(os.environ)
    env.pop("PERLIN_SAMPLES", None)
    if args.samples:
        env["PERLIN_SAMPLES"] = str(args.samples.resolve())
    args.output.parent.mkdir(parents=True, exist_ok=True)
    report = {
        "metadata": {
            "platform": platform.platform(),
            "cpu_affinity": sorted(os.sched_getaffinity(0)) if hasattr(os, "sched_getaffinity") else None,
            "radius": args.radius, "rounds": args.rounds, "repeats": args.repeats,
            "chunks": (2 * args.radius + 1) ** 2,
            "order": ["before", "after", "after", "before"] * args.repeats,
            "samples": str(args.samples.resolve()) if args.samples else None,
            "scope": "CPU full meshing, synthetic world; no GPU or renderer timing",
            "binary_sha256": {name: sha256(path) for name, path in binaries.items()},
        },
        "runs": [],
    }
    for run, variant in enumerate(report["metadata"]["order"]):
        started = time.perf_counter()
        result = subprocess.run(
            [str(binaries[variant]), str(args.radius), str(args.rounds)],
            cwd=args.workdir, env=env, stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT, text=True,
        )
        log_name = f"{args.output.stem}-{run}-{variant}.log"
        (args.output.parent / log_name).write_text(result.stdout)
        if result.returncode:
            raise RuntimeError(f"{variant} exited {result.returncode}; see {log_name}")
        measured = re.search(
            r"\[meshbench\] (\d+) chunks x (\d+) rounds: mean ([\d.]+) ms, median ([\d.]+) ms, "
            r"p90 ([\d.]+) ms, (\d+) vertices/chunk", result.stdout)
        checksum = re.search(r"^RESULT mesh=([\d.]+) checksum=([0-9a-f]+)$", result.stdout, re.MULTILINE)
        merge = re.search(r"\[meshbench\] merge: (\d+) -> (\d+) vertices .*?, (\d+) of (\d+) sample points differ", result.stdout)
        edits = re.findall(r"^\[meshbench\] (.+?)\s+(same|DIFFERS) \((\d+) sections rebuilt, (\d+) copied\)$",
                           result.stdout, re.MULTILINE)
        if not measured or not checksum or not merge or len(edits) != 10:
            raise RuntimeError(f"incomplete benchmark output; see {log_name}")
        if int(measured[1]) != report["metadata"]["chunks"] or int(measured[2]) != args.rounds:
            raise RuntimeError("benchmark workload does not match the requested workload")
        if int(merge[3]) or any(edit[1] != "same" for edit in edits):
            raise RuntimeError(f"mesher exactness failure; see {log_name}")
        record = {
            "run": run, "variant": variant, "log": log_name,
            "wall_seconds": time.perf_counter() - started,
            "mean_ms": float(measured[3]), "median_ms": float(measured[4]),
            "p90_ms": float(measured[5]), "vertices_per_chunk": int(measured[6]),
            "checksum": checksum[2], "merge_flat_vertices": int(merge[1]),
            "merge_merged_vertices": int(merge[2]), "merge_sample_points": int(merge[4]),
            "merge_mismatches": int(merge[3]), "full_partial_edits_passed": len(edits),
        }
        report["runs"].append(record)
        print(json.dumps(record), flush=True)
    exact_fields = ("checksum", "vertices_per_chunk", "merge_flat_vertices", "merge_merged_vertices",
                    "merge_sample_points", "merge_mismatches", "full_partial_edits_passed")
    report["exactness_matches"] = all(len({run[key] for run in report["runs"]}) == 1 for key in exact_fields)
    medians = {name: statistics.median(run["mean_ms"] for run in report["runs"] if run["variant"] == name)
               for name in binaries}
    report["summary"] = {
        "before_ms_per_chunk": medians["before"], "after_ms_per_chunk": medians["after"],
        "reduction_percent": 100 * (1 - medians["after"] / medians["before"]),
        "speedup": medians["before"] / medians["after"],
    }
    args.output.write_text(json.dumps(report, indent=2) + "\n")
    if not report["exactness_matches"]:
        raise RuntimeError("mesher outputs differ between runs; timing comparison is invalid")
    print(json.dumps(report["summary"], indent=2), flush=True)


if __name__ == "__main__":
    main()
