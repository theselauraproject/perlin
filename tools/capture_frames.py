#!/usr/bin/env python3
import os
import pathlib
import shutil
import signal
import subprocess
import sys
import tempfile
import time

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import runtime_smoke as smoke


def capture(exe, display, name, out_dir, workdir):
    port = smoke.free_port()
    key, _, value = name.partition(":")
    home = os.path.join(workdir, name.replace(":", "_"))
    os.makedirs(home, exist_ok=True)
    log_path = os.path.join(home, "run.log")
    env = dict(os.environ, DISPLAY=display, PERLIN_DATA_DIR=home, HOME=home, SDL_AUDIODRIVER="dummy",
               PERLIN_PREVIEW="1", **{f"PERLIN_PREVIEW_{key}": value or "1"})
    with open(log_path, "w") as log:
        proc = subprocess.Popen([exe, "--accept-eula", "--control-port", str(port), "--offline", "--name", "Smoke"],
                                stdout=log, stderr=subprocess.STDOUT, env=env, start_new_session=True)
    try:
        token = smoke.wait_token(log_path, proc, time.time() + 60)
        if not token:
            return False
        smoke.fetch_frame(port, token, time.time() + 60)
        time.sleep(3.0)
        frame = smoke.fetch_frame(port, token, time.time() + 30)
        if frame is None:
            return False
        (out_dir / (name.replace(":", "_") + ".jpg")).write_bytes(frame)
        return True
    finally:
        if proc.poll() is None:
            try:
                smoke.request(port, token, "/command", {"op": "shutdown"}, timeout=2)
            except Exception:
                pass
            try:
                proc.wait(timeout=10)
            except subprocess.TimeoutExpired:
                os.killpg(proc.pid, signal.SIGKILL)
                proc.wait()


def main():
    if len(sys.argv) < 3:
        print("usage: capture_frames.py <Perlin executable> <output dir> [scenario...]")
        return 2
    exe = os.path.abspath(sys.argv[1])
    out_dir = pathlib.Path(sys.argv[2])
    out_dir.mkdir(parents=True, exist_ok=True)
    scenarios = sys.argv[3:] or smoke.SCENARIOS
    display_proc, display = smoke.start_display()
    if not display:
        print("no X display available")
        return smoke.SKIP
    workdir = tempfile.mkdtemp(prefix="perlin_frames_")
    bad = 0
    try:
        for name in scenarios:
            ok = capture(exe, display, name, out_dir, workdir)
            print(f"{'ok  ' if ok else 'FAIL'} {name}")
            bad += 0 if ok else 1
    finally:
        if display_proc:
            display_proc.kill()
        shutil.rmtree(workdir, ignore_errors=True)
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
