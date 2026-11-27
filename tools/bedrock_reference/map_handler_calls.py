#!/usr/bin/env python3
"""Summarize first-level call sites in identity-checked packet handler functions.

Reads an EXE, matching PDB, and map_matched_pdb.py manifest. Uses GNU objdump to
decode instructions; never executes the target. Does not save disassembly.
Indirect calls, tail calls, missing symbols and unknown boundaries remain
explicitly unresolved; this is not an interprocedural behavioral proof.
"""
import argparse
import collections
import concurrent.futures
import hashlib
import json
import re
import shutil
import subprocess
from pathlib import Path

from analyze_minecraft_static import PE
from pdb_inspect import PDB


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('exe', type=Path)
    ap.add_argument('pdb', type=Path)
    ap.add_argument('--manifest', required=True, type=Path)
    ap.add_argument('--out', required=True, type=Path)
    args = ap.parse_args()
    objdump = shutil.which('objdump')
    if not objdump:
        raise RuntimeError('GNU objdump is required for instruction decoding')
    pe, pdb = PE(args.exe), PDB(args.pdb)
    if not any(x['guid'] == pdb.guid and x['age'] == pdb.age for x in pe.metadata['codeview']):
        raise ValueError('PDB GUID/age does not match executable')
    manifest = json.loads(args.manifest.read_text())
    meta = manifest['metadata']
    if (meta['executable']['sha256'] != pe.metadata['sha256']
            or meta['pdb']['sha256'] != hashlib.sha256(pdb.b).hexdigest()
            or not meta.get('section_header_match')):
        raise ValueError('Manifest does not verify this exact executable and PDB')
    by_rva = collections.defaultdict(list)
    for symbol in pdb.symbols():
        if 1 <= symbol.get('section', 0) <= len(pe.sections):
            rva = pe.sections[symbol['section'] - 1]['rva'] + int(symbol['section_offset'], 16)
            by_rva[rva].append(symbol['name'])
    functions = {}
    for packet in manifest['packets']:
        for handler in packet['dispatch_handlers']:
            rva = int(handler['rva'], 16)
            functions.setdefault(rva, dict(rva=handler['rva'], symbols=set(), packet_ids=set(),
                                          vtable_dispatch=[], bounds=handler.get('unwind_function_range')))
            functions[rva]['symbols'].add(handler['name'])
            functions[rva]['packet_ids'].add(packet['packet_id'])
        dispatch = packet.get('client_dispatch')
        if dispatch:
            rva = int(dispatch['handler_rva'], 16)
            functions.setdefault(rva, dict(rva=dispatch['handler_rva'], symbols=set(), packet_ids=set(),
                                          vtable_dispatch=[], bounds=dispatch.get('unwind_function_range')))
            functions[rva]['symbols'].update(dispatch['public_symbol_names'])
            functions[rva]['packet_ids'].add(packet['packet_id'])
            functions[rva]['vtable_dispatch'].append(dict(packet_id=packet['packet_id'], packet_name=packet['name'],
                                                          dispatch_class=dispatch['dispatch_class'],
                                                          vtable_rva=dispatch['vtable_rva'], slot_offset=dispatch['slot_offset']))
            if dispatch['verified_ret_only']:
                if pe.code(rva, 1) != b'\xc3':
                    raise ValueError('Claimed RET-only client target does not begin with RET')
                functions[rva]['bounds'] = [hex(rva),hex(rva+1)]
                functions[rva]['boundary_evidence'] = 'Single RET instruction directly verified; no unwind range needed.'
            elif dispatch.get('verified_mark_handled_only'):
                if pe.code(rva, 6) != b'\x41\xc6\x40\x11\x01\xc3':
                    raise ValueError('Claimed mark-handled leaf does not match the verified instruction sequence')
                functions[rva]['bounds'] = [hex(rva),hex(rva+6)]
                functions[rva]['boundary_evidence'] = 'Store true to Packet::mIsHandled and RET directly verified; no unwind range needed.'

    def summarize(item):
        start, row = item
        row['symbols'] = sorted(row['symbols'])
        row['packet_ids'] = sorted(row['packet_ids'])
        if not row['bounds']:
            row.update(status='unresolved: no .pdata function boundary', direct_calls=[],
                       indirect_calls=[], external_jumps=[])
            return row
        low, high = [int(x, 16) for x in row['bounds']]
        if low != start:
            row.update(status='unresolved: symbol is inside a .pdata range', direct_calls=[],
                       indirect_calls=[], external_jumps=[])
            return row
        result = subprocess.run([objdump, '-d', '-Mintel', '--no-show-raw-insn',
                                 '--start-address=' + hex(pe.base + start),
                                 '--stop-address=' + hex(pe.base + high), str(args.exe)],
                                check=True, capture_output=True, text=True)
        instructions = []
        for line in result.stdout.splitlines():
            m = re.match(r'\s*([0-9a-fA-F]+):\s+(\S+)(?:\s+(.*))?$', line)
            if m:
                instructions.append((int(m[1], 16) - pe.base, m[2], (m[3] or '').strip()))
        direct, indirect, jumps = [], [], []
        for site, mnemonic, operand in instructions:
            if mnemonic not in ('call', 'callq', 'jmp', 'jmpq'):
                continue
            target_match = re.match(r'0x([0-9a-fA-F]+)(?:\s|$)', operand)
            if target_match:
                target = int(target_match[1], 16) - pe.base
                entry = dict(site_rva=hex(site), target_rva=hex(target),
                             target_symbols=sorted(by_rva.get(target, [])))
                if mnemonic.startswith('call'):
                    direct.append(entry)
                elif not low <= target < high:
                    entry['interpretation'] = 'possible tail call or external control transfer'
                    jumps.append(entry)
            elif mnemonic.startswith('call'):
                entry = dict(site_rva=hex(site), operand=operand.split('#')[0].strip(),
                             resolution='unresolved indirect target')
                # An import/CFG slot name does not resolve the eventual virtual target.
                slot = re.search(r'#\s*0x([0-9a-fA-F]+)', operand)
                if slot:
                    slot_rva = int(slot[1], 16) - pe.base
                    entry['pointer_slot_rva'] = hex(slot_rva)
                    entry['pointer_slot_symbols'] = sorted(by_rva.get(slot_rva, []))
                indirect.append(entry)
            else:
                jumps.append(dict(site_rva=hex(site), operand=operand.split('#')[0].strip(),
                                  resolution='unresolved indirect jump'))
        meaningful = [x for x in instructions if x[1] not in ('nop', 'nopl', 'nopw', 'int3')]
        only_return = bool(meaningful) and all(x[1] in ('ret', 'retq') for x in meaningful)
        row.update(status='decoded', code_size_bytes=high-low, instruction_count=len(instructions),
                   direct_calls=direct, indirect_calls=indirect, external_jumps=jumps,
                   only_return_and_padding=only_return,
                   behavioral_status='unverified: first-level control-transfer inventory only')
        return row

    with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
        rows = list(pool.map(summarize, sorted(functions.items())))
    result = dict(metadata=dict(executable_sha256=pe.metadata['sha256'],
                                pdb_sha256=meta['pdb']['sha256'], pdb_guid=pdb.guid, pdb_age=pdb.age,
                                pdb_match=True, section_header_match=True,
                                source_manifest_sha256=hashlib.sha256(args.manifest.read_bytes()).hexdigest(),
                                method='GNU objdump decoding within PE .pdata ranges or directly verified trivial leaf functions; exact public-symbol lookup',
                                limitations=[
                                    'Includes public dispatch symbols and, when present, the verified LegacyClientNetworkHandler vtable entries in the input manifest.',
                                    'Missing public symbols do not mean a packet is unhandled.',
                                    'Client vtable entries describe that dispatch route; alternate listeners are not resolved.',
                                    'Direct targets may be wrappers; indirect/virtual/CFG calls remain unresolved.',
                                    'No recursive control-flow analysis, runtime testing, or behavioral parity claim.',
                                    'Possible tail calls and external jumps are recorded separately.']),
                  summary=dict(packet_count=len(manifest['packets']),
                               packets_with_public_dispatch_symbols=meta['packets_with_dispatch_symbols'],
                               packets_with_verified_client_vtable_entry=sum('client_dispatch' in p for p in manifest['packets']),
                               unique_handler_entrypoints=len(rows),
                               decoded_entrypoints=sum(r['status'] == 'decoded' for r in rows),
                               direct_call_sites=sum(len(r['direct_calls']) for r in rows),
                               direct_call_sites_with_symbols=sum(bool(c['target_symbols']) for r in rows for c in r['direct_calls']),
                               unresolved_indirect_call_sites=sum(len(r['indirect_calls']) for r in rows),
                               return_only_entrypoints=sum(r.get('only_return_and_padding', False) for r in rows)),
                  handlers=rows)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result['summary'], indent=2))


if __name__ == '__main__':
    main()
