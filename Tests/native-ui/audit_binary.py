#!/usr/bin/env python3
"""Read-only PE callsite/layout checks. Does not claim live hook or visual validation."""
import argparse
import hashlib
import json
import struct
from pathlib import Path

CALLS = {
    0x58FCE4: 0x58C080, 0x58D552: 0x58C080, 0x58FBEE: 0x58B180,
    0x58C092: 0x464980, 0x5810BD: 0x6A0050, 0x581122: 0x6A0050,
    0x581191: 0x6A0050, 0x58143E: 0x6A0050, 0x58B3F1: 0x6A0050,
    0x58B673: 0x6A0050, 0x573FC7: 0x6A0050, 0x58C1B3: 0x6A0050,
}


def audit(path):
    data = path.read_bytes()
    pe = struct.unpack_from('<I', data, 60)[0]
    assert data[:2] == b'MZ' and data[pe:pe+4] == b'PE\0\0'
    count, opt_size = struct.unpack_from('<H', data, pe+6)[0], struct.unpack_from('<H', data, pe+20)[0]
    opt = pe + 24
    assert struct.unpack_from('<H', data, opt)[0] == 0x10B, '32-bit PE required'
    base = struct.unpack_from('<I', data, opt+28)[0]
    sections = [struct.unpack_from('<IIII', data, opt+opt_size+40*i+8) for i in range(count)]

    def read(address, size):
        rva = address-base
        for virtual_size, virtual_address, raw_size, offset in sections:
            if virtual_address <= rva and rva+size <= virtual_address+raw_size:
                start = offset+rva-virtual_address
                return data[start:start+size]
        raise ValueError(f'unmapped address {address:#x}')

    for site, target in CALLS.items():
        instruction = read(site, 5)
        assert instruction[0] == 0xE8 and site+5+struct.unpack_from('<i', instruction, 1)[0] == target, hex(site)
    # The VM executable relocates AddClock/ClearCounter. The adapter never
    # calls those SCM routines: its renderer and data layout are the contract.
    assert hashlib.sha256(read(0x58B180, 0x740)).hexdigest() == '57a76bf68226c4c2e82f01ce49460285e98f6a8de2b7f9c29c8375515217db60', 'timer renderer'
    allocator = 'direct-reference' if read(0x44CD5D, 4) == bytes.fromhex('66 83 fa 01') else 'relocated-not-used-by-adapter'
    assert read(0x5822E7, 3) == bytes.fromhex('83 fe 02'), 'menu limit'
    assert read(0x58230B, 5) == bytes.fromhex('68 18 04 00 00'), 'menu structure size'
    assert read(0x58C09F, 5) == bytes.fromhex('bd 60 00 00 00'), 'script text limit'
    assert read(0x580C3C, 7) == bytes.fromhex('80 bc 02 d6 03 00 00'), 'unsafe retail grid acceptance read'
    return {'sha256': hashlib.sha256(data).hexdigest(), 'callsites': len(CALLS),
            'timerAllocator': allocator, 'clockSlots': 1, 'counterSlots': 4, 'menuSlots': 2, 'scriptTextSlots': 96,
            'scope': 'on-disk PE only; no live hooks, rendering or gameplay tested', 'result': 'pass'}


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('binary', type=Path, nargs='+')
    args = parser.parse_args()
    print(json.dumps([audit(path) for path in args.binary], indent=2))
