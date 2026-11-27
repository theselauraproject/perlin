#!/usr/bin/env python3
"""Recover packet-specific client dispatch slots from a matching PDB and EXE.

Validates identity through an existing map_matched_pdb.py manifest. Reads the
NetEventCallback handle overload list, correlates known public handler addresses
to vtables, checks inherited slots, and requires references in the named derived
constructor. Outputs a vtable inventory and an extended matched manifest.
No target code is executed and no disassembly or target code bytes are saved.
"""
import argparse
import collections
import hashlib
import json
import re
import struct
from pathlib import Path

from analyze_minecraft_static import PE
from pdb_inspect import PDB


CLASSES = ('NetEventCallback', 'ClientNetworkHandler', 'LegacyClientNetworkHandler')


def handle_methods(pdb, class_name):
    """Read one complete LF_METHOD overload list with strict shape checks."""
    kind, fieldlist = pdb.records[pdb.classes[class_name]['fieldlist']]
    if kind != 0x1203:
        raise ValueError('Expected LF_FIELDLIST')
    matches = []
    offset = 0
    while True:
        offset = fieldlist.find(b'handle\0', offset)
        if offset < 0:
            break
        if offset >= 8:
            leaf, count, type_index = struct.unpack_from('<HHI', fieldlist, offset - 8)
            if leaf == 0x150f and pdb.records[type_index][0] == 0x1206:
                matches.append((count, type_index))
        offset += 1
    if len(matches) != 1:
        raise ValueError(f'{class_name}: expected one complete handle overload list')
    count, type_index = matches[0]
    _, record = pdb.records[type_index]
    current, rows = 0, []
    for _ in range(count):
        attr, padding, method_type = struct.unpack_from('<HHI', record, current)
        current += 8
        slot = None
        if ((attr >> 2) & 7) in (4, 6):
            slot = struct.unpack_from('<i', record, current)[0]
            current += 4
        kind, member_function = pdb.records[method_type]
        if kind != 0x1009:
            raise ValueError('Expected LF_MFUNCTION')
        arglist_index = struct.unpack_from('<I', member_function, 16)[0]
        kind, arglist = pdb.records[arglist_index]
        if kind != 0x1201:
            raise ValueError('Expected LF_ARGLIST')
        argc = struct.unpack_from('<I', arglist)[0]
        types = struct.unpack_from('<' + str(argc) + 'I', arglist, 4)
        args = [pdb.typename(x) for x in types]
        if len(args) != 2 or args[0] != 'const NetworkIdentifier &':
            raise ValueError(f'Unexpected handle signature: {args!r}')
        match = re.fullmatch(r'(?:const (\w+Packet) &|std::shared_ptr<(\w+Packet)>)', args[1])
        if not match:
            raise ValueError(f'Unexpected packet argument: {args[1]}')
        rows.append(dict(packet_name=match[1] or match[2], slot_offset=slot, arguments=args))
    if current != len(record):
        raise ValueError('Method-list trailing data was not interpreted')
    return rows


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('exe', type=Path)
    ap.add_argument('pdb', type=Path)
    ap.add_argument('--manifest', required=True, type=Path)
    ap.add_argument('--out', required=True, type=Path)
    args = ap.parse_args()
    pe, pdb = PE(args.exe), PDB(args.pdb)
    if not any(x['guid'] == pdb.guid and x['age'] == pdb.age for x in pe.metadata['codeview']):
        raise ValueError('PDB GUID/age does not match executable')
    manifest = json.loads(args.manifest.read_text())
    meta = manifest['metadata']
    if (meta['executable']['sha256'] != pe.metadata['sha256']
            or meta['pdb']['sha256'] != hashlib.sha256(pdb.b).hexdigest()
            or not meta.get('section_header_match')):
        raise ValueError('Manifest does not verify this exact executable and PDB')
    methods = {name: handle_methods(pdb, name) for name in CLASSES}
    slots = {r['packet_name']: r['slot_offset'] for r in methods['NetEventCallback']}
    if len(slots) != len(methods['NetEventCallback']) or any(v is None or v % 8 for v in slots.values()):
        raise ValueError('Non-unique or invalid virtual slot offsets')
    if not all(p['name'] in slots for p in manifest['packets']):
        raise ValueError('Active packet catalog is not covered by NetEventCallback slots')
    symbols = []
    for symbol in pdb.symbols():
        if 1 <= symbol.get('section', 0) <= len(pe.sections):
            row = dict(symbol)
            row['rva_int'] = pe.sections[row['section']-1]['rva'] + int(row['section_offset'], 16)
            symbols.append(row)
    base_symbol = [s for s in symbols if s['name'] == '??_7NetEventCallback@@6B@']
    if len(base_symbol) != 1:
        raise ValueError('Expected one named NetEventCallback vtable')
    tables = {'NetEventCallback': base_symbol[0]['rva_int']}
    section = next(s for s in pe.sections if s['name'] == '.rdata')
    data = pe.code(section['rva'], section['file_size'])
    anchors, anchor_counts = {}, {}
    for class_name in CLASSES[1:]:
        known = []
        for packet in manifest['packets']:
            for handler in packet['dispatch_handlers']:
                if handler['name'].startswith('?handle@' + class_name + '@@'):
                    known.append((packet['name'], int(handler['rva'], 16)))
        if len(known) < 2:
            raise ValueError('Insufficient public handler anchors')
        candidates = collections.Counter()
        for packet_name, handler in known:
            needle = struct.pack('<Q', pe.base + handler)
            pos = 0
            while True:
                pos = data.find(needle, pos)
                if pos < 0:
                    break
                table = section['rva'] + pos - slots[packet_name]
                if table >= section['rva'] and table % 8 == 0:
                    candidates[table] += 1
                pos += 1
        validated = []
        parent_class = CLASSES[CLASSES.index(class_name)-1]
        declared = {x['packet_name'] for x in methods[class_name]}
        for table, count in candidates.items():
            if count != len(known):
                continue
            public_match = all(struct.unpack('<Q', pe.code(table + slots[n], 8))[0] == pe.base + r for n, r in known)
            inherited_match = all(pe.code(tables[parent_class]+slot, 8) == pe.code(table+slot, 8)
                                  for name, slot in slots.items() if name not in declared)
            if public_match and inherited_match:
                validated.append(table)
        if len(validated) != 1:
            raise ValueError(f'{class_name}: expected one fully anchored vtable, got {validated!r}')
        tables[class_name] = validated[0]
        anchors[class_name] = [dict(packet_name=n, handler_rva=hex(r), slot_offset=slots[n]) for n, r in known]
        anchor_counts[class_name] = len(known)
    constructor = [s for s in symbols if s['name'].startswith('??0LegacyClientNetworkHandler@@')]
    if len(constructor) != 1:
        raise ValueError('Expected one named LegacyClientNetworkHandler constructor')
    constructor_range = pe.function_containing(constructor[0]['rva_int'])
    if not constructor_range:
        raise ValueError('Missing constructor function boundary')
    references = []
    for site, target in pe.lea_refs(constructor_range[0], constructor_range[1]):
        for class_name in CLASSES[1:]:
            if target == tables[class_name]:
                references.append(dict(class_name=class_name, instruction_rva=hex(site), vtable_rva=hex(target)))
    if set(x['class_name'] for x in references) != set(CLASSES[1:]):
        raise ValueError('Named constructor does not reference both identified vtables')
    inheritance_checks = []
    for parent, child in zip(CLASSES, CLASSES[1:]):
        declared = {x['packet_name'] for x in methods[child]}
        checked = 0
        for name, slot in slots.items():
            if name not in declared:
                if pe.code(tables[parent]+slot, 8) != pe.code(tables[child]+slot, 8):
                    raise ValueError(f'Unexpected inherited-slot difference for {child}::{name}')
                checked += 1
        inheritance_checks.append(dict(parent=parent, child=child, matching_inherited_slots=checked))
    rows = []
    handled_field = [f for f in pdb.fields(pdb.classes['Packet']['fieldlist']) if f.get('name') == 'mIsHandled']
    if len(handled_field) != 1 or handled_field[0].get('type') != 'bool' or not 0 <= handled_field[0]['offset'] < 128:
        raise ValueError('Expected a byte-addressable Packet::mIsHandled field')
    # The handle signature places the packet reference in R8 on Windows x64.
    mark_handled_pattern = b'\x41\xc6\x40' + bytes([handled_field[0]['offset']]) + b'\x01\xc3'
    for packet in manifest['packets']:
        name, slot = packet['name'], slots[packet['name']]
        targets = {cn: struct.unpack('<Q', pe.code(table+slot, 8))[0] - pe.base for cn, table in tables.items()}
        target = targets['LegacyClientNetworkHandler']
        fn = pe.function_containing(target)
        public_names = [s['name'] for s in packet['dispatch_handlers']
                        if int(s['rva'], 16) == target and
                        any(s['name'].startswith('?handle@'+cn+'@@') for cn in CLASSES)]
        ret_only = pe.code(target, 1) == b'\xc3'
        mark_handled_only = pe.code(target, len(mark_handled_pattern)) == mark_handled_pattern
        dispatch = dict(dispatch_class='LegacyClientNetworkHandler', vtable_rva=hex(tables['LegacyClientNetworkHandler']),
                        slot_offset=slot, handler_rva=hex(target), public_name=public_names[0] if public_names else None,
                        public_symbol_names=public_names,
                        verified_ret_only=ret_only, verified_mark_handled_only=mark_handled_only,
                        verified_leaf_function_range=[hex(target),hex(target+(1 if ret_only else 6))] if ret_only or mark_handled_only else None,
                        unwind_function_range=[hex(fn[0]),hex(fn[1])] if fn else None,
                        inherits_net_event_callback_target=target == targets['NetEventCallback'],
                        class_targets={k:hex(v) for k,v in targets.items()},
                        scope='This virtual dispatch entry only; alternate listeners and downstream behavior remain unresolved.')
        packet['client_dispatch'] = dispatch
        rows.append(dict(packet_id=packet['packet_id'], packet_name=name, **dispatch))
    summary = dict(active_packet_count=len(rows), net_event_handle_overloads=len(slots),
                   packets_with_verified_client_dispatch=len(rows),
                   distinct_final_client_handler_targets=len({r['handler_rva'] for r in rows}),
                   packets_with_ret_only_final_client_target=sum(r['verified_ret_only'] for r in rows),
                   packets_with_mark_handled_only_final_client_target=sum(r['verified_mark_handled_only'] for r in rows),
                   packets_with_final_client_target_public_name=sum(bool(r['public_name']) for r in rows),
                   public_anchor_counts=anchor_counts)
    result = dict(metadata=dict(executable_sha256=pe.metadata['sha256'], pdb_sha256=meta['pdb']['sha256'],
                                pdb_guid=pdb.guid,pdb_age=pdb.age,pdb_match=True,section_header_match=True,
                                method='PDB virtual slots, exact public function pointers, constructor references and inherited-slot validation',
                                constructor_symbol=constructor[0]['name'],constructor_rva=hex(constructor[0]['rva_int']),
                                constructor_vtable_references=references,
                                limitation='Maps the LegacyClientNetworkHandler virtual dispatch route; no claim that alternate listeners or full semantics were reconstructed.'),
                  summary=summary, vtables={k:hex(v) for k,v in tables.items()},
                  inherited_slot_validation=inheritance_checks, public_anchors=anchors,
                  packet_dispatch=rows)
    manifest['metadata']['client_vtable_dispatch'] = summary
    args.out.mkdir(parents=True, exist_ok=True)
    (args.out/'client_dispatch_vtables.json').write_text(json.dumps(result, indent=2)+'\n')
    (args.out/'matched_packet_manifest.json').write_text(json.dumps(manifest, indent=2)+'\n')
    print(json.dumps(summary, indent=2))


if __name__ == '__main__':
    main()
