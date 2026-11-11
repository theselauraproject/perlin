import argparse
import json
import os
import subprocess
import sys

CORE_PREFIXES = (
    "resource_pack/textures/",
    "metadata/vanilladata_modules/",
    "behavior_pack/blocks/",
    "behavior_pack/biomes/",
    "resource_pack/models/",
    "resource_pack/ui/",
    "resource_pack/font/",
    "resource_pack/attachables/",
    "resource_pack/animations/",
    "resource_pack/animation_controllers/",
    "resource_pack/render_controllers/",
    "resource_pack/entity/",
    "behavior_pack/entities/",
)

CORE_FILES = {
    "resource_pack/blocks.json",
    "resource_pack/biomes_client.json",
    "resource_pack/manifest.json",
    "behavior_pack/manifest.json",
    "resource_pack/texts/en_US.lang",
    "resource_pack/texts/en_GB.lang",
    "resource_pack/texts/languages.json",
    "resource_pack/texts/language_names.json",
}

SOUND_PREFIXES = ("resource_pack/sounds/",)
SOUND_FILES = {"resource_pack/sounds.json"}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--repo", required=True)
    parser.add_argument("--repository", default="Mojang/bedrock-samples")
    parser.add_argument("--out", required=True)
    args = parser.parse_args()

    names = subprocess.check_output(["git", "-C", args.repo, "ls-tree", "-r", "--name-only", "HEAD"], text=True).splitlines()
    ref = subprocess.check_output(["git", "-C", args.repo, "rev-parse", "HEAD"], text=True).strip()

    core = []
    sounds = []
    for name in names:
        if name in CORE_FILES or name.startswith(CORE_PREFIXES):
            core.append(name)
        elif name in SOUND_FILES or name.startswith(SOUND_PREFIXES):
            sounds.append(name)

    if not core:
        sys.exit("manifest is empty")
    os.makedirs(os.path.dirname(os.path.abspath(args.out)), exist_ok=True)
    with open(args.out, "w") as f:
        json.dump({"repository": args.repository, "ref": ref, "files": {"core": core, "sounds": sounds}}, f, separators=(",", ":"))
    print(f"{len(core)} core files, {len(sounds)} sound files at {ref}")


if __name__ == "__main__":
    main()
