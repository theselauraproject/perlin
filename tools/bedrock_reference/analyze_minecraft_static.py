#!/usr/bin/env python3
"""Read-only PE packet-catalog inspection; never loads or executes the image.

Recovers evidence addresses, not packet behavior. The getName/getId recognition is
specific to observed Windows x64 compiler output. Unrecognized functions are not
silently counted as equivalent. Optional comparisons use numeric packet IDs.
"""
from __future__ import annotations
import argparse
import bisect
import hashlib
import json
import re
import struct
import uuid
from pathlib import Path


class PE:
    def __init__(self, path: Path):
        self.path = path
        self.data = path.read_bytes()
        b = self.data
        if b[:2] != b'MZ':
            raise ValueError('Input is not a DOS/PE image')
        self.pe_offset = struct.unpack_from('<I', b, 0x3c)[0]
        if b[self.pe_offset:self.pe_offset + 4] != b'PE\0\0':
            raise ValueError('Missing PE signature')
        machine, count, timestamp, symptr, symcount, optsize, characteristics = struct.unpack_from('<HHIIIHH', b, self.pe_offset + 4)
        opt = self.pe_offset + 24
        if struct.unpack_from('<H', b, opt)[0] != 0x20b or machine != 0x8664:
            raise ValueError('This recognizer supports PE32+ x86-64 images only')
        self.base = struct.unpack_from('<Q', b, opt + 24)[0]
        self.directories = [struct.unpack_from('<II', b, opt + 112 + i * 8) for i in range(16)]
        self.sections = []
        for i in range(count):
            fields = struct.unpack_from('<8sIIIIIIHHI', b, opt + optsize + i * 40)
            name, vsize, rva, rawsize, offset, _, _, _, _, flags = fields
            if offset + rawsize > len(b):
                raise ValueError('Section exceeds input size')
            self.sections.append(dict(name=name.rstrip(b'\0').decode('ascii'), rva=rva,
                                      virtual_size=vsize, file_offset=offset, file_size=rawsize,
                                      flags=flags))
        self.metadata = dict(filename=path.name, size_bytes=len(b), sha256=hashlib.sha256(b).hexdigest(),
                             architecture='x86-64', format='PE32+', image_base=hex(self.base),
                             linker_timestamp_raw=timestamp, coff_symbol_table_offset=symptr,
                             coff_symbol_count=symcount, sections=self.sections,
                             authenticode_directory_present=bool(self.directories[4][1]),
                             authenticode_signature_verified=False)
        self.metadata['codeview'] = self.codeview()
        self.metadata['file_versions'] = self.versions()
        self.runtime_functions = None

    def section_for_rva(self, rva):
        return next((s for s in self.sections if s['rva'] <= rva < s['rva'] + s['file_size']), None)

    def offset(self, rva):
        s = self.section_for_rva(rva)
        if s is None:
            raise ValueError(f'RVA {rva:#x} has no file bytes')
        return s['file_offset'] + rva - s['rva']

    def code(self, rva, n):
        o = self.offset(rva)
        return self.data[o:o + n]

    def cstring(self, rva, limit=256):
        o = self.offset(rva)
        end = self.data.find(b'\0', o, o + limit)
        if end < 0:
            raise ValueError('Missing short C string terminator')
        return self.data[o:end].decode('ascii')

    def codeview(self):
        rva, size = self.directories[6]
        if not rva:
            return []
        out = []
        at = self.offset(rva)
        for i in range(size // 28):
            _, stamp, major, minor, kind, datasize, datarva, dataoff = struct.unpack_from('<IIHHIIII', self.data, at + i * 28)
            if kind == 2 and self.data[dataoff:dataoff + 4] == b'RSDS':
                guid = str(uuid.UUID(bytes_le=self.data[dataoff + 4:dataoff + 20]))
                age = struct.unpack_from('<I', self.data, dataoff + 20)[0]
                path = self.data[dataoff + 24:dataoff + datasize].split(b'\0', 1)[0].decode('utf-8', 'replace')
                out.append(dict(guid=guid, age=age, pdb_path=path, file_offset=hex(dataoff)))
        return out

    def versions(self):
        resource = next((s for s in self.sections if s['name'] == '.rsrc'), None)
        if resource is None:
            return []
        start = resource['file_offset']
        region = self.data[start:start + resource['file_size']]
        out = []
        for m in re.finditer(re.escape(struct.pack('<I', 0xfeef04bd)), region):
            vals = struct.unpack_from('<13I', region, m.start())
            if vals[1] != 0x10000:
                continue
            def version(hi, lo):
                return '.'.join(map(str, (hi >> 16, hi & 0xffff, lo >> 16, lo & 0xffff)))
            out.append(dict(file_version=version(vals[2], vals[3]),
                            product_version=version(vals[4], vals[5]),
                            fixed_file_info_offset=hex(start + m.start())))
        return out

    def function_containing(self, rva):
        if self.runtime_functions is None:
            prva, size = self.directories[3]
            data = self.code(prva, size)
            self.runtime_functions = list(struct.iter_unpack('<III', data))
            self.runtime_starts = [x[0] for x in self.runtime_functions]
        index = bisect.bisect_right(self.runtime_starts, rva) - 1
        if index >= 0:
            row = self.runtime_functions[index]
            if row[0] <= rva < row[1]:
                return row
        return None

    def lea_refs(self, start, end):
        data = self.code(start, end - start)
        for m in re.finditer(rb'[\x48-\x4f]\x8d[\x05\x0d\x15\x1d\x25\x2d\x35\x3d]', data):
            if m.start() + 7 > len(data):
                continue
            source = start + m.start()
            target = source + 7 + struct.unpack_from('<i', data, m.start() + 3)[0]
            yield source, target

    def packet_catalog(self):
        getnames = {}
        # A non-owning string view return: result pointer, string pointer, length.
        pattern = rb'\x48\x89\xd0\x48\x8d\x0d(.{4})\x48\x89\x0a\x48\xc7\x42\x08(.{4})\xc3'
        for s in self.sections:
            if not s['flags'] & 0x20000000:
                continue
            data = memoryview(self.data)[s['file_offset']:s['file_offset'] + s['file_size']]
            for m in re.finditer(pattern, data, re.S):
                rva = s['rva'] + m.start()
                target = rva + 10 + struct.unpack('<i', m[1])[0]
                length = struct.unpack('<I', m[2])[0]
                if not 6 < length < 100:
                    continue
                try:
                    name = self.code(target, length).decode('ascii')
                except (ValueError, UnicodeDecodeError):
                    continue
                if re.fullmatch(r'[A-Za-z][A-Za-z0-9]*Packet', name):
                    getnames[self.base + rva] = dict(name=name, get_name_rva=rva, name_string_rva=target)
        packets = []
        for s in self.sections:
            if s['flags'] & 0x20000000:
                continue
            size = s['file_size'] // 8 * 8
            data = memoryview(self.data)[s['file_offset']:s['file_offset'] + size]
            for i, (pointer,) in enumerate(struct.iter_unpack('<Q', data)):
                if pointer not in getnames or i < 2:
                    continue
                getid = struct.unpack_from('<Q', data, i * 8 - 8)[0] - self.base
                try:
                    body = self.code(getid, 6)
                except ValueError:
                    continue
                if body[0] != 0xb8 or body[5] != 0xc3:
                    continue
                packet_id = struct.unpack_from('<I', body, 1)[0]
                if not 0 < packet_id < 1024:
                    continue
                vtable = s['rva'] + i * 8 - 16
                row = dict(getnames[pointer], packet_id=packet_id, get_id_rva=getid,
                           vtable_rva=vtable,
                           evidence='string-view getName; preceding vtable entry returns immediate packet ID')
                for key in ('get_name_rva', 'name_string_rva', 'get_id_rva', 'vtable_rva'):
                    row[key] = hex(row[key])
                packets.append(row)
        names = {x['name'] for x in packets}
        return dict(executable_sha256=self.metadata['sha256'],
                    recognized_get_name_functions=len(getnames), mapped_packets=len(packets),
                    unique_packet_ids=len({x['packet_id'] for x in packets}),
                    unmapped_names=sorted({x['name'] for x in getnames.values()} - names),
                    packets=sorted(packets, key=lambda x: x['packet_id']))

    def input_flags(self):
        labels = []
        for s in self.sections:
            if not s['flags'] & 0x20000000:
                continue
            for source, target in self.lea_refs(s['rva'], s['rva'] + s['file_size']):
                if not self.section_for_rva(target):
                    continue
                # Check the exact enum label before materializing the C string.
                try:
                    if self.code(target, 20) == b'PlayerAuthInputData\0':
                        labels.append((source, target))
                except ValueError:
                    pass
        candidates = []
        for label_source, _ in labels:
            fn = self.function_containing(label_source)
            if fn is None:
                continue
            allrefs = list(self.lea_refs(fn[0], fn[1]))
            stringrefs = []
            for source, target in allrefs:
                s = self.section_for_rva(target)
                if not s or s['flags'] & 0x20000000:
                    continue
                try:
                    name = self.cstring(target, 96)
                except (ValueError, UnicodeDecodeError):
                    continue
                if re.fullmatch('[A-Za-z][A-Za-z0-9_]*', name):
                    stringrefs.append((source, target, name))
            for i, (source, target, name) in enumerate(stringrefs):
                if name == 'PlayerAuthInputData':
                    continue
                stop = stringrefs[i + 1][0] if i + 1 < len(stringrefs) else fn[1]
                for ref, getter in allrefs:
                    if not source < ref < stop:
                        continue
                    s = self.section_for_rva(getter)
                    if not s or not s['flags'] & 0x20000000:
                        continue
                    body = self.code(getter, 200)
                    type_hash = re.search(rb'\xc7\x46\x20(.{4})', body, re.S)
                    value = re.search(rb'\xc7\x06(.{4})', body, re.S)
                    if type_hash and value:
                        candidates.append(dict(name=name, value=struct.unpack('<I', value[1])[0],
                                            enum_type_hash=hex(struct.unpack('<I', type_hash[1])[0]),
                                            name_reference_rva=hex(source), getter_rva=hex(getter),
                                            registration_function_rva=hex(fn[0])))
        # A generic shared meta converter also returns a zero of another type.
        # The enum-specific hash must have a distinct value getter for each label.
        hashes = {x['enum_type_hash'] for x in candidates}
        selected_hash = max(hashes, key=lambda h: len({x['getter_rva'] for x in candidates if x['enum_type_hash'] == h}), default=None)
        results = []
        seen_names = set()
        for row in candidates:
            if row['enum_type_hash'] == selected_hash and row['name'] not in seen_names:
                results.append(row)
                seen_names.add(row['name'])
        return dict(enum='PlayerAuthInputData', members=results,
                    complete_observed_0_through_65=(len(results) == 66 and {x['value'] for x in results} == set(range(66))
                                                  and len({x['enum_type_hash'] for x in results}) == 1),
                    evidence='Enum label registration and constant-return metadata getter, not PDB offsets')


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('exe', type=Path)
    p.add_argument('--out', type=Path, required=True)
    p.add_argument('--perlin-schema', type=Path)
    p.add_argument('--mojang-json-dir', type=Path)
    a = p.parse_args()
    image = PE(a.exe)
    catalog = image.packet_catalog()
    flags = image.input_flags()
    mapping = {x['packet_id']: x['name'] for x in catalog['packets']}
    comparisons = {}
    if a.perlin_schema:
        schema = json.loads(a.perlin_schema.read_text())
        names = schema['types']['mcpe_packet'][1][0]['type'][1]['mappings']
        names = {int(k): v for k, v in names.items()}
        comparisons['perlin'] = dict(schema_sha256=hashlib.sha256(a.perlin_schema.read_bytes()).hexdigest(),
            packet_id_count=len(names), missing_from_perlin=[{'id': i, 'name': n} for i, n in mapping.items() if i not in names],
            schema_only_ids=[{'id': i, 'schema_name': n} for i, n in names.items() if i not in mapping],
            note='Schema-only IDs may be deprecated. Presence does not establish a behavioral defect.')
    if a.mojang_json_dir:
        names = {}
        for file in sorted(a.mojang_json_dir.glob('*.json')):
            d = json.loads(file.read_text())
            value = d.get('$metaProperties', {}).get('[cereal:packet]')
            if value is not None:
                names[int(value)] = d['title']
        comparisons['mojang'] = dict(packet_id_count=len(names),
            differences=[{'id': i, 'binary_name': mapping.get(i), 'official_name': names.get(i)}
                         for i in sorted(set(names) | set(mapping)) if names.get(i) != mapping.get(i)])
    a.out.mkdir(parents=True, exist_ok=True)
    def write(name, data):
        (a.out / name).write_text(json.dumps(data, indent=2) + '\n')
    write('executable_metadata.json', image.metadata)
    write('packet_catalog.json', catalog)
    write('player_auth_input_enum.json', flags)
    write('catalog_comparison.json', comparisons)
    print(json.dumps(dict(metadata=image.metadata, packet_summary={k:v for k,v in catalog.items() if k != 'packets'},
                          enum_members=len(flags['members']), enum_complete=flags['complete_observed_0_through_65'],
                          comparisons=comparisons), indent=2))


if __name__ == '__main__':
    main()
