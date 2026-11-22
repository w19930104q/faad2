#!/usr/bin/env python3
# Compare two raw signed 16-bit little-endian PCM files (float vs fixed
# decoder output) against the quantization bound documented in
# docs/fixed-point-accuracy.md:
#
#   per-sample:  |float[n] - fixed[n]| <= B_MAX = Q + E_PIPE = 1 + 3 = 4 LSB
#   aggregate:   rms(float - fixed)   <= B_RMS = 1.5 LSB
#
# Usage: compare_pcm.py <float.pcm> <fixed.pcm>
# Exit status: 0 = within bound, 1 = bound violated or I/O error.

import math
import struct
import sys

Q = 1        # output quantization allowance (both sides round to s16)
E_PIPE = 3   # fixed-point pipeline accumulation allowance
B_MAX = Q + E_PIPE
B_RMS = 1.5


def read_pcm(path):
    with open(path, "rb") as f:
        data = f.read()
    if len(data) % 2 != 0:
        raise ValueError(f"{path}: odd byte count {len(data)}, not s16 PCM")
    return struct.unpack(f"<{len(data) // 2}h", data)


def main(argv):
    if len(argv) != 3:
        print(__doc__)
        return 1
    a = read_pcm(argv[1])
    b = read_pcm(argv[2])
    if len(a) != len(b):
        print(f"FAIL: length mismatch: {len(a)} vs {len(b)} samples")
        return 1
    if len(a) == 0:
        print("FAIL: empty output")
        return 1

    max_diff = 0
    sum_sq = 0
    for x, y in zip(a, b):
        d = abs(x - y)
        if d > max_diff:
            max_diff = d
        sum_sq += (x - y) * (x - y)
    rms = math.sqrt(sum_sq / len(a))

    ok = max_diff <= B_MAX and rms <= B_RMS
    status = "PASS" if ok else "FAIL"
    print(f"{status}: samples={len(a)} max_diff={max_diff} (bound {B_MAX}) "
          f"rms_diff={rms:.4f} (bound {B_RMS})")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
