#!/usr/bin/env python3
# Rewrites the attribute of one library export in a .velf: set_export_attr.py FILE LIBRARY_NID NEW_ATTR,
# with exactly one export entry of that library NID in the file. An export entry is 0x20 bytes long,
# starts with its size, holds its u16 attribute at +4 and its library NID at +0x10.
import struct
import sys

path, nid, attr = sys.argv[1], int(sys.argv[2], 16), int(sys.argv[3], 16)
data = bytearray(open(path, 'rb').read())
nid_bytes = nid.to_bytes(4, 'little')
entries = []
at = data.find(nid_bytes)
while at >= 0:
    entry = at - 0x10
    if entry >= 0 and struct.unpack_from('<H', data, entry)[0] == 0x20:
        entries.append(entry)
    at = data.find(nid_bytes, at + 1)
if len(entries) != 1:
    sys.exit(f'{path}: {len(entries)} export entries of library {nid:#010x}, expected one')
struct.pack_into('<H', data, entries[0] + 4, attr)
open(path, 'wb').write(data)
