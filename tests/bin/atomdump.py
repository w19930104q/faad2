#!/usr/bin/env python3
"""Minimal ISO-BMFF atom dumper (standard library only). Run with: python3 -I ..."""
import struct
import sys

CONTAINERS = {b'moov', b'trak', b'mdia', b'minf', b'stbl', b'edts',
              b'dinf', b'udta', b'meta', b'ilst', b'----'}


def dump(path):
    with open(path, 'rb') as f:
        data = f.read()

    def walk(start, end, depth):
        off = start
        while off + 8 <= end:
            size, name = struct.unpack('>I4s', data[off:off + 8])
            hdr = 8
            if size == 1:
                size = struct.unpack('>Q', data[off + 8:off + 16])[0]
                hdr = 16
            if size == 0:
                size = end - off
            print(' ' * (depth * 2) + name.decode('latin1') + f' @{off} size={size}')
            if name == b'chpl':
                # Observed layout (FFmpeg movenc, Apple QT):
                # version(1) flags(3) reserved(4) chapter_count(1),
                # then per entry: timestamp(8) name_len(1) name(UTF-8).
                payload = off + hdr
                ver = data[payload]
                cnt = data[payload + 8]
                print(' ' * (depth * 2 + 2) + f'version={ver} count={cnt}')
                p = payload + 9
                for i in range(cnt):
                    ts = struct.unpack('>Q', data[p:p + 8])[0]
                    nlen = data[p + 8]
                    title = data[p + 9:p + 9 + nlen]
                    print(' ' * (depth * 2 + 2)
                          + f'[{i}] ts={ts} namelen={nlen} title={title!r}')
                    p += 9 + nlen
            if name in CONTAINERS:
                child = off + hdr
                if name == b'meta' and data[child:child + 4] == b'\x00\x00\x00\x00':
                    child += 4
                walk(child, off + size, depth + 1)
            off += size

    walk(0, len(data), 0)


if __name__ == '__main__':
    dump(sys.argv[1])
