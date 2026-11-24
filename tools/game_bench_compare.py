#!/usr/bin/env python3
"""Compare two builds of Perlin on the built-in benchmark (`--benchmark SECONDS`).

Runs the builds in interleaved rounds, so machine load hits both alike, and reports per build the median of:
frame rate (real and, with frame generation, shown), median frame time, CPU seconds per thread role (summed
over the thread names, e.g. server-chunkgen, perlin-mesh, perlin-render), peak resident memory, and the chunks
the client received. Chunks received matters as much as frames: a build that loads the world faster does more
terrain work per second while loading, which on a machine with few cores can cost frames.

Each run starts from a copy of --template, a data directory you prepared (it needs your own game assets, which
Perlin downloads on first start; nothing here ships them), so both builds see the same settings. Linux only:
CPU and memory come from /proc.

    tools/game_bench_compare.py build-old/Perlin build-new/Perlin --template ~/perlin-bench-data --seconds 60
    tools/game_bench_compare.py old new --template data --rounds 3 --set frameGen=2x --set renderer=gles
"""

import argparse
import collections
import json
import os
import re
import shutil
import statistics
import subprocess
import sys
import tempfile
import threading
import time


def watch(proc, out):
    """Sample per-thread CPU seconds and peak RSS of proc until it exits; keep the largest snapshot."""
    tick = os.sysconf("SC_CLK_TCK")
    best = {}
    while proc.poll() is None:
        snap = collections.Counter()
        try:
            for tid in os.listdir(f"/proc/{proc.pid}/task"):
                try:
                    with open(f"/proc/{proc.pid}/task/{tid}/stat") as f:
                        stat = f.read()
                    with open(f"/proc/{proc.pid}/task/{tid}/comm") as f:
                        name = re.sub(r"[-_ ]?\d+$", "", f.read().strip())
                except OSError:
                    continue
                fields = stat[stat.rindex(")") + 2:].split()
                snap[name] += (int(fields[11]) + int(fields[12])) / tick
            with open(f"/proc/{proc.pid}/status") as f:
                for line in f:
                    if line.startswith("VmHWM:"):
                        out["peak_rss_mib"] = int(line.split()[1]) / 1024
        except OSError:
            pass
        if sum(snap.values()) >= sum(best.values()):
            best = dict(snap)
        time.sleep(1.0)
    out["cpu"] = best


def run(binary, template, seconds, settings, timeout):
    data = tempfile.mkdtemp(prefix="perlin-bench-")
    shutil.rmtree(data)
    shutil.copytree(template, data, symlinks=True)
    for sub in ("benchmarks", "benchmark-world"):
        shutil.rmtree(os.path.join(data, sub), ignore_errors=True)
    options_path = os.path.join(data, "options.json")
    options = json.load(open(options_path)) if os.path.exists(options_path) else {}
    options.update(settings)
    json.dump(options, open(options_path, "w"), indent=1)
    report = os.path.join(data, "report.json")
    env = dict(os.environ, PERLIN_DATA_DIR=data, HOME=data)
    log_path = os.path.join(data, "stdout.log")
    with open(log_path, "w") as log:
        proc = subprocess.Popen([os.path.abspath(binary), "--accept-eula", "--offline", "--name", "Bench",
                                 "--benchmark", str(seconds), "--benchmark-out", report],
                                stdout=log, stderr=subprocess.STDOUT, env=env, cwd=data)
        sampled = {}
        sampler = threading.Thread(target=watch, args=(proc, sampled))
        sampler.start()
        try:
            proc.wait(timeout=timeout)
        except subprocess.TimeoutExpired:
            proc.kill()
        sampler.join()
    text = open(log_path, errors="replace").read()
    chunks = re.findall(r"\[net\] \d+s: .*?chunks=(\d+)", text)
    result = {"cpu": sampled.get("cpu", {}), "peak_rss_mib": sampled.get("peak_rss_mib", 0.0),
              "chunks": int(chunks[-1]) if chunks else 0}
    if os.path.exists(report):
        result["report"] = json.load(open(report))
    shutil.rmtree(data, ignore_errors=True)
    return result


def summarize(name, runs):
    def med(values):
        values = [v for v in values if v is not None]
        return statistics.median(values) if values else float("nan")
    reports = [r.get("report", {}) for r in runs]
    print(f"\n{name}  ({len(runs)} runs)")
    print(f"  real fps         {med([r.get('average_fps') for r in reports]):8.2f}")
    if any(r.get("generated_frames") for r in reports):
        print(f"  shown fps        {med([r.get('shown_fps') for r in reports]):8.2f}")
    print(f"  median frame ms  {med([r.get('frame_ms', {}).get('median') for r in reports]):8.1f}")
    print(f"  chunks received  {med([r['chunks'] for r in runs]):8.0f}")
    print(f"  peak RSS MiB     {med([r['peak_rss_mib'] for r in runs]):8.0f}")
    roles = sorted({k for r in runs for k in r["cpu"]}, key=lambda k: -med([r["cpu"].get(k, 0) for r in runs]))
    for role in roles[:8]:
        cpu = med([r["cpu"].get(role, 0.0) for r in runs])
        per_chunk = med([r["cpu"].get(role, 0.0) * 1000 / r["chunks"] for r in runs if r["chunks"]])
        print(f"  cpu {role:22s} {cpu:7.1f} s  ({per_chunk:6.1f} ms per chunk received)")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("old")
    ap.add_argument("new")
    ap.add_argument("--template", required=True, help="prepared data directory copied for every run")
    ap.add_argument("--seconds", type=int, default=60)
    ap.add_argument("--rounds", type=int, default=2)
    ap.add_argument("--set", action="append", default=[], metavar="KEY=VALUE",
                    help="override an options.json setting for both builds (JSON values allowed)")
    a = ap.parse_args()
    settings = {}
    for item in a.set:
        key, _, value = item.partition("=")
        try:
            settings[key] = json.loads(value)
        except json.JSONDecodeError:
            settings[key] = value
    timeout = a.seconds * 4 + 300
    results = {a.old: [], a.new: []}
    for round_index in range(a.rounds):
        for binary in (a.old, a.new):
            print(f"round {round_index + 1}: {binary}", file=sys.stderr)
            results[binary].append(run(binary, a.template, a.seconds, settings, timeout))
    for binary in (a.old, a.new):
        summarize(binary, results[binary])


if __name__ == "__main__":
    main()
