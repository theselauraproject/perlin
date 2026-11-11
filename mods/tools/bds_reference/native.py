"""Prepare verified BDS pages for optional native x86-64 oracle execution.

The executable image is written only under the requested build directory.
No Unicorn dependency is needed for this independent check.
"""

import argparse
import hashlib
import json
import struct
from pathlib import Path

INPUT_SHA256 = "f35469677a9d0db236e8559e9e72c4d6532230edf54497e88677eceff4cde539"


def export_pages(data, output):
    if data["input_sha256"] != INPUT_SHA256 or data["input_name"] != "Windows Server 1.26.10":
        raise ValueError("Wrong BDS reference")
    pages = {}
    for function in data['functions'].values():
        for address, raw in function['pages'].items():
            address = int(address, 16)
            value = bytes.fromhex(raw)
            if len(value) != 4096 or address in pages and pages[address] != value:
                raise ValueError('Inconsistent exported pages')
            pages[address] = value
    for function in data['functions'].values():
        digest = hashlib.sha256()
        for start, end in function['chunks']:
            digest.update(bytes(pages[address & ~4095][address & 4095] for address in range(start, end)))
        if digest.hexdigest() != function['code_sha256']:
            raise ValueError('Code hash mismatch')
    with output.open('wb') as stream:
        stream.write(struct.pack('<II', 0x53444250, len(pages)))
        for address, value in sorted(pages.items()):
            stream.write(struct.pack('<Q', address))
            stream.write(value)


def compare_fixture(emulated, native):
    """Reject changed inputs/finite results; count the known NaN payload issue."""
    old = [line.split() for line in emulated.splitlines() if line and not line.startswith('#')]
    new = [line.split() for line in native.splitlines() if line and not line.startswith('#')]
    if len(old) != len(new):
        raise ValueError('Native oracle returned a different fixture length')
    differences = 0
    for before, after in zip(old, new):
        kind = before[0]
        results = {'S': 2, 'C': 2, 'M': 10}[kind]
        if len(before) != len(after) or before[:results] != after[:results]:
            raise ValueError('Native oracle changed fixture inputs')
        for left, right in zip(before[results:], after[results:]):
            if left != right:
                if not ((int(left, 16) & 0x7FFFFFFF) > 0x7F800000 and
                        (int(right, 16) & 0x7FFFFFFF) > 0x7F800000):
                    raise ValueError('Native and emulated BDS disagree on a non-NaN result')
                differences += 1
    return differences


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--export', required=True, type=Path)
    parser.add_argument('--out', required=True, type=Path)
    args = parser.parse_args()
    export_pages(json.loads(args.export.read_text()), args.out)
