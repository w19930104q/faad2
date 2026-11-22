#!/usr/bin/env python3
"""Independent WAVE header parser and hardcoded assertions for faad output.

Parses RIFF/WAVE chunks from scratch (no audio libraries), validates every
fmt / WAVEFORMATEXTENSIBLE field, checks chunk-size consistency, and for
multi-channel content verifies the channel mask and the physical channel
ordering against per-channel probe tones.

Run with: python3 -I wav_check.py
"""
import struct
import sys
import wave

PCM_GUID = bytes.fromhex("0100000000001000800000aa00389b71")
FLOAT_GUID = bytes.fromhex("0300000000001000800000aa00389b71")

# Microsoft speaker bits
FL, FR, FC, LFE, BL, BR, BC, SL, SR = (
    0x1, 0x2, 0x4, 0x8, 0x10, 0x20, 0x100, 0x200, 0x400)

MASK_51 = FL | FR | FC | LFE | BL | BR          # 0x3F
MASK_71 = FL | FR | FC | LFE | BL | BR | SL | SR  # 0x63F
# ISO/IEC 14496-3 channelConfiguration 4: front L/R/C + back center
MASK_CFG4 = FL | FR | FC | BC                   # 0x107


class WavInfo:
    def __init__(self, path):
        with open(path, "rb") as f:
            self.blob = f.read()
        self._parse()

    def _parse(self):
        b = self.blob
        assert b[:4] == b"RIFF", "missing RIFF"
        self.riff_size = struct.unpack_from("<I", b, 4)[0]
        assert b[8:12] == b"WAVE", "missing WAVE"
        assert self.riff_size == len(b) - 8, "RIFF size inconsistent with file"
        off = 12
        self.fmt = None
        self.data_size = None
        while off + 8 <= len(b):
            cid, csize = struct.unpack_from("<4sI", b, off)
            body = off + 8
            if cid == b"fmt ":
                self.fmt = self._parse_fmt(b, body, csize)
            elif cid == b"data":
                self.data_size = csize
                assert csize <= len(b) - body, "data chunk past end of file"
            off = body + csize + (csize & 1)  # chunks are word-aligned
        assert self.fmt is not None, "no fmt chunk"
        assert self.data_size is not None, "no data chunk"
        # data payload must be an exact number of audio frames
        f = self.fmt
        assert self.data_size % f["blockalign"] == 0
        self.nframes = self.data_size // f["blockalign"]

    @staticmethod
    def _parse_fmt(b, body, csize):
        tag, nch, sr, avgbr, blockalign, bits = struct.unpack_from(
            "<HHIIHH", b, body)
        fmt = {"tag": tag, "nchannels": nch, "samplerate": sr,
               "avgbytespersec": avgbr, "blockalign": blockalign,
               "bitspersample": bits, "cbSize": None, "ext": None}
        p = body + 16
        if csize >= 18:
            (fmt["cbSize"],) = struct.unpack_from("<H", b, p)
            p += 2
        if tag == 0xFFFE:
            assert csize == 40, "extensible fmt chunk must be 40 bytes"
            assert fmt["cbSize"] == 22
            validbits, mask = struct.unpack_from("<HI", b, p)
            guid = b[p + 6: p + 22]
            fmt["ext"] = {"validbitspersample": validbits,
                          "channelmask": mask, "subformat": guid}
        else:
            assert csize == 16, "plain fmt chunk must be 16 bytes"
        return fmt

    def check_common(self, expect_sr, expect_ch, expect_bits, float_mode=False):
        f = self.fmt
        assert f["nchannels"] == expect_ch
        assert f["samplerate"] == expect_sr
        assert f["bitspersample"] == expect_bits
        bytes_per_sample = expect_bits // 8
        assert f["blockalign"] == bytes_per_sample * expect_ch
        assert f["avgbytespersec"] == expect_sr * f["blockalign"]
        if f["tag"] == 0xFFFE:
            ext = f["ext"]
            assert ext["validbitspersample"] == expect_bits
            assert ext["subformat"] == (FLOAT_GUID if float_mode else PCM_GUID)
            # popcount of the mask must equal the channel count
            assert bin(ext["channelmask"]).count("1") == expect_ch


def expect_plain(path, expect_sr, expect_ch, expect_bits, float_mode=False):
    w = WavInfo(path)
    w.check_common(expect_sr, expect_ch, expect_bits, float_mode)
    f = w.fmt
    assert f["tag"] == (0x0003 if float_mode else 0x0001), \
        "mono/stereo must use a plain format tag"
    assert f["cbSize"] is None, "plain header must carry no extension"
    assert len(w.blob) == 44 + w.data_size, "plain header must be exactly 44 bytes"
    return w


