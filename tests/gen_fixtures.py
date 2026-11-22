#!/usr/bin/env python3
"""Generate the MP4 edit-list test fixtures in tests/fixtures/.

Pipeline:
  1. synthesize a 0.25 s 440 Hz mono tone (11025 samples @ 44100 Hz);
  2. encode it to AAC/M4A with ffmpeg (12 AAC frames, mdhd duration 12049,
     ffmpeg writes a single-entry edit list: seg=250 / media_time=1024);
  3. rewrite the container metadata (no re-encoding, media data untouched):

     elst_offset.m4a   edit list with a leading empty edit (0.1 s offset):
                         {seg=100, media_time=-1}, {seg=250, media_time=1024}
     elst_gapless.m4a  edit list AND an iTunSMPB gapless tag that disagree;
                       the edit list must win:
                         elst: {seg=200, media_time=2048}
                         iTunSMPB: delay=1024 padding=239 valid=11025
     gapless_only.m4a  no edit list, iTunSMPB only (delay=1024, valid=11025)
     noelst.m4a        no edit list, no iTunSMPB (baseline behaviour)

Usage: python3 tests/gen_fixtures.py
Requires ffmpeg on PATH.
"""
import os
import struct
import subprocess
import sys
import tempfile
import wave

HERE = os.path.dirname(os.path.abspath(__file__))
FIXTURES = os.path.join(HERE, "fixtures")

SAMPLE_RATE = 44100
TONE_SAMPLES = 11025  # 0.25 s


def make_tone(path):
    import math
    w = wave.open(path, "wb")
    w.setnchannels(1)
    w.setsampwidth(2)
    w.setframerate(SAMPLE_RATE)
    w.writeframes(b"".join(
        struct.pack("<h", int(12000 * math.sin(2 * math.pi * 440 * i / SAMPLE_RATE)))
        for i in range(TONE_SAMPLES)))
    w.close()


def find_atom(data, name, start=0, end=None):
    """Return (offset, size) of the first atom named `name` in data[start:end]."""
    if end is None:
        end = len(data)
    off = start
    while off + 8 <= end:
        size, atom = struct.unpack(">I4s", data[off:off + 8])
        if atom == name:
            return off, size
        off += size
    raise ValueError("atom %r not found" % name)


def find_path(data, *names):
    """Walk a container path like ('moov', 'trak'), returning (offset, size)
    of the final atom's *content* and the atom itself."""
    off, end = 0, len(data)
    for name in names:
        aoff, asize = find_atom(data, name, off, end)
        hdr = 8
        off, end = aoff + hdr, aoff + asize
        if name == b"meta":
            off += 4  # fullbox version/flags
    return off, end


def build_elst(entries, version=0):
    """entries: list of (segment_duration, media_time)"""
    body = struct.pack(">B3sI", version, b"\0\0\0", len(entries))
    for seg, mt in entries:
        if version == 1:
            body += struct.pack(">QqHH", seg, mt, 1, 0)
        else:
            body += struct.pack(">IiHH", seg, mt, 1, 0)
    return struct.pack(">I4s", 8 + len(body), b"elst") + body


def build_edts(entries, version=0):
    elst = build_elst(entries, version)
    return struct.pack(">I4s", 8 + len(elst), b"edts") + elst


def build_itunsmpb(delay, padding, valid):
    payload = ("iTunSMPB 00000000 %08X %08X %016X" % (delay, padding, valid)).encode()
    mean = struct.pack(">I4s", 28, b"mean") + b"\0\0\0\0" + b"com.apple.iTunes"
    name = struct.pack(">I4s", 20, b"name") + b"\0\0\0\0" + b"iTunSMPB"
    data = struct.pack(">I4s", 16 + len(payload), b"data") + b"\0\0\0\1" + b"\0\0\0\0" + payload
    body = mean + name + data
    return struct.pack(">I4s", 8 + len(body), b"----") + body


def splice(data, off, old_len, new):
    """Replace data[off:off+old_len] with `new`, fixing the sizes of the
    enclosing atoms (which must be moov/trak/... chain we specify)."""
    return data[:off] + new + data[off + old_len:]


def adjust_size(data, off, delta):
    size = struct.unpack(">I", data[off:off + 4])[0]
    return data[:off] + struct.pack(">I", size + delta) + data[off + 4:]


