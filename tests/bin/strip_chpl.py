#!/usr/bin/env python3
"""
Strip the Apple "chpl" atom from an MP4 so only the ISO chapter reference
text track remains. Container atoms are rebuilt with corrected sizes; all
other atoms are copied byte-for-byte.

Standard library only. Run with: python3 -I ...
"""
import struct
import sys

CONTAINERS = {b'moov', b'trak', b'mdia', b'minf', b'stbl', b'edts',
              b'dinf', b'udta'}


def rebuild(data, start, end, drop):
    """Return the (possibly rewritten) byte range [start, end)."""
    out = bytearray()
    off = start
    while off + 8 <= end:
        size, name = struct.unpack('>I4s', data[off:off + 8])
        hdr = 8
        if size == 1:
            size = struct.unpack('>Q', data[off + 8:off + 16])[0]
            hdr = 16
        atom_end = off + size
        if name == drop:
            off = atom_end
            continue
        if name in CONTAINERS:
            child_payload = rebuild(data, off + hdr, atom_end, drop)
            out += struct.pack('>I', hdr + len(child_payload)) + name
            if hdr == 16:
                out += data[off + 8:off + 16]
            out += child_payload
        else:
            out += data[off:atom_end]
        off = atom_end
    return bytes(out)


if __name__ == '__main__':
    src, dst = sys.argv[1], sys.argv[2]
    data = open(src, 'rb').read()
    patched = rebuild(data, 0, len(data), b'chpl')
    open(dst, 'wb').write(patched)
    print(f'wrote {dst}: {len(data)} -> {len(patched)} bytes')
