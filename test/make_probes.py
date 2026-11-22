#!/usr/bin/env python3
"""Generate multi-channel probe-tone WAVs (stdlib only).

Each channel carries a distinct sine so channel ordering can be verified
after an encode/decode round trip. Channels are written in Microsoft
WAVEFORMATEXTENSIBLE order.
"""
import math
import struct
import wave

SR = 48000
DUR = 0.5

LAYOUTS = {
    # LFE is band-limited in AAC (~120 Hz), so its probe is 60 Hz.
    "probe6":  (6, (100, 200, 300, 60, 500, 600)),        # FL FR FC LFE BL BR
    "probe8":  (8, (100, 200, 300, 60, 500, 600, 700, 800)),  # FL FR FC LFE BL BR SL SR
    "quad":    (4, (100, 200, 500, 600)),                 # FL FR BL BR
}


def write_wav(path, nch, freqs):
    n = int(SR * DUR)
    amp = 12000
    with wave.open(path, "wb") as w:
        w.setnchannels(nch)
        w.setsampwidth(2)
        w.setframerate(SR)
        frames = bytearray()
        for i in range(n):
            for f in freqs:
                v = int(amp * math.sin(2 * math.pi * f * i / SR))
                frames += struct.pack("<h", v)
        w.writeframes(bytes(frames))


if __name__ == "__main__":
    import sys
    outdir = sys.argv[1]
    for name, (nch, freqs) in LAYOUTS.items():
        write_wav("%s/%s.wav" % (outdir, name), nch, freqs)
        print("wrote %s/%s.wav (%d ch)" % (outdir, name, nch))
