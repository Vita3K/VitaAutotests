#!/usr/bin/env python3
# Rewrites one export NID in a .velf: set_nid.py FILE OLD NEW, with exactly one OLD in the file
import sys

path, old, new = sys.argv[1], int(sys.argv[2], 16), int(sys.argv[3], 16)
data = bytearray(open(path, 'rb').read())
old_bytes = old.to_bytes(4, 'little')
if data.count(old_bytes) != 1:
    sys.exit(f'{path}: {old:#010x} appears {data.count(old_bytes)} times, expected once')
at = data.index(old_bytes)
data[at:at + 4] = new.to_bytes(4, 'little')
open(path, 'wb').write(data)
