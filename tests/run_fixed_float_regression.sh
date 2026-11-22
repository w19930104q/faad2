#!/bin/sh
# Float/fixed consistency regression.
#
# Decodes every sample in the sample set with the floating point CLI and the
# fixed point CLI and checks that both outputs agree within the quantization
# bound documented in docs/fixed-point-accuracy.md.
#
# Usage: run_fixed_float_regression.sh <faad> <faad_fixed> <samples_dir>
# Exit status: 0 = all samples within bound, 1 = any violation.

set -u

FAAD_FLOAT="$1"
FAAD_FIXED="$2"
SAMPLES="$3"
HERE="$(cd "$(dirname "$0")" && pwd)"

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

fail=0
count=0

for sample in "$SAMPLES"/*.aac "$SAMPLES"/*.m4a; do
    [ -e "$sample" ] || continue
    name="$(basename "$sample")"
    count=$((count + 1))

    if ! "$FAAD_FLOAT" -q -f 2 -b 1 -o "$TMP/$name.float.pcm" "$sample" >/dev/null 2>&1; then
        echo "FAIL: $name: floating point decoder exited with an error"
        fail=1
        continue
    fi
    if ! "$FAAD_FIXED" -q -f 2 -b 1 -o "$TMP/$name.fixed.pcm" "$sample" >/dev/null 2>&1; then
        echo "FAIL: $name: fixed point decoder exited with an error"
        fail=1
        continue
    fi

    printf '%s: ' "$name"
    if ! python3 -I "$HERE/compare_pcm.py" "$TMP/$name.float.pcm" "$TMP/$name.fixed.pcm"; then
        fail=1
    fi
done

if [ "$count" -eq 0 ]; then
    echo "FAIL: no samples found in $SAMPLES"
    exit 1
fi

if [ "$fail" -eq 0 ]; then
    echo "fixed/float consistency: all $count samples within the quantization bound"
else
    echo "fixed/float consistency: FAILURES detected"
fi
exit "$fail"
