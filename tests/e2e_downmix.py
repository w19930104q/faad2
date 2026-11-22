#!/usr/bin/env python3
"""End-to-end downmix verification.

Runs tests/e2e_harness (two deterministic libfaad decode passes of the
same ADTS file: multichannel and stereo) and independently recomputes
every stereo sample from the multichannel PCM plus the channel
position table emitted by the decoder. The no-peak-control and
peak-scaled results must match faad bit-for-bit (binary32); clip results
must match the documented hard-clip rule.

Usage: python3 -I tests/e2e_downmix.py <harness> <samples_dir> <work_dir>
"""
import os
import struct
import subprocess
import sys

F32 = lambda x: struct.unpack('f', struct.pack('f', x))[0]
PACK = lambda x: struct.pack('f', x)

# neaacdec.h channel roles
ROLE = {1: 'C', 2: 'L', 3: 'R', 4: 'SL', 5: 'SR',
        6: 'BL', 7: 'BR', 8: 'BC', 9: 'LFE'}


def c_lit(text):
    return F32(float(text))


RSQRT2 = c_lit('0.7071067811865475244')
HALF = c_lit('0.5')
DM_MUL_LEGACY = c_lit('0.3203772410170407')


def read_f32(path):
    data = open(path, 'rb').read()
    return list(struct.unpack('<%df' % (len(data) // 4), data))


def build_terms(pos):
    """Same term order and weights as downmix_build_plan()."""
    left, right = [], []
    pairs, bc = 0, False
    n = len(pos)
    for idx in range(n):
        if pos[idx] == 0:
            continue  # unknown role: ignored by the C plan builder too
        role = ROLE[pos[idx]]
        if role == 'L':
            left.append((idx, F32(1.0)))
        if role == 'R':
            right.append((idx, F32(1.0)))
    for idx in range(n):
        if pos[idx] == 0:
            continue
        role = ROLE[pos[idx]]
        if role == 'C':
            left.append((idx, RSQRT2)); right.append((idx, RSQRT2))
        elif role in ('SL', 'BL'):
            left.append((idx, RSQRT2)); pairs += 1
        elif role in ('SR', 'BR'):
            right.append((idx, RSQRT2))
        elif role == 'BC':
            left.append((idx, HALF)); right.append((idx, HALF)); bc = True
    legacy = pairs == 1 and not bc
    return left, right, legacy, bc


def headroom(left, right):
    def s_of(terms):
        s = F32(0.0)
        for _, w in terms:
            s = F32(s + (F32(-w) if w < 0 else w))
        return s
    s = max(s_of(left), s_of(right))
    return F32(F32(1.0) / s)


def mix(terms, chans, i, gain, profile, peak):
    acc = F32(0.0)
    for idx, w in terms:
        acc = F32(acc + F32(chans[idx][i] * w))
    acc = F32(acc * gain)
    # ITU + CLIP only; threshold is the internal +/-32768 time_out scale
    if profile == 1 and peak == 1:
        if acc > F32(32768.0):
            acc = F32(32768.0)
        elif acc < F32(-32768.0):
            acc = F32(-32768.0)
    return acc


def main():
    harness, samples, work = sys.argv[1:4]
    layouts = ('5.1', '6.1', '7.1')
    failures = 0
    for layout in layouts:
        prefix = os.path.join(work, 'e2e_' + layout.replace('.', ''))
        for profile in (0, 1):
            for peak in (0, 1, 2):
                subprocess.run([harness, os.path.join(samples, layout + '.aac'),
                                str(profile), str(peak), prefix], check=True)
                frames, ch_m, ch_d, frames_d = map(
                    int, open(prefix + '_meta.txt').read().split())
                assert ch_d == 2 and frames == frames_d
                pos = list(open(prefix + '_pos.u8', 'rb').read())[:ch_m]
                multi = read_f32(prefix + '_multi.pcm')
                got = read_f32(prefix + '_dmx.pcm')
                total = frames * 1024
                chans = [[multi[f * ch_m + c] for f in range(total)]
                         for c in range(ch_m)]
                left, right, legacy, bc = build_terms(pos)
                hdr = headroom(left, right)
                if profile == 0:
                    gain = DM_MUL_LEGACY if legacy else hdr
                    if peak == 2:
                        gain = hdr
                else:
                    gain = hdr if peak == 2 else F32(1.0)
                bad = 0
                for i in range(total):
                    e_l = mix(left, chans, i, gain, profile, peak)
                    e_r = mix(right, chans, i, gain, profile, peak)
                    if PACK(got[i * 2]) != PACK(e_l):
                        bad += 1
                    if PACK(got[i * 2 + 1]) != PACK(e_r):
                        bad += 1
                tag = '%s profile=%d peak=%d' % (layout, profile, peak)
                if bad:
                    failures += 1
                    print('FAIL', tag, 'mismatches', bad)
                else:
                    print('OK  ', tag, 'frames', total)
    print('E2E failures:', failures)
    sys.exit(1 if failures else 0)


if __name__ == '__main__':
    main()
