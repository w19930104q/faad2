#!/usr/bin/env python3
"""Convert an ADTS AAC stream to a LATM/LOAS stream.

Usage: adts2latm.py <in.adts> <out.loas> <muxversion:0|1>

The AAC raw_data_block of every ADTS frame is wrapped verbatim in a
LATM AudioMuxElement (payloadLengthInfo + payload), so the decoded
output is identical. The first frame carries a full StreamMuxConfig
(audioMuxVersion 0 or 1), later frames use useSameStreamMux.
"""

import sys


class BitWriter:
    def __init__(self):
        self.bits = []

    def write(self, value, nbits):
        for i in range(nbits - 1, -1, -1):
            self.bits.append((value >> i) & 1)

    def align(self):
        while len(self.bits) % 8:
            self.bits.append(0)

    def bytes(self):
        assert len(self.bits) % 8 == 0
        out = bytearray()
        for i in range(0, len(self.bits), 8):
            b = 0
            for j in range(8):
                b = (b << 1) | self.bits[i + j]
            out.append(b)
        return bytes(out)


def parse_adts(data):
    """Yield (profile, sf_index, chan_config, protection_absent, payload)."""
    pos = 0
    frames = []
    while pos + 7 <= len(data):
        if data[pos] != 0xFF or (data[pos + 1] & 0xF0) != 0xF0:
            raise ValueError("ADTS sync lost at %d" % pos)
        protection_absent = data[pos + 1] & 1
        profile = (data[pos + 2] >> 6) & 3
        sf_index = (data[pos + 2] >> 2) & 0xF
        chan_config = ((data[pos + 2] & 1) << 2) | ((data[pos + 3] >> 6) & 3)
        frame_length = ((data[pos + 3] & 3) << 11) | (data[pos + 4] << 3) | (data[pos + 5] >> 5)
        header = 7 if protection_absent else 9
        payload = data[pos + header:pos + frame_length]
        frames.append((profile, sf_index, chan_config, payload))
        pos += frame_length
    if pos != len(data):
        raise ValueError("trailing garbage")
    return frames


def write_latm_value(w, value):
    """LATM 'bytesForValue' variable length integer."""
    nbytes = 0
    v = value
    while v:
        nbytes += 1
        v >>= 8
    w.write(nbytes, 2)
    for i in range(nbytes - 1, -1, -1):
        w.write((value >> (8 * i)) & 0xFF, 8)


def audio_specific_config(profile, sf_index, chan_config):
    w = BitWriter()
    w.write(profile + 1, 5)      # audioObjectType
    w.write(sf_index, 4)         # samplingFrequencyIndex
    w.write(chan_config, 4)      # channelConfiguration
    # GASpecificConfig
    w.write(0, 1)                # frameLengthFlag
    w.write(0, 1)                # dependsOnCoreCoder
    w.write(0, 1)                # extensionFlag
    return w.bits


def mux_element(payload, asc_bits, version, with_config):
    w = BitWriter()
    if with_config:
        w.write(0, 1)            # useSameStreamMux = 0
        w.write(1 if version == 1 else 0, 1)  # audioMuxVersion
        if version == 1:
            w.write(0, 1)        # audioMuxVersionA = 0
            write_latm_value(w, 0)  # taraBufferFullness
        w.write(1, 1)            # allStreamsSameTimeFraming
        w.write(0, 6)            # numSubFrames
        w.write(0, 4)            # numProgram
        w.write(0, 3)            # numLayer
        if version == 1:
            write_latm_value(w, len(asc_bits))  # ascLen, in bits
        for b in asc_bits:
            w.write(b, 1)        # audioSpecificConfig
        w.write(0, 3)            # frameLengthType = 0 (byte length in payload)
        w.write(0, 8)            # latmBufferFullness
        w.write(0, 1)            # otherDataPresent = 0
        w.write(0, 1)            # crcCheckPresent = 0
    else:
        w.write(1, 1)            # useSameStreamMux = 1
    # payloadLengthInfo (frameLengthType == 0)
    n = len(payload)
    while n >= 255:
        w.write(255, 8)
        n -= 255
    w.write(n, 8)
    # payload
    for byte in payload:
        w.write(byte, 8)
    w.align()
    return w.bytes()


def loas_frame(mux):
    w = BitWriter()
    w.write(0x2B7, 11)           # LOAS sync word
    w.write(len(mux), 13)        # AudioMuxElement length in bytes
    hdr = w.bytes()
    assert len(hdr) == 3
    return hdr + mux


def main():
    src, dst, version = sys.argv[1], sys.argv[2], int(sys.argv[3])
    assert version in (0, 1)
    data = open(src, "rb").read()
    frames = parse_adts(data)
    profile, sf_index, chan_config, _ = frames[0]
    asc = audio_specific_config(profile, sf_index, chan_config)

    out = bytearray()
    for i, (_, _, _, payload) in enumerate(frames):
        mux = mux_element(payload, asc, version, with_config=(i == 0))
        out += loas_frame(mux)
    open(dst, "wb").write(bytes(out))
    print("wrote %s: %d frames, %d bytes, audioMuxVersion %d"
          % (dst, len(frames), len(out), version))


if __name__ == "__main__":
    main()
