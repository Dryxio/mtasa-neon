#!/usr/bin/env python3
"""Audit PE import/delay-import dependencies without trusting a developer's PATH.

This checks the package closure, not arbitrary LoadLibrary strings or optional
Windows features. Run against composed InstallFiles or an extracted installer.
"""
import argparse
import hashlib
import json
import struct
from pathlib import Path


def imports(path):
    data = path.read_bytes()
    def read(fmt, offset):
        return struct.unpack_from(fmt, data, offset)
    if data[:2] != b'MZ':
        raise ValueError(f'{path}: not a PE executable')
    pe, = read('<I', 0x3c)
    if data[pe:pe + 4] != b'PE\0\0':
        raise ValueError(f'{path}: invalid PE header')
    machine, sections = read('<HH', pe + 4)
    optional_size, = read('<H', pe + 20)
    optional = pe + 24
    magic, = read('<H', optional)
    if magic not in (0x10b, 0x20b):
        raise ValueError(f'{path}: unsupported PE format')
    is64 = magic == 0x20b
    image_base, = read('<Q' if is64 else '<I', optional + (24 if is64 else 28))
    directory = optional + (112 if is64 else 96)
    count, = read('<I', directory - 4)
    section_table = optional + optional_size
    mappings = []
    for index in range(sections):
        _, rva, raw_size, raw = read('<IIII', section_table + index * 40 + 8)
        mappings.append((rva, raw_size, raw))
    def offset(rva):
        for start, size, raw in mappings:
            if start <= rva < start + size:
                result = raw + rva - start
                if result < len(data):
                    return result
        raise ValueError(f'{path}: unmapped import RVA {rva:#x}')
    found = set()
    for index, size in ((1, 20), (13, 32)):
        if count <= index:
            continue
        rva, length = read('<II', directory + index * 8)
        if not rva:
            continue
        cursor = offset(rva)
        # Walk only the declared directory; malformed binaries must fail closed.
        for step in range(0, length, size):
            values = read('<' + 'I' * (size // 4), cursor + step)
            if not any(values):
                break
            name_rva = values[3] if index == 1 else values[1]
            if index == 13 and not values[0] & 1:
                name_rva -= image_base
            start = offset(name_rva)
            end = data.find(b'\0', start, start + 512)
            if end == -1:
                raise ValueError(f'{path}: invalid import name')
            found.add(data[start:end].decode('ascii').lower())
    return machine, sorted(found)


def audit(root, policy, vc2010=None, *, client_only=False):
    binaries = []
    for path in sorted(root.rglob('*')):
        relative = path.relative_to(root).as_posix().replace('\\', '/')
        # compose_files also stages foreign-architecture server network modules.
        # CLIENT_ONLY NSIS never ships that tree; it cannot supply client imports
        # or create missing-dependency failures in a client installer audit.
        if client_only and relative.split('/')[0].casefold() == 'server':
            continue
        if path.is_file() and path.suffix.lower() in ('.exe', '.dll'):
            machine, dependencies = imports(path)
            binaries.append({'file': relative, 'machine': machine, 'imports': dependencies})
    if not binaries:
        raise ValueError('No PE binaries found')
    packaged = {(Path(row['file']).name.lower(), row['machine']) for row in binaries}
    # XInput's suffixed payload is copied to its import name by InitLocalization.
    aliases = policy['loaderAliases']
    vc_present = vc2010 and vc2010.is_file() and hashlib.sha256(vc2010.read_bytes()).hexdigest() == policy['vc2010Sha256']
    findings = []
    for row in binaries:
        for name in row['imports']:
            if (name, row['machine']) in packaged:
                continue
            if (aliases.get(name), row['machine']) in packaged:
                continue
            if name in policy['windows10Libraries'] or name.startswith(('api-ms-win-', 'ext-ms-win-')):
                continue
            if name == 'msvcr100.dll' and row['machine'] == 0x14c and vc_present:
                continue
            findings.append({'file': row['file'], 'dependency': name,
                             'severity': 'warning' if name in policy['existingPrerequisites'] else 'error',
                             'reason': policy['existingPrerequisites'].get(name, 'No matching packaged DLL or verified prerequisite')})
    return {'binaries': binaries, 'findings': findings, 'vc2010Verified': bool(vc_present), 'scope': 'client-only' if client_only else 'all'}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('root', type=Path)
    parser.add_argument('--vc2010-installer', type=Path)
    parser.add_argument('--output', type=Path)
    parser.add_argument('--client-only', action='store_true',
                        help='Match CLIENT_ONLY NSIS: exclude the top-level server tree from consumers and providers')
    args = parser.parse_args()
    policy = json.loads(Path(__file__).with_name('import-policy.json').read_text())
    report = audit(args.root, policy, args.vc2010_installer, client_only=args.client_only)
    if args.output:
        args.output.write_text(json.dumps(report, indent=2) + '\n')
    print(f"Audited {len(report['binaries'])} PE binaries (normal and delayed imports)")
    for finding in report['findings']:
        print(f"{finding['severity']}: {finding['file']} -> {finding['dependency']}: {finding['reason']}")
    return int(any(item['severity'] == 'error' for item in report['findings']))


if __name__ == '__main__':
    raise SystemExit(main())
