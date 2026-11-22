#!/usr/bin/env python3
"""
Generate malformed MP4 mutations for robustness testing. Standard library
only; run with python3 -I. Each output must be handled without crashing.
"""
import os
import struct
import sys


def mutate(src_dir, out_dir):
    data = open(os.path.join(src_dir, 'chapters.mp4'), 'rb').read()
    os.makedirs(out_dir, exist_ok=True)

    # 1. truncate at every offset near atom boundaries (coarse steps to keep
    #    the corpus small)
    step = max(1, len(data) // 40)
    n = 0
    for cut in range(0, len(data), step):
        open(os.path.join(out_dir, f'trunc_{n:03d}.mp4'), 'wb').write(data[:cut])
        n += 1

    # 2. corrupt every byte of the chpl atom (located via marker)
    i = data.find(b'chpl')
    if i > 0:
        box_start = i - 4
        size = struct.unpack('>I', data[box_start:i])[0]
        for off in range(box_start, min(box_start + size, len(data))):
            for val in (0, 0xff):
                m = bytearray(data)
                m[off] = val
                open(os.path.join(out_dir, f'chplbyte_{off}_{val:02x}.mp4'),
                     'wb').write(bytes(m))

    # 3. inflated/deflated atom sizes
    for tag in (b'moov', b'udta', b'meta', b'chpl'):
        j = data.find(tag)
        if j < 0:
            continue
        for new_size in (0, 1, 7, 8, 0xffffffff):
            m = bytearray(data)
            m[j - 4:j] = struct.pack('>I', new_size)
            safe = tag.decode('latin1')
            open(os.path.join(out_dir, f'size_{safe}_{new_size:x}.mp4'),
                 'wb').write(bytes(m))

    # 4. random-ish byte flips at fixed positions (deterministic)
    for k, pos in enumerate(range(0, len(data), 997)):
        m = bytearray(data)
        m[pos] ^= 0xA5
        open(os.path.join(out_dir, f'flip_{k:03d}.mp4'), 'wb').write(bytes(m))

    print('corpus in', out_dir)


if __name__ == '__main__':
    mutate(sys.argv[1], sys.argv[2])
