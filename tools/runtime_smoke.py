#!/usr/bin/env python3
import json
import os
import re
import shutil
import signal
import socket
import subprocess
import sys
import tempfile
import time
import urllib.error
import urllib.request

SKIP = 77
SCENARIOS = ["TITLE", "PAUSE", "OPTIONS", "INVENTORY", "CREATIVE", "CHEST", "DEATH", "FORM", "CREDITS",
             "DISCONNECT", "LOADING", "DOWNLOAD", "HUD", "CHAT", "DEBUG", "BORDERS", "TITLE:servers",
             "TITLE:accounts", "TITLE:replays"]


def free_port():
    with socket.socket() as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


def start_display():
    if os.environ.get("PERLIN_SMOKE_USE_DISPLAY") and os.environ.get("DISPLAY"):
        return None, os.environ["DISPLAY"]
    xvfb = shutil.which("Xvfb")
    if not xvfb:
        return None, None
    for number in range(90, 120):
        if os.path.exists(f"/tmp/.X11-unix/X{number}"):
            continue
        proc = subprocess.Popen([xvfb, f":{number}", "-screen", "0", "1280x720x24", "-nolisten", "tcp"],
                                stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        for _ in range(50):
            if os.path.exists(f"/tmp/.X11-unix/X{number}"):
                return proc, f":{number}"
            if proc.poll() is not None:
                break
            time.sleep(0.1)
        proc.kill()
    return None, None


def request(port, token, path, body=None, timeout=5):
    req = urllib.request.Request(f"http://127.0.0.1:{port}{path}", headers={"X-Perlin-Token": token})
    if body is not None:
        req.data = json.dumps(body).encode()
        req.add_header("Content-Type", "application/json")
    with urllib.request.urlopen(req, timeout=timeout) as resp:
        return resp.status, resp.read()


def wait_token(log_path, proc, deadline):
    while time.time() < deadline:
        if proc.poll() is not None:
            return None
        with open(log_path, errors="replace") as f:
            m = re.search(r"token=([0-9a-fA-F]+)", f.read())
        if m:
            return m.group(1)
        time.sleep(0.2)
    return None


def fetch_frame(port, token, deadline):
    while time.time() < deadline:
        try:
            status, data = request(port, token, "/frame")
        except (urllib.error.URLError, ConnectionError, socket.timeout):
            time.sleep(0.5)
            continue
        if status == 200 and data[:2] == b"\xff\xd8":
            return data
        time.sleep(0.5)
    return None


def run_scenario(exe, display, name, workdir, startup):
    port = free_port()
    key, _, value = name.partition(":")
    home = os.path.join(workdir, name.replace(":", "_"))
    os.makedirs(home, exist_ok=True)
    log_path = os.path.join(home, "run.log")
    env = dict(os.environ, DISPLAY=display, PERLIN_DATA_DIR=home, HOME=home, SDL_AUDIODRIVER="dummy",
               PERLIN_PREVIEW="1", **{f"PERLIN_PREVIEW_{key}": value or "1"})
    with open(log_path, "w") as log:
        proc = subprocess.Popen([exe, "--accept-eula", "--control-port", str(port), "--offline", "--name", "Smoke"],
                                stdout=log, stderr=subprocess.STDOUT, env=env, start_new_session=True)
    problems = []
    try:
        token = wait_token(log_path, proc, time.time() + startup)
        if not token:
            return [f"{name}: no control token (exit={proc.poll()})"]
        frame = fetch_frame(port, token, time.time() + startup)
        if frame is None:
            problems.append(f"{name}: no frame produced")
        elif len(frame) < 4000:
            problems.append(f"{name}: frame suspiciously small ({len(frame)} bytes)")
        time.sleep(2.0)
        try:
            request(port, token, "/command", {"op": "pointer", "x": 640, "y": 360})
            request(port, token, "/command", {"op": "mouse", "x": 640, "y": 360, "button": 0, "down": True})
            request(port, token, "/command", {"op": "mouse", "x": 640, "y": 360, "button": 0, "down": False})
            request(port, token, "/command", {"op": "key", "key": 27, "down": True})
            request(port, token, "/command", {"op": "key", "key": 27, "down": False})
        except (urllib.error.URLError, ConnectionError, socket.timeout) as e:
            problems.append(f"{name}: control request failed after render: {e}")
        time.sleep(2.0)
        if proc.poll() is not None:
            problems.append(f"{name}: process died (exit={proc.returncode})")
        else:
            try:
                request(port, token, "/state")
            except (urllib.error.URLError, ConnectionError, socket.timeout) as e:
                problems.append(f"{name}: state endpoint unreachable after input: {e}")
        return problems
    finally:
        if proc.poll() is None:
            try:
                request(port, token, "/command", {"op": "shutdown"}, timeout=2)
            except Exception:
                pass
            try:
                proc.wait(timeout=10)
            except subprocess.TimeoutExpired:
                os.killpg(proc.pid, signal.SIGKILL)
                proc.wait()
        if problems:
            with open(log_path, errors="replace") as f:
                tail = f.read()[-1500:]
            problems.append(f"{name} log tail:\n{tail}")


def main():
    if len(sys.argv) < 2:
        print("usage: runtime_smoke.py <Perlin executable> [scenario...]")
        return 2
    exe = os.path.abspath(sys.argv[1])
    if not os.path.exists(exe):
        print(f"missing executable {exe}")
        return 2
    scenarios = sys.argv[2:] or SCENARIOS
    display_proc, display = start_display()
    if not display:
        print("no X display available; skipping")
        return SKIP
    workdir = tempfile.mkdtemp(prefix="perlin_smoke_")
    failures = []
    try:
        for name in scenarios:
            problems = run_scenario(exe, display, name, workdir, 60)
            print(f"{'FAIL' if problems else 'ok  '} {name}")
            failures += problems
    finally:
        if display_proc:
            display_proc.kill()
        shutil.rmtree(workdir, ignore_errors=True)
    for p in failures:
        print(p)
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