def rewrite(base, edts_blob, itunsmpb_blob):
    data = base
    moov_off, moov_size = find_atom(data, b"moov")
    mdat_off, _ = find_atom(data, b"mdat")
    assert mdat_off < moov_off, "moov must come after mdat (no faststart)"

    # --- replace or remove the edit list inside the (audio) trak ---
    trak_off, trak_size = find_atom(data, b"trak", moov_off + 8, moov_off + moov_size)
    try:
        edts_off, edts_size = find_atom(data, b"edts", trak_off + 8, trak_off + trak_size)
    except ValueError:
        edts_off, edts_size = trak_off + 8, 0  # insert at trak start
    old_edts_size = edts_size
    new_blob = edts_blob or b""
    data = splice(data, edts_off, old_edts_size, new_blob)
    delta = len(new_blob) - old_edts_size
    data = adjust_size(data, trak_off, delta)
    data = adjust_size(data, moov_off, delta)

    # --- optionally append an iTunSMPB freeform tag to ilst ---
    if itunsmpb_blob is not None:
        moov_off, moov_size = find_atom(data, b"moov")
        udta_off, udta_size = find_atom(data, b"udta", moov_off + 8, moov_off + moov_size)
        meta_off, meta_size = find_atom(data, b"meta", udta_off + 8, udta_off + udta_size)
        ilst_off, ilst_size = find_atom(data, b"ilst", meta_off + 12, meta_off + meta_size)
        data = splice(data, ilst_off + ilst_size, 0, itunsmpb_blob)
        delta = len(itunsmpb_blob)
        for off in (ilst_off, meta_off, udta_off, moov_off):
            data = adjust_size(data, off, delta)

    return data


def main():
    os.makedirs(FIXTURES, exist_ok=True)
    with tempfile.TemporaryDirectory() as tmp:
        tone = os.path.join(tmp, "tone.wav")
        base = os.path.join(tmp, "base.m4a")
        make_tone(tone)
        subprocess.run(["ffmpeg", "-v", "error", "-y", "-i", tone,
                        "-c:a", "aac", "-b:a", "96k", base], check=True)
        data = open(base, "rb").read()

    # sanity: base file must match what the expectations below assume
    moov_off, moov_size = find_atom(data, b"moov")
    trak_off, trak_size = find_atom(data, b"trak", moov_off + 8, moov_off + moov_size)
    mdia = find_path(data, b"moov", b"trak", b"mdia")
    mdhd_off, _ = find_atom(data, b"mdhd", *mdia)
    ts, dur = struct.unpack(">II", data[mdhd_off + 20:mdhd_off + 28])
    assert (ts, dur) == (44100, 12049), (ts, dur)

    padding = 12 * 1024 - 1024 - TONE_SAMPLES  # 239
    assert padding == 239

    out = {
        # leading empty edit: 0.1 s (100/1000) presentation offset
        "elst_offset.m4a": rewrite(data, build_edts([(100, -1), (250, 1024)]), None),
        # same timeline stored as a version-1 (64-bit) edit list
        "elst_offset_v1.m4a": rewrite(data, build_edts([(100, -1), (250, 1024)], version=1), None),
        # empty edit + two contiguous media entries (0.1 s media each)
        "elst_multientry.m4a": rewrite(data, build_edts([(100, -1), (100, 1024), (100, 5434)]), None),
        # trailing empty edit: 0.1 s media followed by 0.1 s of silence
        "elst_trailing.m4a": rewrite(data, build_edts([(100, 1024), (100, -1)]), None),
        # edit list disagrees with iTunSMPB: edit list must win
        "elst_gapless.m4a": rewrite(data, build_edts([(200, 2048)]),
                                    build_itunsmpb(1024, padding, TONE_SAMPLES)),
        # gapless tag only, no edit list
        "gapless_only.m4a": rewrite(data, None, build_itunsmpb(1024, padding, TONE_SAMPLES)),
        # neither edit list nor gapless tag
        "noelst.m4a": rewrite(data, None, None),
    }
    for name, blob in out.items():
        with open(os.path.join(FIXTURES, name), "wb") as f:
            f.write(blob)
        print("wrote %s (%d bytes)" % (name, len(blob)))


if __name__ == "__main__":
    main()
