#!/usr/bin/env python3
"""Generate malformed LATM/LOAS samples for deterministic-error tests.

Usage: gen_malformed.py <outdir> <tone.adts>
"""

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from adts2latm import (BitWriter, parse_adts, audio_specific_config,
                       mux_element, loas_frame)


def mux_element_custom(payload, asc_bits, version_a=False, num_program=0,
                       framelen_type=0, frame_length=0):
    """First-frame AudioMuxElement with tweakable broken fields."""
    w = BitWriter()
    w.write(0, 1)                # useSameStreamMux = 0
    w.write(1, 1)                # audioMuxVersion = 1
    if version_a:
        w.write(1, 1)            # audioMuxVersionA = 1 (unsupported)
        w.align()
        return w.bytes()
    w.write(0, 1)                # audioMuxVersionA = 0
    w.write(0, 2)                # taraBufferFullness: bytesForValue = 0
    w.write(1, 1)                # allStreamsSameTimeFraming
    w.write(0, 6)                # numSubFrames
    w.write(num_program, 4)      # numProgram (!= 0 is unsupported)
    w.write(0, 3)                # numLayer
    w.write(1, 2)                # ascLen: bytesForValue = 1
    w.write(len(asc_bits), 8)    # ascLen value, in bits
    for b in asc_bits:
        w.write(b, 1)
    w.write(framelen_type, 3)    # frameLengthType
    if framelen_type == 0:
        w.write(0, 8)            # latmBufferFullness
    elif framelen_type == 1:
        w.write(frame_length, 9)  # frameLength (0 is invalid)
    w.write(0, 1)                # otherDataPresent
    w.write(0, 1)                # crcCheckPresent
    n = len(payload)
    while n >= 255:
        w.write(255, 8)
        n -= 255
    w.write(n, 8)
    for byte in payload:
        w.write(byte, 8)
    w.align()
    return w.bytes()


def main():
    outdir, adts_path = sys.argv[1], sys.argv[2]
    data = open(adts_path, "rb").read()
    frames = parse_adts(data)
    profile, sf_index, chan_config, _ = frames[0]
    asc = audio_specific_config(profile, sf_index, chan_config)
    payload = frames[0][3]

    os.makedirs(outdir, exist_ok=True)

    def w(name, blob):
        open(os.path.join(outdir, name), "wb").write(blob)
        print("wrote", name, len(blob), "bytes")

    # pure garbage, no LOAS sync anywhere
    w("m_garbage.loas", bytes((i * 37 + 11) & 0xFF for i in range(512)))

    # audioMuxVersionA = 1: unsupported
    w("m_versiona.loas", loas_frame(mux_element_custom(payload, asc, version_a=True)))

    # numProgram != 0: unsupported configuration
    w("m_programs.loas", loas_frame(mux_element_custom(payload, asc, num_program=1)))

    # frameLengthType = 2: CELP/HVXC, unsupported
    w("m_celp.loas", loas_frame(mux_element_custom(payload, asc, framelen_type=2)))

    # frameLengthType = 1 with frameLength = 0: invalid
    w("m_framelen0.loas", loas_frame(mux_element_custom(payload, asc, framelen_type=1, frame_length=0)))

    # truncated inside the StreamMuxConfig
    good = loas_frame(mux_element(payload, asc, 0, True))
    w("m_trunc.loas", good[:6])

    # valid stream whose 3rd frame carries a broken mux element
    out = bytearray()
    out += loas_frame(mux_element(frames[0][3], asc, 0, True))
    out += loas_frame(mux_element(frames[1][3], asc, 0, False))
    out += loas_frame(mux_element_custom(frames[2][3], asc, num_program=1))
    for f in frames[3:]:
        out += loas_frame(mux_element(f[3], asc, 0, False))
    w("m_badmid.loas", bytes(out))


if __name__ == "__main__":
    main()
