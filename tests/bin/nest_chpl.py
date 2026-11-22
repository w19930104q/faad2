#!/usr/bin/env python3
"""
Move the chpl atom from udta/chpl into udta/meta/chpl to exercise the nested
placement. Standard library only; run with python3 -I.
"""
import struct
import sys


def main(src, dst):
    data = bytearray(open(src, 'rb').read())

    # locate udta, its meta child, and the sibling chpl
    def atom_at(buf, off):
        size, name = struct.unpack('>I4s', bytes(buf[off:off + 8]))
        return size, name

    # find chpl and enclosing udta by scanning known markers
    chpl_tag = data.find(b'chpl')
    chpl_start = chpl_tag - 4
    chpl_size = struct.unpack('>I', bytes(data[chpl_start:chpl_start + 4]))[0]
    chpl = bytes(data[chpl_start:chpl_start + chpl_size])

    # find meta (FullBox) inside the same udta
    meta_tag = data.find(b'meta')
    meta_start = meta_tag - 4
    meta_size = struct.unpack('>I', bytes(data[meta_start:meta_start + 4]))[0]
    meta_end = meta_start + meta_size

    # sanity: chpl must immediately follow meta in our fixture
    assert meta_end == chpl_start, 'expected chpl right after meta'

    # remove chpl, grow meta to include it (meta payload after its 12-byte head)
    without_chpl = bytes(data[:chpl_start]) + bytes(data[chpl_start + chpl_size:])
    new_meta_size = meta_size + chpl_size
    # patch meta size in the trimmed buffer
    out = bytearray(without_chpl)
    out[meta_start:meta_start + 4] = struct.pack('>I', new_meta_size)
    # insert chpl at old meta_end position (which is now chpl_start after trim)
    out[meta_end:meta_end] = chpl
    open(dst, 'wb').write(bytes(out))
    print('wrote', dst, len(out), 'bytes; new meta size', new_meta_size)


if __name__ == '__main__':
    main(sys.argv[1], sys.argv[2])