def expect_extensible(path, expect_sr, expect_ch, expect_bits, expect_mask,
                      float_mode=False):
    w = WavInfo(path)
    w.check_common(expect_sr, expect_ch, expect_bits, float_mode)
    f = w.fmt
    assert f["tag"] == 0xFFFE, "multi-channel WAV must be WAVEFORMATEXTENSIBLE"
    assert f["ext"]["channelmask"] == expect_mask
    assert len(w.blob) == 68 + w.data_size, "extensible header must be 68 bytes"
    # stdlib wave module (independent parser) must read the same fields
    with wave.open(path, "rb") as wf:
        assert wf.getnchannels() == expect_ch
        assert wf.getframerate() == expect_sr
        assert wf.getsampwidth() == expect_bits // 8
        assert wf.getnframes() == w.nframes
    return w


def dominant_freq(path, nch, bits):
    """Return the dominant frequency of each output channel (probe tones)."""
    import math
    w = WavInfo(path)
    bps = bits // 8
    n = w.nframes
    sr = w.fmt["samplerate"]
    freqs = []
    for ch in range(nch):
        vals = []
        base = 68 if w.fmt["tag"] == 0xFFFE else 44
        for frame in range(n):
            off = base + (frame * nch + ch) * 2
            vals.append(struct.unpack_from("<h", w.blob, off)[0])
        # DFT magnitude (sin+cos) on a 10 Hz candidate grid
        best, best_power = 0, 0.0
        for cand in range(50, 1001, 10):
            s_in = s_quad = c_quad = 0.0
            for i, v in enumerate(vals):
                t = i / sr
                s_in += v * math.sin(2 * math.pi * cand * t)
                c_quad += v * math.cos(2 * math.pi * cand * t)
            power = s_in * s_in + c_quad * c_quad
            if power > best_power:
                best, best_power = cand, power
        freqs.append(best)
    return freqs


def main():
    failures = []

    def check(name, fn):
        try:
            fn()
        except AssertionError as e:
            failures.append((name, str(e)))
        else:
            print("PASS %s" % name)

    work = sys.argv[1]

    # ---- hardcoded header assertions: fixed 5.1 / 7.1 samples ----
    def t_51():
        w = expect_extensible(work + "/5.1.wav", 48000, 6, 16, MASK_51)
        assert w.fmt["ext"]["channelmask"] == 0x3F
    check("5.1 m4a extensible header, mask 0x3F", t_51)

    def t_51_adts():
        expect_extensible(work + "/51adts.wav", 48000, 6, 16, MASK_51)
    check("5.1 ADTS extensible header, mask 0x3F", t_51_adts)

    def t_71():
        w = expect_extensible(work + "/7.1.wav", 48000, 8, 16, MASK_71)
        assert w.fmt["ext"]["channelmask"] == 0x63F
    check("7.1 m4a extensible header, mask 0x63F", t_71)

    def t_71_adts():
        # ADTS has no fixed-table entry for 8 channels; the library reports
        # unknown positions, so the writer falls back to a plain header
        # instead of emitting a misleading mask. The file must stay valid.
        w = WavInfo(work + "/71adts.wav")
        assert w.fmt["nchannels"] == 8
        assert w.fmt["tag"] == 0x0001, "unknown layout must fall back to PCM"
        assert w.fmt["cbSize"] is None
        assert len(w.blob) == 44 + w.data_size
    check("7.1 ADTS unknown-layout safe plain-PCM fallback", t_71_adts)

    # ---- mono / stereo must stay plain PCM (no regression) ----
    check("mono plain PCM 44-byte header",
          lambda: expect_plain(work + "/mono.wav", 48000, 1, 16))
    check("stereo plain PCM 44-byte header",
          lambda: expect_plain(work + "/stereo.wav", 48000, 2, 16))

    # ---- ISO fixed-table 4-channel layout (front L/R/C + back center) ----
    check("4ch config-4 extensible header, mask 0x107",
          lambda: expect_extensible(work + "/quad.wav", 48000, 4, 16,
                                    MASK_CFG4))

    # ---- channel ordering: probe tones, WAVE order after reorder ----
    def t_order_51():
        # WAVE-order probes: FL,FR,FC,LFE,BL,BR -> 100,200,300,60,500,600 Hz
        got = dominant_freq(work + "/5.1probe.wav", 6, 16)
        assert got == [100, 200, 300, 60, 500, 600], got
    check("5.1 channel order matches bitstream/WAVE layout", t_order_51)

    def t_order_71():
        # FL,FR,FC,LFE,BL,BR,SL,SR -> 100,200,300,60,500,600,700,800
        got = dominant_freq(work + "/7.1probe.wav", 8, 16)
        assert got == [100, 200, 300, 60, 500, 600, 700, 800], got
    check("7.1 channel order matches bitstream/WAVE layout", t_order_71)

    # ---- RAW output must be headerless and untouched ----
    def t_raw():
        import os
        raw = work + "/stereo.pcm"
        size = os.path.getsize(raw)
        with open(raw, "rb") as f:
            assert f.read(4) != b"RIFF", "RAW output must not carry a WAV header"
        # 16-bit stereo: size must be an exact frame multiple, no 44/68 prefix
        assert size % 4 == 0 and size > 44
    check("RAW PCM output has no WAV header", t_raw)

    if failures:
        for name, msg in failures:
            print("FAIL %s: %s" % (name, msg))
        sys.exit(1)
    print("\nall checks passed")


if __name__ == "__main__":
    main()
