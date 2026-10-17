"""Run ONLY inside the existing Binary Ninja GUI using bnctl exec -f.

Read-only static inspection: no analysis changes, native calls, target process,
BN database saves, names, comments, type changes, or filesystem writes. Each
invocation prints bounded JSON evidence; the caller must checkpoint its output
using the explicit file tools. Optional query configuration lives in
bnx.stash['layered_menu_query']; its absence selects the preserved registration
and DTI-accessor anchors.

Example (set in a one-line bnctl exec, then run this file with -f):
    import claude_bnx as bnx; bnx.stash['layered_menu_query'] = {'preset': 'factory'}

Use a preset from PRESETS below or an explicitly bounded custom query. Custom
keys include functions, regions, references, instruction_ranges, max_lines,
hlil_offset, max_instructions, max_refs and include_instructions. Prefer one
large function per query to stay within bnctl's output cap. The caller preserves
stdout evidence in re/observations/static-layered-menu.json using file tools.
The assigned binary and database paths are fixed. This probe writes no files.
BN's displayed prototypes are analysis hypotheses, never verified call ABIs.
"""
import datetime
import hashlib
import json
import os
import struct

import claude_bnx as bnx

EXPECTED_SHA256 = 'FE50D8468881FF8CB094C5F9A599A5D6B2965950331A22EE34148C612651C521'
ROOT = 'C:/Projects/MHW Mods'
BINARY = ROOT + '/re/inputs/mhw-current/MonsterHunterWorld.exe.unpacked.exe'
DATABASE = BINARY + '.bndb'
IMAGE_BASE = 0x140000000
view = bv


def hexaddr(address):
    return hex(int(address))


def location(address):
    return {'va': hexaddr(address), 'rva': hexaddr(int(address) - IMAGE_BASE)}


def normalized(path):
    return os.path.normcase(os.path.abspath(path)).replace('\\', '/').lower()


def file_hash(path):
    digest = hashlib.sha256()
    with open(path, 'rb') as source:
        while True:
            block = source.read(1024 * 1024)
            if not block:
                break
            digest.update(block)
    return digest.hexdigest().upper()


if normalized(view.file.filename) != normalized(DATABASE):
    raise RuntimeError('Wrong open database: ' + repr(view.file.filename))
if view.start != IMAGE_BASE or view.arch.name != 'x86_64':
    raise RuntimeError('Unexpected image base or architecture')
if file_hash(BINARY) != EXPECTED_SHA256:
    raise RuntimeError('Isolated analysis binary hash mismatch')
raw = view
while raw.parent_view is not None:
    raw = raw.parent_view
raw_digest = hashlib.sha256()
for offset in range(raw.start, raw.end, 1024 * 1024):
    block = raw.read(offset, min(1024 * 1024, raw.end - offset))
    if not block:
        raise RuntimeError('Incomplete raw-view identity read')
    raw_digest.update(block)
if raw_digest.hexdigest().upper() != EXPECTED_SHA256:
    raise RuntimeError('Open database backing bytes do not match expected binary')
