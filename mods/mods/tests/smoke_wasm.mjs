import fs from 'node:fs';
import { WASI } from 'node:wasi';

const file = process.argv[2];
const wasi = new WASI({ version: 'preview1', args: [], env: {} });
const module = await WebAssembly.compile(fs.readFileSync(file));
const imports = WebAssembly.Module.imports(module).map((i) => `${i.module}.${i.name}`);
const exports = WebAssembly.Module.exports(module).map((e) => e.name);
for (const need of ['perlin_alloc', 'perlin_free', 'perlin_event', 'perlin_subscriptions', 'memory'])
  if (!exports.includes(need)) { console.error(`missing export ${need}`); process.exit(1); }
const foreign = imports.filter((i) => !i.startsWith('wasi_snapshot_preview1.') && i !== 'perlin.call');
if (foreign.length) { console.error(`unexpected imports: ${foreign.join(', ')}`); process.exit(1); }

let calls = 0;
const instance = await WebAssembly.instantiate(module, {
  wasi_snapshot_preview1: wasi.wasiImport,
  perlin: { call: () => { calls++; return -4; } },
});
wasi.initialize(instance);
const e = instance.exports;
const ev = (id, bytes = new Uint8Array(0)) => {
  const ptr = bytes.length ? e.perlin_alloc(bytes.length) : 0;
  if (bytes.length) new Uint8Array(e.memory.buffer, ptr, bytes.length).set(bytes);
  const rc = e.perlin_event(id, ptr, bytes.length);
  if (bytes.length) e.perlin_free(ptr);
  return rc;
};
ev(1);
const mask = e.perlin_subscriptions();
const frame = new DataView(new ArrayBuffer(16));
frame.setFloat32(0, 0.016, true); frame.setFloat32(4, 60, true); frame.setInt32(8, 320, true); frame.setInt32(12, 180, true);
for (let i = 0; i < 30; i++) ev(4, new Uint8Array(frame.buffer));
console.log(`${file}: ok, ${imports.length} imports, subscriptions 0x${mask.toString(16)}, ${calls} host calls, ${fs.statSync(file).size} bytes`);
