#!/usr/bin/env python3
"""Map packet-related PDB public symbols only after GUID/age and section validation.

Requires analyze_minecraft_static.py and pdb_inspect.py next to this script.
Does not execute, load, or patch the target executable. Does not reconstruct
complete handlers. Records unverified behavioral status explicitly.
"""
import argparse
import hashlib
import json
import re
import struct
from pathlib import Path
from analyze_minecraft_static import PE
from pdb_inspect import PDB


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('exe', type=Path)
    ap.add_argument('pdb', type=Path)
    ap.add_argument('--catalog', type=Path, required=True)
    ap.add_argument('--out', type=Path, required=True)
    args = ap.parse_args()
    pe, pdb = PE(args.exe), PDB(args.pdb)
    if not any(x['guid'] == pdb.guid and x['age'] == pdb.age for x in pe.metadata['codeview']):
        raise ValueError('PDB GUID/age does not match executable; refusing address mapping')
    catalog = json.loads(args.catalog.read_text())
    if catalog.get('executable_sha256') != pe.metadata['sha256']:
        raise ValueError('Packet catalog executable SHA-256 does not match executable; refusing address mapping')
    dbi = pdb.streams[3]
    offset = 64 + sum(struct.unpack_from('<i', dbi, k)[0] for k in [24,28,32,36,40,52])
    dbgsize = struct.unpack_from('<i', dbi, 48)[0]
    dbg = struct.unpack_from('<' + str(dbgsize // 2) + 'H', dbi, offset)
    if len(dbg) < 6 or dbg[5] == 0xffff:
        raise ValueError('Missing PDB section-header stream; refusing unverified address mapping')
    headers = pdb.streams[dbg[5]]
    if len(headers) // 40 != len(pe.sections):
        raise ValueError('PDB/executable section counts differ')
    for i, section in enumerate(pe.sections):
        values = struct.unpack_from('<8sIIIIIIHHI', headers, 40 * i)
        actual = (section['name'], section['virtual_size'], section['rva'], section['file_size'], section['file_offset'], section['flags'])
        expected = (values[0].rstrip(b'\0').decode('ascii'), values[1], values[2], values[3], values[4], values[-1])
        if actual != expected:
            raise ValueError(f'PDB/executable section mismatch: {actual!r} != {expected!r}')
    symbols = list(pdb.symbols())
    indexed = []
    globals_found = []
    for symbol in symbols:
        if 'section' not in symbol or not 1 <= symbol['section'] <= len(pe.sections):
            continue
        if not any(t in symbol['name'] for t in ('Packet', 'NetworkProtocolVersion', 'Compression', 'InventoryStack', 'ItemStackNetManager', 'Cooldown')):
            continue
        row = dict(symbol)
        rva = pe.sections[row['section'] - 1]['rva'] + int(row['section_offset'], 16)
        row['rva'] = hex(rva)
        row['va'] = hex(pe.base + rva)
        try:
            row['file_offset'] = hex(pe.offset(rva))
        except ValueError:
            row['file_offset'] = None  # Uninitialized virtual data has no file bytes.
        section = pe.sections[row['section'] - 1]
        row['is_code_section'] = bool(section['flags'] & 0x20000000)
        if row['is_code_section']:
            fn = pe.function_containing(rva)
            row['unwind_function_range'] = [hex(fn[0]), hex(fn[1])] if fn else None
        if row['name'] == '?NetworkProtocolVersion@SharedConstants@@3HB':
            row['int32_value'] = struct.unpack_from('<i', pe.data, pe.offset(rva))[0]
            globals_found.append(row)
        indexed.append(row)
    rows = []
    for packet in catalog['packets']:
        name = packet['name']
        pattern = re.compile(r'(?:@|[UV])' + re.escape(name) + r'(?:Payload|VariantPayload)?@@')
        relevant = [s for s in indexed if pattern.search(s['name']) and s['is_code_section']]
        handlers = [s for s in relevant if s['name'].startswith('?handle@')]
        serializers = [s for s in relevant if any(s['name'].startswith('?' + method + '@' + name + '@@') for method in ('read', '_read', 'write', 'writeWithSerializationMode'))]
        validators = [s for s in relevant if s['name'].startswith('?isValid@' + name + '@@')]
        others = [s for s in relevant if s not in handlers and s not in serializers and s not in validators]
        layouts = []
        for label in (name, name + 'Payload', name + 'VariantPayload'):
            if label in pdb.classes:
                layout = dict(pdb.classes[label])
                layout['fields'] = pdb.fields(layout['fieldlist'])
                layout['type_index'] = hex(layout['type_index'])
                layout['fieldlist'] = hex(layout['fieldlist'])
                layouts.append(layout)
        rows.append(dict(packet, dispatch_handlers=handlers, serializers=serializers,
                         validators=validators, related_entrypoints=others, memory_layouts=layouts,
                         behavioral_parity='unverified: entrypoint mapping does not reconstruct all handling paths',
                         wire_layout='unverified: C++ memory offsets are not serialization order'))
    meta = dict(executable=pe.metadata,
                pdb=dict(filename=args.pdb.name, sha256=hashlib.sha256(pdb.b).hexdigest(), guid=pdb.guid, age=pdb.age),
                pdb_match=True, section_header_match=True, globals=globals_found,
                packet_count=len(rows), packets_with_dispatch_symbols=sum(bool(x['dispatch_handlers']) for x in rows),
                packets_with_serializer_symbols=sum(bool(x['serializers']) for x in rows),
                packets_with_validator_symbols=sum(bool(x['validators']) for x in rows),
                note='No dynamic validation performed; no claim of complete packet semantics or anticheat compatibility.')
    args.out.mkdir(parents=True, exist_ok=True)
    (args.out/'matched_packet_manifest.json').write_text(json.dumps(dict(metadata=meta, packets=rows), indent=2)+'\n')
    (args.out/'matched_symbol_index.json').write_text(json.dumps(dict(metadata=meta, symbols=indexed), indent=2)+'\n')
    print(json.dumps({k:v for k,v in meta.items() if k != 'executable'}, indent=2))

if __name__ == '__main__':
    main()
