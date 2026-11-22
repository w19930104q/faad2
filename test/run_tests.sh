#!/bin/sh
# Acceptance tests for the FAAD2 LATM library interface.
#
#   test/run_tests.sh [builddir]
#
# Expects the library and the faad frontend to be built already
# (cmake --build <builddir>). Uses the pregenerated samples in
# test/samples/ (regenerate with: python3 test/adts2latm.py ... and
# python3 test/gen_malformed.py ..., see test/README.md).

set -e

ROOT=$(cd "$(dirname "$0")/.." && pwd)
BUILD=${1:-$ROOT/build}
CC=${CC:-cc}
SAMPLES=$ROOT/test/samples
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT

if [ ! -f "$BUILD/libfaad.so" ] && [ ! -f "$BUILD/libfaad.a" ]; then
    echo "no built library found in $BUILD" >&2
    exit 1
fi

# build the test harness
if [ -f "$BUILD/libfaad.a" ]; then
    LIB="$BUILD/libfaad.a"
    RPATH=
else
    LIB="-L$BUILD -lfaad"
    RPATH="-Wl,-rpath,$BUILD"
fi
$CC -O1 -I "$ROOT/include" "$ROOT/test/latm_test.c" -o "$WORK/latm_test" \
    $LIB $RPATH -lm

fail() { echo "FAIL: $*" >&2; exit 1; }

# 1. fixed LATM samples: library LATM entry == existing entry == frontend
for v in v0 v1; do
    sample=$SAMPLES/tone_$v.loas
    [ -f "$sample" ] || fail "missing sample $sample"

    "$WORK/latm_test" decode-legacy "$sample" "$WORK/legacy.pcm" 2>/dev/null \
        || fail "legacy entry decode ($v)"
    "$WORK/latm_test" decode-latm "$sample" "$WORK/latm.pcm" 2>/dev/null \
        || fail "LATM entry decode ($v)"
    cmp -s "$WORK/legacy.pcm" "$WORK/latm.pcm" \
        || fail "LATM entry != existing entry ($v)"

    "$BUILD/faad" -o "$WORK/frontend.wav" "$sample" 2>/dev/null \
        || fail "frontend decode ($v)"
    python3 -I - "$WORK/frontend.wav" "$WORK/legacy.pcm" <<'EOF' || fail "frontend WAV parse ($v)"
import struct, sys
d = open(sys.argv[1], 'rb').read()
assert d[:4] == b'RIFF' and d[8:12] == b'WAVE'
pos = 12
data = None
while pos < len(d):
    cid, sz = d[pos:pos+4], struct.unpack('<I', d[pos+4:pos+8])[0]
    if cid == b'data':
        data = d[pos+8:pos+8+sz]
        break
    pos += 8 + sz + (sz & 1)
pcm = open(sys.argv[2], 'rb').read()
if data != pcm:
    sys.exit(1)
EOF

    # 2. arbitrary byte-slice feeding must not change the output
    for n in 1 2 3 5 7 13 64 255 1024 4096 999983; do
        "$WORK/latm_test" decode-latm "$sample" "$WORK/chunk.pcm" $n 2>/dev/null \
            || fail "chunked decode ($v, $n)"
        cmp -s "$WORK/legacy.pcm" "$WORK/chunk.pcm" \
            || fail "chunk $n changed output ($v)"
    done
    echo "sample $v: entries identical, chunk feeding identical"
done

# 3. malformed headers: deterministic errors
for f in m_garbage m_versiona m_programs m_celp m_framelen0 m_trunc; do
    "$WORK/latm_test" expect-init-fail "$SAMPLES/$f.loas" >/dev/null 2>&1 \
        || fail "no deterministic init error for $f"
done
"$WORK/latm_test" expect-decode-error "$SAMPLES/m_badmid.loas" >/dev/null 2>&1 \
    || fail "no deterministic decode error for m_badmid"
echo "malformed headers: deterministic errors"

echo "ALL LATM TESTS PASSED"
