#!/usr/bin/env python3
"""Export selected matched-PDB type/member/method facts and public symbols.

This reads metadata only. It does not execute the target or emit its code bytes.
Virtual slot offsets are emitted only when explicitly present in CodeView;
overrides need a separately verified base-class/ABI correlation.
"""
import argparse
import hashlib
import json
import re
import struct
from pathlib import Path

from analyze_minecraft_static import PE
from pdb_inspect import PDB, cstr, number


def validate_sections(pe, pdb):
    """Verify the DBI address basis before joining public symbols to PE RVAs."""
    dbi = pdb.streams[3]
    offset = 64 + sum(struct.unpack_from('<i', dbi, k)[0]
                      for k in [24, 28, 32, 36, 40, 52])
    size = struct.unpack_from('<i', dbi, 48)[0]
    if size < 12 or size % 2 or offset < 64 or offset + size > len(dbi):
        raise ValueError('Invalid or missing PDB optional debug header')
    debug = struct.unpack_from('<' + str(size // 2) + 'H', dbi, offset)
    if debug[5] == 0xffff:
        raise ValueError('Missing PDB section-header stream')
    headers = pdb.streams[debug[5]]
    if len(headers) != len(pe.sections) * 40:
        raise ValueError('PDB/executable section counts differ')
    for i, section in enumerate(pe.sections):
        values = struct.unpack_from('<8sIIIIIIHHI', headers, i * 40)
        actual = (section['name'], section['virtual_size'], section['rva'],
                  section['file_size'], section['file_offset'], section['flags'])
        expected = (values[0].rstrip(b'\0').decode('ascii'), values[1], values[2],
                    values[3], values[4], values[-1])
        if actual != expected:
            raise ValueError('PDB/executable section headers differ')


def method_type(pdb, index):
    kind, rec = pdb.records[index]
    if kind != 0x1009:
        return {'type_index': hex(index), 'unparsed_kind': hex(kind)}
    ret, owner, this, call, flags, argc, arglist, adjust = struct.unpack_from('<IIIBBHIi', rec)
    akind, arec = pdb.records[arglist]
    if akind != 0x1201:
        raise ValueError('Expected LF_ARGLIST')
    count = struct.unpack_from('<I', arec)[0]
    args = struct.unpack_from('<' + str(count) + 'I', arec, 4)
    return {'type_index': hex(index), 'return_type': pdb.typename(ret),
            'class_type': pdb.typename(owner), 'this_type': pdb.typename(this),
            'argument_types': [pdb.typename(x) for x in args],
            'argument_count': argc, 'this_adjust': adjust}


def methods(pdb, fieldlist, seen=None):
    seen = set() if seen is None else seen
    if not fieldlist or fieldlist in seen:
        return []
    seen.add(fieldlist)
    kind, rec = pdb.records[fieldlist]
    if kind != 0x1203:
        raise ValueError('Expected LF_FIELDLIST')
    result, cur = [], 0
    while cur < len(rec):
        if rec[cur] >= 0xf0:
            cur += rec[cur] & 15
            continue
        k = struct.unpack_from('<H', rec, cur)[0]
        cur += 2
        if k == 0x150d:  # instance member
            cur += 6
            _, cur = number(rec, cur)
            _, cur = cstr(rec, cur)
        elif k in (0x150e, 0x1510, 0x1512):
            cur += 6
            _, cur = cstr(rec, cur)
        elif k == 0x1502:
            cur += 2
            _, cur = number(rec, cur)
            _, cur = cstr(rec, cur)
        elif k == 0x1400:
            cur += 6
            _, cur = number(rec, cur)
        elif k in (0x1401, 0x1402):
            cur += 10
            _, cur = number(rec, cur)
            _, cur = number(rec, cur)
        elif k == 0x150f:
            count, index = struct.unpack_from('<HI', rec, cur)
            cur += 6
            name, cur = cstr(rec, cur)
            mkind, mrec = pdb.records[index]
            if mkind != 0x1206:
                raise ValueError('Expected LF_METHODLIST')
            pos = 0
            for _ in range(count):
                attr, padding, mtype = struct.unpack_from('<HHI', mrec, pos)
                pos += 8
                slot = None
                if ((attr >> 2) & 7) in (4, 6):
                    slot = struct.unpack_from('<i', mrec, pos)[0]
                    pos += 4
                result.append(dict(name=name, attribute=hex(attr),
                                   explicit_virtual_slot=slot, **method_type(pdb, mtype)))
            if pos != len(mrec):
                raise ValueError('Unexpected method-list trailing data')
        elif k == 0x1511:
            attr, index = struct.unpack_from('<HI', rec, cur)
            cur += 6
            slot = None
            if ((attr >> 2) & 7) in (4, 6):
                slot = struct.unpack_from('<i', rec, cur)[0]
                cur += 4
            name, cur = cstr(rec, cur)
            result.append(dict(name=name, attribute=hex(attr),
                               explicit_virtual_slot=slot, **method_type(pdb, index)))
        elif k == 0x1409:
            cur += 6
        elif k == 0x1404:
            index = struct.unpack_from('<I', rec, cur + 2)[0]
            cur += 6
            result.extend(methods(pdb, index, seen))
        else:
            raise ValueError(f'Unsupported field-list leaf {k:#x}')
    return result


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('exe', type=Path)
    ap.add_argument('pdb', type=Path)
    ap.add_argument('--class', dest='classes', action='append', default=[])
    ap.add_argument('--class-regex')
    ap.add_argument('--symbol-regex')
    ap.add_argument('--symbol-rva', action='append', default=[], type=lambda x: int(x, 0))
    ap.add_argument('--query', type=Path,
                    help='JSON selectors: classes, class_regex, symbol_regex, symbol_rvas; optional expected_executable_sha256')
    ap.add_argument('--out', required=True, type=Path)
    args = ap.parse_args()
    pe, pdb = PE(args.exe), PDB(args.pdb)
    if not any(x['guid'] == pdb.guid and x['age'] == pdb.age for x in pe.metadata['codeview']):
        raise ValueError('PDB GUID/age do not match executable')
    validate_sections(pe, pdb)
    if args.query:
        query = json.loads(args.query.read_text())
        allowed = {'classes', 'class_regex', 'symbol_regex', 'symbol_rvas',
                   'expected_executable_sha256'}
        if not isinstance(query, dict) or set(query) - allowed:
            raise ValueError('Unexpected reference-query fields')
        expected_hash = query.get('expected_executable_sha256')
        if expected_hash and expected_hash != pe.metadata['sha256']:
            raise ValueError('Reference query executable SHA-256 does not match input')
        args.classes.extend(query.get('classes', []))
        args.symbol_rva.extend(int(x, 0) if isinstance(x, str) else x
                               for x in query.get('symbol_rvas', []))
        for name in ('class_regex', 'symbol_regex'):
            if query.get(name):
                previous = getattr(args, name)
                setattr(args, name, '|'.join(x for x in (previous, query[name]) if x))
    selected = set(args.classes)
    if args.class_regex:
        selected.update(n for n in pdb.classes if re.search(args.class_regex, n))
    missing = selected - pdb.classes.keys()
    rows = []
    for name in sorted(selected - missing):
        row = dict(pdb.classes[name])
        row['type_index'] = hex(row['type_index'])
        row['fields'] = pdb.fields(row['fieldlist'])
        row['methods'] = methods(pdb, row['fieldlist'])
        row['fieldlist'] = hex(row['fieldlist'])
        rows.append(row)
    symbols = []
    for symbol in pdb.symbols():
        section = symbol.get('section', 0)
        if not 1 <= section <= len(pe.sections):
            continue
        rva = pe.sections[section - 1]['rva'] + int(symbol['section_offset'], 16)
        if ((args.symbol_regex and re.search(args.symbol_regex, symbol['name']))
                or rva in args.symbol_rva):
            symbols.append(dict(symbol, rva=hex(rva)))
    result = {'metadata': {'executable_sha256': pe.metadata['sha256'],
                           'pdb_sha256': hashlib.sha256(pdb.b).hexdigest(),
                           'pdb_guid': pdb.guid, 'pdb_age': pdb.age, 'pdb_match': True,
                           'section_header_match': True,
                           'scope': 'C++ metadata and symbol addresses; memory layout is not wire serialization order; virtual overrides need base-slot verification.'},
              'missing_class_names': sorted(missing), 'classes': rows, 'symbols': symbols}
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps({'classes': len(rows), 'symbols': len(symbols), 'missing': sorted(missing)}))


if __name__ == '__main__':
    main()
