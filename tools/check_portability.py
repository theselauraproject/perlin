#!/usr/bin/env python3
"""Rejects standard library features that some shipped platforms do not have.

The macOS, iOS, Android, FreeBSD and web builds use libc++ (Apple clang, the NDK, emscripten),
which lags libstdc++ and MSVC on parts of C++20/23. A Linux or Windows build compiles them fine, so
this check stops them before they break the other platforms.

usage: check_portability.py <source root>
"""
import os
import re
import sys

BANNED = [
    (r"\bclock_cast\b", "std::chrono::clock_cast is missing from libc++; convert through now() on both clocks"),
    (r"\b(current_zone|zoned_time|get_tzdb|locate_zone|tzdb)\b", "chrono time zones are missing from libc++"),
    (r"\bchrono::(parse|from_stream)\b", "chrono parsing is missing from libc++"),
    (r"#\s*include\s*<stacktrace>|\bstd::stacktrace\b", "std::stacktrace is missing from libc++"),
    (r"#\s*include\s*<execution>|\bstd::execution::", "parallel algorithms are missing from libc++"),
    (r"#\s*include\s*<syncstream>|\bosyncstream\b", "<syncstream> is experimental in libc++"),
    (r"\bstd::jthread\b|\bstd::stop_token\b|\bstd::stop_source\b", "std::jthread is experimental in older libc++"),
    (r"\bmove_only_function\b", "std::move_only_function is missing from libc++"),
    (r"#\s*include\s*<generator>|\bstd::generator\b", "std::generator is missing from libc++"),
    (r"#\s*include\s*<flat_(map|set)>|\bstd::flat_(map|set)\b", "std::flat_map is missing from older libc++"),
    (r"\bstd::(views|ranges::views)::(enumerate|chunk_by|cartesian_product|concat)\b",
     "this ranges view is missing from older libc++"),
]
EXTENSIONS = (".cpp", ".hpp", ".h", ".cc", ".mm", ".inl")
SKIP_DIRS = {"build", ".git", "_deps", ".cpm-cache", "third_party", "external"}


def strip_comments(text):
    text = re.sub(r"/\*.*?\*/", lambda m: "\n" * m.group(0).count("\n"), text, flags=re.S)
    return re.sub(r"//[^\n]*", "", text)


def main():
    root = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else ".")
    patterns = [(re.compile(p), why) for p, why in BANNED]
    problems = []
    for top in ("src", "tests", "tools"):
        for dirpath, dirnames, filenames in os.walk(os.path.join(root, top)):
            dirnames[:] = [d for d in dirnames if d not in SKIP_DIRS]
            for name in filenames:
                if not name.endswith(EXTENSIONS):
                    continue
                path = os.path.join(dirpath, name)
                try:
                    with open(path, encoding="utf-8", errors="replace") as f:
                        lines = strip_comments(f.read()).split("\n")
                except OSError:
                    continue
                for number, line in enumerate(lines, 1):
                    for pattern, why in patterns:
                        if pattern.search(line):
                            problems.append(f"{os.path.relpath(path, root)}:{number}: {why}")
    for p in problems:
        print(p)
    if problems:
        print(f"{len(problems)} non-portable standard library use(s)")
        return 1
    print("portable")
    return 0


if __name__ == "__main__":
    sys.exit(main())
