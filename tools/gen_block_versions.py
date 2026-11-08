import argparse
import json
import subprocess
import sys

def load(repo, tag):
    text = subprocess.run(["git", "-C", repo, "show", f"{tag}:metadata/vanilladata_modules/mojang-blocks.json"],
                          check=True, capture_output=True, text=True).stdout
    data = json.loads(text)
    props = {p["name"]: [v["value"] for v in p["values"]] for p in data["block_properties"]}
    blocks = {b["name"]: [p["name"] for p in b.get("properties", [])] for b in data["data_items"]}
    return blocks, props

def delta(old, new):
    old_blocks, _ = old
    new_blocks, new_props = new
    removed = sorted(set(new_blocks) - set(old_blocks))
    added = sorted(set(old_blocks) - set(new_blocks))
    pinned = {}
    for name, keys in new_blocks.items():
        if name not in old_blocks:
            continue
        extra = [k for k in keys if k not in old_blocks[name]]
        if extra:
            pinned[name] = {k: new_props[k][0] for k in extra}
        missing = [k for k in old_blocks[name] if k not in keys]
        if missing:
            sys.exit(f"{name}: the older version has properties {missing} the current table lacks")
    if added:
        sys.exit(f"blocks only the older version has: {added}")
    return removed, pinned

def main():
    ap = argparse.ArgumentParser(description="block palette deltas for older protocols, from Mojang/bedrock-samples tags")
    ap.add_argument("--samples", required=True, help="a bedrock-samples clone with the tags fetched")
    ap.add_argument("--current", required=True, help="tag that block_states.json was built from, e.g. v1.26.60.5")
    ap.add_argument("--current-protocol", type=int, required=True)
    ap.add_argument("--older", nargs="+", required=True, help="protocol=tag, newest first, e.g. 2193=v1.26.50.4")
    ap.add_argument("--out", default="resources/block_versions.json")
    args = ap.parse_args()
    newer_tag, newer_protocol = args.current, args.current_protocol
    newer = load(args.samples, newer_tag)
    entries = []
    for spec in args.older:
        protocol, tag = spec.split("=", 1)
        older = load(args.samples, tag)
        removed, pinned = delta(older, newer)
        entries.append({"before": newer_protocol, "protocol": int(protocol), "version": tag.lstrip("v"),
                        "removed": removed, "pinned": pinned})
        newer, newer_protocol = older, int(protocol)
    with open(args.out, "w", encoding="utf-8", newline="\n") as f:
        json.dump({"current": args.current_protocol, "versions": entries}, f, indent=1, sort_keys=True)
        f.write("\n")
    for e in entries:
        print(f"protocol {e['protocol']} ({e['version']}): {len(e['removed'])} blocks removed, {len(e['pinned'])} pinned")

if __name__ == "__main__":
    main()