identity = {
    'status': 'observed', 'binary': BINARY, 'binary_sha256': EXPECTED_SHA256,
    'database': view.file.filename,
    'original_filename_metadata': view.file.original_filename,
    'image_base': hexaddr(view.start), 'architecture': view.arch.name,
    'raw_view_type': raw.view_type, 'raw_view_length': raw.end - raw.start,
    'raw_view_sha256': raw_digest.hexdigest().upper(),
    'note': 'The original_filename metadata names the source DLSS analysis file; the open database path and its raw backing bytes are independently verified here.'
}
PRESETS = {
    'anchors': {'functions': [0x140037c30, 0x140525bf0],
                'regions': [{'address': 0x144ccd020, 'qwords': 12}],
                'references': [0x144ccd020]},
    'factory': {'functions': [0x140523680, 0x140523950],
                'regions': [{'address': 0x142fa77c8, 'qwords': 4}],
                'max_lines': 80, 'max_instructions': 90},
    'instance-vtable': {'regions': [{'address': 0x142fa77f8, 'qwords': 95}],
                        'references': [0x142fa77f8]},
    'enumeration': {'functions': [0x140525370], 'include_instructions': False,
                    'max_lines': 200},
    'enumeration-raw': {'instruction_ranges': [
        {'address': 0x1405253b1, 'end': 0x140525430, 'limit': 40},
        {'address': 0x140525478, 'end': 0x1405254e0, 'limit': 40}]},
    'helpers': {'functions': [0x1416721d0, 0x141672230, 0x141672810, 0x140311500],
                'max_lines': 80, 'max_instructions': 40},
    'availability': {'functions': [0x14134ce60], 'max_lines': 55,
                     'include_instructions': False,
                     'instruction_ranges': [
                         {'address': 0x14134ea20, 'end': 0x14134ea48, 'limit': 20}]},
    'bindings': {'functions': [0x1405256b0], 'max_lines': 130,
                 'include_instructions': False},
    'activation': {'functions': [0x140524230], 'max_lines': 200,
                   'include_instructions': False},
    'activation-tail': {'functions': [0x140524230], 'hlil_offset': 200,
                        'max_lines': 150, 'include_instructions': False},
    'activation-raw': {'instruction_ranges': [
        {'address': 0x1405244d3, 'end': 0x14052450f, 'limit': 20},
        {'address': 0x140524530, 'end': 0x140524573, 'limit': 20}]},
    'dialog-tables': {'regions': [{'address': 0x142fa7be8, 'qwords': 42}]},
    'single-result': {'functions': [0x140523e60], 'max_lines': 120,
                      'include_instructions': False},
    'set-result': {'functions': [0x140524000], 'max_lines': 120,
                   'include_instructions': False},
    'type3-result': {'functions': [0x140523a50, 0x140524170], 'max_lines': 140,
                     'include_instructions': False},
    'refresh-lead': {'functions': [0x142041700], 'max_lines': 120,
                     'include_instructions': False},
    'event-dispatch': {'functions': [0x140524a70], 'max_lines': 180,
                       'include_instructions': False},
    'contexts': {'functions': [0x141b42010, 0x141b8dbb0], 'max_lines': 60,
                 'max_instructions': 40},
}
configuration = dict(bnx.stash.get('layered_menu_query', {'preset': 'anchors'}))
preset_name = configuration.pop('preset', None)
query = dict(PRESETS[preset_name]) if preset_name else {}
query.update(configuration)
query.setdefault('label', preset_name or 'custom')
max_lines = min(int(query.get('max_lines', 120)), 6000)
max_instructions = min(int(query.get('max_instructions', 180)), 6000)
max_refs = min(int(query.get('max_refs', 100)), 1000)


def symbols_at(address):
    return [s.full_name for s in view.get_symbols(address, 1)][:8]


def reference_snapshot(address):
    refs = list(view.get_code_refs(address))
    data_refs = list(view.get_data_refs(address))
    return {
        **location(address), 'symbols': symbols_at(address),
        'code_ref_count': len(refs),
        'code_refs': [{**location(r.address),
                      'function': location(r.function.start) if r.function else None,
                      'function_name': r.function.name if r.function else None}
                     for r in refs[:max_refs]],
        'data_ref_count': len(data_refs),
        'data_refs': [location(r) for r in data_refs[:max_refs]],
        'truncated': len(refs) > max_refs or len(data_refs) > max_refs,
        'coverage': 'Existing BN cross-reference database only; a zero count does not establish absence.'
    }


