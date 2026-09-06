#!/usr/bin/env python3
"""Read-only checks of the retail entry points used by the cargo adapter.

Usage: python3 tests/retail_layout.py /path/to/gta_sa.exe
This establishes disk ABI evidence, not in-game behavior after hooks.
"""
import hashlib
from pathlib import Path
import struct
import sys

image = Path(sys.argv[1]).read_bytes()
u16 = lambda offset: struct.unpack_from('<H', image, offset)[0]
u32 = lambda offset: struct.unpack_from('<I', image, offset)[0]
pe = u32(0x3C)
assert image[pe:pe+4] == b'PE\0\0'
assert u16(pe+4) == 0x14C and u16(pe+24) == 0x10B, 'requires x86 PE32'
base = u32(pe+52)
sections = []
for i in range(u16(pe+6)):
    offset = pe + 24 + u16(pe+20) + 40*i
    size, rva, raw_size, raw_offset = struct.unpack_from('<IIII', image, offset+8)
    sections.append((rva, raw_size, raw_offset))
def read(address, length):
    rva = address-base
    for start, size, raw in sections:
        if start <= rva and rva+length <= start+size:
            return image[raw+rva-start:raw+rva-start+length]
    raise AssertionError(f'address has no disk mapping: {address:#x}')
for vtable in (0x870B2C, 0x870B50, 0x870B74):
    entries = struct.unpack('<9I', read(vtable, 36))
    assert entries[6:] == (0x693BD0, 0x693C40, 0x6940A0), hex(vtable)
    assert 0x400000 <= entries[0] < 0x800000, 'invalid retail deleting destructor'
for site, target in ((0x46B090,0x6913A0), (0x46B0C4,0x691470),
                     (0x6917D5,0x6913A0), (0x691848,0x691470), (0x6919A4,0x6913A0)):
    call = read(site,5)
    assert call[0] == 0xE8 and site+5+struct.unpack('<i',call[1:])[0] == target
for site, table in ((0x6913D3,0x870B2C),(0x6917E1,0x870B50),(0x6919B1,0x870B74)):
    assert struct.pack('<I',table) in read(site,10), 'constructor vtable write changed'
print('Retail cargo ABI checks passed; SHA256=' + hashlib.sha256(image).hexdigest())
