import hashlib, json, os, shutil, subprocess, sys, tempfile

here = os.path.dirname(os.path.abspath(__file__))
exe, golden_path = sys.argv[1], sys.argv[2]
update = len(sys.argv) > 3 and sys.argv[3] == '--update'
resources = os.path.join(here, '..', 'resources')
seeds = [(1, False), (2, False), (3, False), (4, True), (5, True), (6, False), (7, True), (9, True), (13, False), (45, True)]


def run(seed, pack, work):
    root = os.path.join(work, 's%d' % seed)
    subprocess.check_call([sys.executable, '-I', os.path.join(here, 'block_library_fixtures.py'), str(seed), root])
    data = os.path.join(root, 'data')
    os.makedirs(data)
    env = dict(os.environ, PERLIN_RESOURCES=resources, PERLIN_DATA_DIR=data)
    args = [exe, root, os.path.join(root, 'perlin')] + (['pack'] if pack else [])
    out = subprocess.run(args, env=env, check=True, capture_output=True, text=True).stdout
    kept = [line for line in out.splitlines() if not line.startswith('[')]
    digest = hashlib.sha256('\n'.join(kept).encode())
    with open(os.path.join(data, 'blocks.log'), 'rb') as f:
        digest.update(f.read())
    for name in ('blocks.json', 'shapes.json'):
        path = os.path.join(root, 'perlin', name)
        digest.update(open(path, 'rb').read() if os.path.exists(path) else b'-')
    return digest.hexdigest()


with tempfile.TemporaryDirectory() as work:
    got = {'%d%s' % (s, '+pack' if p else ''): run(s, p, work) for s, p in seeds}
if update:
    json.dump(got, open(golden_path, 'w'), indent=1, sort_keys=True)
    print('updated', golden_path)
    sys.exit(0)
want = json.load(open(golden_path))
bad = [k for k in got if want.get(k) != got[k]]
for k in bad:
    print('MISMATCH', k)
print('block_library_golden', 'FAILED' if bad else 'passed')
sys.exit(1 if bad else 0)