def function_snapshot(address):
    function = view.get_function_at(address)
    if function is None:
        containing = list(view.get_functions_containing(address))
        return {**location(address), 'function_found_at_exact_address': False,
                'containing_functions': [location(f.start) for f in containing]}
    result = {
        **location(function.start), 'name': function.name,
        'bn_analysis_prototype_unverified': str(function.type),
        'verified_call_signature': 'unknown',
        'highest_address': hexaddr(function.highest_address),
        'total_bytes': function.total_bytes,
        'callees': [hexaddr(address) for address in sorted({f.start for f in function.callees})][:500],
        'hlil': [], 'instructions': [],
    }
    try:
        first_line = max(0, int(query.get('hlil_offset', 0)))
        result['hlil_offset'] = first_line
        for index, line in enumerate(function.hlil.root.lines):
            if index < first_line:
                continue
            if index >= first_line + max_lines:
                result['hlil_truncated'] = True
                break
            result['hlil'].append({**location(line.address), 'text': str(line)})
    except Exception as exc:
        result['hlil_error'] = repr(exc)
    count = 0
    for block in sorted(function.basic_blocks, key=lambda b: b.start):
        address = block.start
        while address < block.end:
            if count >= max_instructions:
                result['instructions_truncated'] = True
                break
            size = view.get_instruction_length(address)
            if not size:
                result['instruction_decode_error'] = hexaddr(address)
                break
            result['instructions'].append({
                **location(address), 'bytes': view.read(address, size).hex(' '),
                'text': view.get_disassembly(address),
            })
            address += size
            count += 1
        if count >= max_instructions:
            break
    return result


def region_snapshot(address, qwords):
    qwords = max(1, min(int(qwords), 256))
    data = view.read(address, qwords * 8)
    entries = []
    for offset in range(0, len(data) - 7, 8):
        value = struct.unpack_from('<Q', data, offset)[0]
        target_function = view.get_function_at(value)
        entries.append({
            'offset': hexaddr(offset), **location(address + offset),
            'qword': hexaddr(value),
            'target_symbols': symbols_at(value),
            'target_function': ({**location(value), 'name': target_function.name}
                                if target_function else None),
        })
    return {**location(address), 'bytes': data.hex(' '), 'entries': entries,
            'note': 'Raw qwords, not an assertion that this region is one complete vtable or structure.'}


run = {
    'timestamp_utc': datetime.datetime.now(datetime.timezone.utc).isoformat(),
    'label': query.get('label', 'unnamed'),
    'binary_sha256': EXPECTED_SHA256,
    'query': query, 'functions': [], 'regions': [], 'references': [],
}
for address in query.get('functions', []):
    snapshot = function_snapshot(int(address))
    snapshot['hlil'] = [[line['va'], line['text']] for line in snapshot.get('hlil', [])
                        if line['text'].strip()]
    if query.get('include_instructions', True):
        snapshot['instructions'] = [[line['va'], line['bytes'], line['text']]
                                    for line in snapshot.get('instructions', [])]
    else:
        snapshot.pop('instructions', None)
    run['functions'].append(snapshot)
for region in query.get('regions', []):
    snapshot = region_snapshot(int(region['address']), region.get('qwords', 16))
    snapshot['entries'] = [[entry['offset'], entry['qword']]
                           for entry in snapshot['entries']]
    run['regions'].append(snapshot)
for address in query.get('references', []):
    run['references'].append(reference_snapshot(int(address)))
for entry in query.get('instruction_ranges', []):
    address, end = int(entry['address']), int(entry['end'])
    # Reject a guessed range starting in the middle of an instruction.
    boundary_verified = False
    for function in view.get_functions_containing(address):
        for block in function.basic_blocks:
            if block.start <= address < block.end:
                cursor = block.start
                while cursor < address:
                    size = view.get_instruction_length(cursor)
                    if not size:
                        break
                    cursor += size
                if cursor == address:
                    boundary_verified = True
    if not boundary_verified:
        raise ValueError('Instruction range start is not an analyzed boundary: ' + hexaddr(address))
    items = []
    for _ in range(min(int(entry.get('limit', 120)), 1000)):
        if address >= end:
            break
        size = view.get_instruction_length(address)
        if not size:
            break
        items.append({**location(address), 'bytes': view.read(address, size).hex(' '),
                      'text': view.get_disassembly(address)})
        address += size
    run.setdefault('instruction_ranges', []).append({
        **location(int(entry['address'])), 'requested_end': hexaddr(end),
        'decoded_end': hexaddr(address), 'instructions': items})
run['completed'] = True
print(json.dumps({'identity': identity, 'run': run}, ensure_ascii=False, indent=2))
