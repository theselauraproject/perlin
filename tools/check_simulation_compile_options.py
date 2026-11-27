#!/usr/bin/env python3
"""Check the generated compiler commands for simulation floating-point rules."""

import argparse
import json
from pathlib import Path
import shlex
import sys


SOURCES = (
    "src/world/movement/PlayerPhysics.cpp",
    "src/world/movement/VehiclePhysics.cpp",
    "src/client/systems/player/LocalPlayer.cpp",
    "src/client/systems/player/PlayerFov.cpp",
    "src/client/systems/player/HeldBuild.cpp",
    "src/client/systems/camera/CameraEffects.cpp",
    "src/client/systems/resources/BlockOffsetDefinition.cpp",
    "src/client/systems/resources/BlockBoxDefinition.cpp",
)


def validate(database):
    seen = set()
    failures = []
    checked = 0
    for entry in database:
        path = str(entry["file"]).replace("\\", "/")
        source = next((name for name in SOURCES if path.endswith("/" + name) or path == name), None)
        if source is None:
            continue
        seen.add(source)
        command = entry.get("arguments")
        if command is None:
            # Windows paths need their backslashes retained; compiler switches
            # are separate tokens in both Ninja compile-database formats.
            text = entry["command"]
            windows = "clang-cl" in text.lower() or "cl.exe" in text.lower()
            command = shlex.split(text, posix=not windows)
        flags = [arg.strip('"').lower() for arg in command]
        fp_modes = [arg for arg in flags if arg in ("/fp:strict", "/fp:precise", "/fp:fast")]
        if fp_modes:
            valid = fp_modes[-1] == "/fp:strict"
        else:
            fast_modes = [arg for arg in flags if arg in ("-ffast-math", "-fno-fast-math", "-ofast")]
            contracts = [arg for arg in flags if arg.startswith("-ffp-contract=")]
            valid = bool(fast_modes and fast_modes[-1] == "-fno-fast-math" and
                         contracts and contracts[-1] == "-ffp-contract=off")
        checked += 1
        if not valid:
            failures.append({"source": source, "reason": "strict floating-point options missing or superseded"})
    failures.extend({"source": name, "reason": "missing from compile database"}
                    for name in sorted(set(SOURCES) - seen))
    return {"checked_commands": checked, "checked_sources": len(seen), "failures": failures}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("compile_commands", type=Path)
    args = parser.parse_args()
    result = validate(json.loads(args.compile_commands.read_text(encoding="utf-8")))
    print(json.dumps(result, indent=2))
    return 1 if result["failures"] else 0


if __name__ == "__main__":
    sys.exit(main())
