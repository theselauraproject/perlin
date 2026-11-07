#!/usr/bin/env python3
"""Run Minecraft's model classes headlessly to regenerate numeric pose fixtures.

Requires a legally obtained, unmodified Java 26.3 client JAR and JDK 26+.
The client and its dependencies stay in the local build cache, outside Git.
"""
import argparse
import concurrent.futures
import hashlib
import json
import os
from pathlib import Path
import subprocess
import urllib.request

CLIENT_SHA1 = 'e877b6a07acd633fb3bb475002175cec036e7b87'
ROOT = Path(__file__).resolve().parents[1]


def download(url, target, sha1=None):
    if not target.exists() or (sha1 and hashlib.sha1(target.read_bytes()).hexdigest() != sha1):
        target.parent.mkdir(parents=True, exist_ok=True)
        data = urllib.request.urlopen(url, timeout=60).read()
        if sha1 and hashlib.sha1(data).hexdigest() != sha1:
            raise RuntimeError(f'Checksum mismatch: {url}')
        target.write_bytes(data)
    return target


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--jar', type=Path, required=True)
    parser.add_argument('--java-home', type=Path, required=True)
    parser.add_argument('--cache', type=Path, default=ROOT / 'build/java-oracle')
    parser.add_argument('--output', type=Path, default=ROOT / 'tests/data/java_animation_reference.txt')
    args = parser.parse_args()
    if hashlib.sha1(args.jar.read_bytes()).hexdigest() != CLIENT_SHA1:
        raise RuntimeError('The reference must be the unmodified Minecraft Java 26.3 client JAR')
    args.cache.mkdir(parents=True, exist_ok=True)
    metadata = args.cache / 'version.json'
    if not metadata.exists():
        manifest = json.load(urllib.request.urlopen('https://piston-meta.mojang.com/mc/game/version_manifest_v2.json', timeout=60))
        version = next(item for item in manifest['versions'] if item['id'] == '26.3')
        download(version['url'], metadata, version['sha1'])
    version = json.loads(metadata.read_text())
    if version['id'] != '26.3' or version['downloads']['client']['sha1'] != CLIENT_SHA1:
        raise RuntimeError('Reference library metadata does not match the client')
    libraries = [item['downloads']['artifact'] for item in version['libraries']
                 if 'artifact' in item.get('downloads', {}) and not item['name'].startswith('org.lwjgl')]
    def fetch(artifact):
        return download(artifact['url'], args.cache / 'lib' / artifact['path'], artifact['sha1'])
    with concurrent.futures.ThreadPoolExecutor(max_workers=8) as executor:
        jars = list(executor.map(fetch, libraries))
    classes = args.cache / 'classes'
    classes.mkdir(exist_ok=True)
    classpath = os.pathsep.join(str(path.resolve()) for path in [args.jar, *jars])
    source = ROOT / 'tools/animation_reference/AnimationOracle.java'
    subprocess.run([str(args.java_home / 'bin/javac'), '-cp', classpath, '-d', str(classes), str(source)], check=True)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    subprocess.run([str(args.java_home / 'bin/java'), '-Djava.awt.headless=true', '-cp', str(classes.resolve()) + os.pathsep + classpath,
                    'AnimationOracle', str(args.output.resolve()), str(args.output.resolve().with_name('java_animation_root_reference.txt'))], check=True)
    print(f'Wrote reference poses to {args.output}')


if __name__ == '__main__':
    main()
