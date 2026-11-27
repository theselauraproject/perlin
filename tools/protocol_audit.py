#!/usr/bin/env python3
"""Inventory protocol definitions and source handlers without claiming behavioral parity.

All inputs are local. Optional Mojang metadata and a statically recovered executable
packet catalog add independent ID/name evidence; neither proves handler equivalence.
"""

from __future__ import annotations

import argparse
import collections
import copy
import hashlib
import importlib.util
import json
import re
import subprocess
from pathlib import Path


def load_generator(root: Path):
    spec = importlib.util.spec_from_file_location("perlin_protogen", root / "tools/protogen/protogen.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def mask_cpp(text: str) -> str:
    # Preserve line/character offsets, but do not count examples in comments or strings.
    pattern = re.compile(r'//[^\n]*|/\*[\s\S]*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'')
    return pattern.sub(lambda match: "".join("\n" if c == "\n" else " " for c in match[0]), text)


def line_at(text: str, offset: int) -> int:
    return text.count("\n", 0, offset) + 1


def closing_brace(code: str, start: int) -> int:
    depth = 0
    for i in range(start, len(code)):
        if code[i] == "{":
            depth += 1
        elif code[i] == "}":
            depth -= 1
            if depth == 0:
                return i
    return start


def source_evidence(root: Path, classes: set[str]) -> dict:
    result = {name: {"handlers": [], "references": []} for name in classes}
    registration = re.compile(r'\b(OnPartial|On)\s*<\s*(?:proto::)?(\w+Packet)\s*>')
    functions = re.compile(r'\b(?:\w+::)+(\w+)\s*\([^;{}]*\)\s*\{')
    packet_ref = re.compile(r'\b\w+Packet\b')
    for path in sorted((root / "src").rglob("*.cpp")):
        if "selftest" in path.parts:
            continue
        raw = path.read_text(encoding="utf-8")
        code = mask_cpp(raw)
        relative = path.relative_to(root).as_posix()
        function_sites = list(functions.finditer(code))
        seen_refs = set()
        for match in packet_ref.finditer(code):
            name = match[0]
            if name not in classes:
                continue
            line = line_at(code, match.start())
            if (name, line) not in seen_refs:
                result[name]["references"].append({"path": relative, "line": line})
                seen_refs.add((name, line))
        for match in registration.finditer(code):
            name = match[2]
            if name not in classes:
                continue
            begin = code.find("{", match.end())
            if begin < 0:
                continue
            end = closing_brace(code, begin)
            body = code[begin + 1:end]
            function = next((m[1] for m in reversed(function_sites) if m.start() < match.start()), None)
            receiver = re.search(r'(\w+)\s*\.\s*$', code[:match.start()])
            scope = "recorded_client_replay" if receiver and receiver[1] == "replayClientDispatcher" else "live_session"
            kind = "partial" if match[1] == "OnPartial" else "typed"
            if not body.strip():
                kind = "no_op"
            result[name]["handlers"].append({
                "path": relative,
                "line": line_at(code, match.start()),
                "end_line": line_at(code, end),
                "registration_function": function,
                "scope": scope,
                "kind": kind,
                "send_calls_in_handler": sorted(set(re.findall(r'\b(Send\w*)\s*\(', body))),
            })
        # Perlin explicitly installs its no-op handlers through this local pack expansion.
        for match in re.finditer(r'accept\.template\s+operator\(\)\s*<([^>]+)>\s*\(', code):
            for name in re.findall(r'\b\w+Packet\b', match[1]):
                if name in classes:
                    result[name]["handlers"].append({
                        "path": relative, "line": line_at(code, match.start()),
                        "end_line": line_at(code, match.end()),
                        "registration_function": "RegisterIgnoredHandlers", "kind": "no_op",
                        "scope": "live_session",
                        "send_calls_in_handler": [],
                    })
    return result


def receive_status(handlers: list[dict]) -> str:
    handlers = [h for h in handlers if h.get("scope", "live_session") == "live_session"]
    if any(h["kind"] == "typed" for h in handlers):
        return "typed_handler_present"
    if any(h["kind"] == "partial" for h in handlers):
        return "partial_handler_present"
    return "explicit_no_op" if handlers else "no_receive_handler_found"


def official_catalog(directory: Path | None) -> dict[int, dict]:
    result = {}
    if directory is None:
        return result
    for path in sorted(directory.glob("*.json")):
        doc = json.loads(path.read_text(encoding="utf-8"))
        packet_id = doc.get("$metaProperties", {}).get("[cereal:packet]")
        if packet_id is not None:
            if packet_id in result:
                raise ValueError(f"Duplicate official packet ID {packet_id}")
            result[packet_id] = {
                "id": packet_id, "name": doc["title"],
                "protocol": doc.get("x-protocol-version"),
                "game_version": doc.get("x-minecraft-version"),
                "metadata_file": path.name,
            }
    if not result:
        raise ValueError("Official metadata directory contains no packet entries")
    return result


def revision(root: Path) -> str | None:
    run = subprocess.run(["git", "-C", str(root), "rev-parse", "HEAD"], capture_output=True, text=True)
    return run.stdout.strip() if run.returncode == 0 else None


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def reference_handlers(path: Path | None, binary: dict[int, dict],
                       binary_executable_sha256: str | None = None) -> tuple[dict, dict | None]:
    if path is None:
        return {}, None
    if not binary:
        raise ValueError("A reference handler manifest requires an executable packet catalog")
    manifest = json.loads(path.read_text(encoding="utf-8"))
    metadata = manifest["metadata"]
    if metadata.get("pdb_match") is not True or metadata.get("section_header_match") is not True:
        raise ValueError("Reference handler manifest has no verified executable/PDB identity")
    if not binary_executable_sha256:
        raise ValueError("Reference handler mapping requires the catalog's executable SHA-256")
    if binary_executable_sha256 != metadata["executable"]["sha256"]:
        raise ValueError("Reference handler manifest/catalog executable SHA-256 differs")
    packets = {}
    for packet in manifest["packets"]:
        packet_id = packet["packet_id"]
        if packet_id in packets:
            raise ValueError(f"Duplicate reference handler packet ID {packet_id}")
        packets[packet_id] = packet
    if set(packets) != set(binary):
        raise ValueError("Reference handler manifest and executable catalog cover different packets")
    for packet_id, packet in packets.items():
        for key in ("name", "get_name_rva", "get_id_rva", "vtable_rva"):
            if packet.get(key) is None or packet[key] != binary[packet_id].get(key):
                raise ValueError(f"Reference handler manifest/catalog identity differs at ID {packet_id}: {key}")
    return {packet_id: packet.get("client_dispatch") for packet_id, packet in packets.items()}, {
        "executable_sha256": metadata["executable"]["sha256"],
        "pdb_sha256": metadata["pdb"]["sha256"],
        "file_versions": metadata["executable"].get("file_versions", []),
        "scope": "The matched client's virtual dispatch entries only; other listeners and semantics remain unresolved.",
    }


def make_inventory(root: Path, spec_path: Path, overrides_path: Path,
                   official_dir: Path | None = None, binary_path: Path | None = None,
                   reference_path: Path | None = None) -> dict:
    generator = load_generator(root)
    spec = json.loads(spec_path.read_text(encoding="utf-8"))
    overrides = json.loads(overrides_path.read_text(encoding="utf-8"))
    if "legacy" in spec_path.parts:
        overrides = generator.legacy_overrides(spec, overrides)
    generated = generator.generate(copy.deepcopy(spec), copy.deepcopy(overrides))
    packet_types = {packet_id: type_name for packet_id, _, type_name in generator.build(generated.gen)}
    official = official_catalog(official_dir)
    binary = {}
    binary_executable_sha256 = None
    if binary_path:
        entries = json.loads(binary_path.read_text(encoding="utf-8"))
        if isinstance(entries, dict):
            binary_executable_sha256 = entries.get("executable_sha256")
            entries = entries["packets"]
        for entry in entries:
            packet_id = entry.get("id", entry.get("packet_id"))
            if not isinstance(packet_id, int):
                raise ValueError("Executable packet catalog entry has no numeric ID")
            if packet_id in binary:
                raise ValueError(f"Duplicate executable packet ID {packet_id}")
            binary[packet_id] = {**entry, "id": packet_id}
    dispatch, reference_identity = reference_handlers(reference_path, binary, binary_executable_sha256)
    evidence = source_evidence(root, {row[2] for row in generated.registry})
    records = []
    ids = set()
    for packet_id, name, cpp_class, codec, reason in generated.registry:
        if packet_id in ids:
            raise ValueError(f"Duplicate Perlin packet ID {packet_id}")
        ids.add(packet_id)
        source = evidence[cpp_class]
        doc = official.get(packet_id)
        executable = binary.get(packet_id)
        active = None if not binary else executable is not None
        schema_type = packet_types.get(packet_id)
        schema_definition = generated.gen.types.get(schema_type)
        records.append({
            "id": packet_id,
            "schema_name": name,
            "cpp_class": cpp_class,
            "schema_type": schema_type,
            "schema_definition": schema_definition,
            "wire_equivalence": "not_established: field definitions are schema-derived",
            "codec": codec,
            "codec_limitation": reason,
            "present_in_executable_catalog": active,
            "official": doc,
            "executable": executable,
            "reference_client_dispatch": dispatch.get(packet_id),
            "catalog_name_match": None if not doc or not executable else doc["name"] == executable["name"],
            "receive_status": receive_status(source["handlers"]),
            "handlers": source["handlers"],
            "source_references": source["references"],
            "behavioral_equivalence": "not_established",
        })
    missing = sorted((set(official) | set(binary)) - ids)
    counts = collections.Counter(r["receive_status"] for r in records)
    active_counts = collections.Counter(r["receive_status"] for r in records if r["present_in_executable_catalog"])
    return {
        "format_version": 1,
        "scope": "Static packet catalog, codec generation mode, and source registration inventory",
        "limitations": [
            "Handler presence, codec generation, and ID/name matches do not establish behavioral equivalence.",
            "No receive handler may be correct for a serverbound-only packet; direction is not inferred.",
            "Source inspection records direct On/OnPartial registrations and Perlin's explicit no-op pack; recorded-client replay callbacks are separate from live receive handling.",
            "Response calls are textual call sites, not proof that a reply is correct or that all control flow was analyzed.",
            "Legacy schema inventory uses current source handler class names; version bridges still require separate testing.",
            "A reference return-only callback describes one dispatch route, not every listener or packet-related effect.",
        ],
        "perlin_base_revision": revision(root),
        "schema": spec_path.relative_to(root).as_posix(),
        "schema_sha256": digest(spec_path),
        "overrides_sha256": digest(overrides_path),
        "official_revision": revision(official_dir.parent) if official_dir else None,
        "binary_catalog_sha256": digest(binary_path) if binary_path else None,
        "binary_executable_sha256": binary_executable_sha256,
        "reference_handler_manifest_sha256": digest(reference_path) if reference_path else None,
        "reference_handler_identity": reference_identity,
        "summary": {
            "perlin_packet_definitions": len(records),
            "official_packet_definitions": len(official) if official_dir else None,
            "executable_packet_classes": len(binary) if binary_path else None,
            "missing_packet_ids": missing,
            "catalog_name_mismatches": [r["id"] for r in records if r["catalog_name_match"] is False],
            "codec_modes": dict(collections.Counter(r["codec"] for r in records)),
            "receive_status_all_definitions": dict(counts),
            "receive_status_executable_packets": dict(active_counts) if binary_path else None,
            "reference_client_dispatch_entries": sum(bool(entry) for entry in dispatch.values()),
            "reference_return_only_entries": sum(bool(entry and entry.get("verified_ret_only")) for entry in dispatch.values()),
            "reference_mark_handled_only_entries": sum(bool(entry and entry.get("verified_mark_handled_only")) for entry in dispatch.values()),
            "behavioral_equivalence_established": 0,
        },
        "packets": records,
    }


def markdown_inventory(data: dict) -> str:
    lines = ["# Packet source inventory", "",
             "This is an exhaustive inventory of the selected schema, not a certificate of complete behavior.", "",
             "`no receive handler` includes packets sent only by the client; it is not automatically a defect.",
             "`typed` or `partial` means source code is registered. `no-op` means the registered callback does no work.",
             "Every packet's behavior remains unproven as a whole; confirmed fixes are documented separately.", "",
             f"Schema: `{data['schema']}`. Base revision: `{data['perlin_base_revision']}`.", "",
             "The reference dispatch column is a matched-build virtual entrypoint, not proof of full behavior.", "",
             "| ID | Perlin name | Executable name | Active in executable | Codec | Receive handler | Reference client dispatch | Source |",
             "| ---: | --- | --- | --- | --- | --- | --- | --- |"]
    labels = {"typed_handler_present": "typed", "partial_handler_present": "partial",
              "explicit_no_op": "no-op", "no_receive_handler_found": "no receive handler"}
    for record in data["packets"]:
        executable = record["executable"] or {}
        active = record["present_in_executable_catalog"]
        status = "not checked" if active is None else "yes" if active else "no"
        dispatch = record["reference_client_dispatch"]
        target = "—"
        if dispatch:
            target = f"`{dispatch['handler_rva']}`"
            if dispatch.get("verified_ret_only"):
                target += " (return-only)"
            elif dispatch.get("verified_mark_handled_only"):
                target += " (mark handled only)"
        locations = "; ".join(f"`{h['path']}:{h['line']}`" for h in record["handlers"] if h["scope"] == "live_session")
        lines.append(f"| {record['id']} | `{record['schema_name']}` | {executable.get('name', '—')} | "
                     f"{status} | {record['codec']} | {labels[record['receive_status']]} | {target} | {locations or '—'} |")
    return "\n".join(lines) + "\n"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--spec", type=Path, default=Path("protocol/bedrock-1.26.60.json"))
    parser.add_argument("--overrides", type=Path, default=Path("protocol/overrides.json"))
    parser.add_argument("--official-dir", type=Path)
    parser.add_argument("--binary-inventory", type=Path)
    parser.add_argument("--reference-handler-manifest", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--markdown", type=Path)
    args = parser.parse_args()
    root = args.root.resolve()
    data = make_inventory(root, root / args.spec, root / args.overrides,
                          args.official_dir, args.binary_inventory, args.reference_handler_manifest)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")
    if args.markdown:
        args.markdown.parent.mkdir(parents=True, exist_ok=True)
        args.markdown.write_text(markdown_inventory(data), encoding="utf-8")
    print(json.dumps(data["summary"], indent=2))
    return 1 if data["summary"]["missing_packet_ids"] or data["summary"]["catalog_name_mismatches"] else 0


if __name__ == "__main__":
    raise SystemExit(main())
