#!/usr/bin/env python3
"""Disk ABI evidence for native pickup and source-filtered noise (no game launch).

Usage: python3 pickup_retail.py /path/to/gta_sa.exe [/path/to/GTA/data] [--native-only]
"""
from pathlib import Path
import re
import runpy
import struct
import sys

retail = runpy.run_path(str(Path(__file__).with_name('retail_layout.py')))
read = retail['read']
expected = (0x6935F0, 0x692C80, 0x421190, 0x4211A0, 0x691A40, 0x421180,
            0x4211B0, 0x61A430, 0x691AE0, 0x693610, 0x691D50)
assert struct.unpack('<11I', read(0x870B98, 44)) == expected
assert read(0x691A40, 6) == bytes.fromhex('b8 36 01 00 00 c3'), 'task type 310'
assert read(0x691A3B, 3) == bytes.fromhex('c2 08 00'), 'constructor takes entity/group'
assert read(0x692C97, 2) == bytes.fromhex('6a 34'), 'retail pickup root size'
assert read(0x692CB3, 6) == bytes.fromhex('8b 4e 2c 8b 56 0c'), 'clone copies group/entity'
assert read(0x691C57, 2) == bytes.fromhex('6a 4c'), 'simple pickup size'
assert read(0x691CE3, 2) == bytes.fromhex('6a 3c'), 'secondary hold size'
for site, target in ((0x692CBD, 0x6919C0), (0x691A1E, 0x571B70),
                     (0x691A86, 0x571A00), (0x691AC0, 0x61A3D0),
                     (0x691C8D, 0x6917B0), (0x691D14, 0x6913A0),
                     (0x691D31, 0x681B60), (0x4AB937, 0x4B2850),
                     (0x4AB957, 0x4AC050)):
    call = read(site, 5)
    assert call[0] == 0xE8 and site + 5 + struct.unpack('<i', call[1:])[0] == target, hex(site)
assert read(0x4AB924, 11) == bytes.fromhex('ff 50 28 3b c7 74 04 85 ff 75 38'), 'sound source filter'
assert read(0x4AB97B, 3) == bytes.fromhex('c2 08 00'), 'sound query arguments'
assert read(0x6932EB, 3) == bytes.fromhex('f6 c4 20'), 'drop checks liftable bit 0x2000'
assert read(0x5E061D, 6) == bytes.fromhex('83 7e 1c ff 75 2a'), 'quiet event captures position for time -1'

arguments = [arg for arg in sys.argv[2:] if arg != '--native-only']
if arguments:
    data = Path(arguments[0])
    models = {}
    for line in (data/'maps/interior/props.ide').read_text().splitlines():
        fields = [v.strip() for v in line.split(',')]
        if len(fields) >= 2 and fields[0].isdigit():
            models[fields[1].upper()] = int(fields[0])
    selected, subgroup = set(), ''
    groups = {'LOUNGE_TVS', 'LOUNGE_HIFIS', 'LOUNGE_VIDEOS', 'LOUNGE_CONSOLES'}
    for line in (data/'furnitur.dat').read_text().splitlines():
        if line.startswith('SUBGROUP:'):
            subgroup = line.split()[1]
        if line.startswith('ITEM:') and subgroup in groups:
            selected.add(models[line.split()[1].upper()])
    source = (Path(__file__).resolve().parents[3]/'Client/mods/deathmatch/logic/CClientCargoManager.cpp').read_text()
    allowlist = source.split('bool IsPickupModel(', 1)[1].split('const char* StateName', 1)[0]
    implemented = {int(v) for v in re.findall(r'case (\d+):', allowlist)}
    assert implemented == selected | {1271}, (implemented, selected)
    print(f'Stock furniture correspondence passed: {len(selected)} household models + crate.')
print('Pickup root/clone/lifecycle and source-filtered noise disk ABI checks passed.')
