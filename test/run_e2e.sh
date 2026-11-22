#!/usr/bin/env bash
#
# End-to-end WAV output verification:
#   1. generate channel probe / fixed-layout AAC samples with ffmpeg
#   2. decode them with the freshly built faad CLI
#   3. run an independent (stdlib-only) WAV header/order parser
#
# Requires: ffmpeg, python3. The C unit tests in CTest need neither.
set -euo pipefail

ROOT=$(cd "$(dirname "$0")/.." && pwd)
FAAD="${1:-$ROOT/build/faad}"
WORK="$ROOT/test/work-e2e"
SAMPLES="$ROOT/test/samples-e2e"

for tool in ffmpeg python3; do
    command -v "$tool" >/dev/null || { echo "SKIP: $tool not found"; exit 0; }
done
[ -x "$FAAD" ] || { echo "SKIP: $FAAD not built"; exit 0; }

rm -rf "$WORK" "$SAMPLES"
mkdir -p "$WORK" "$SAMPLES"

# fixed-layout samples (MP4 carries ISO channelConfiguration properly)
ffmpeg -hide_banner -loglevel error -f lavfi -i "sine=frequency=440:sample_rate=48000:duration=1" -ac 6 -c:a aac -b:a 256k -y "$SAMPLES/5.1.m4a"
ffmpeg -hide_banner -loglevel error -f lavfi -i "sine=frequency=440:sample_rate=48000:duration=1" -ac 8 -c:a aac -b:a 384k -y "$SAMPLES/7.1.m4a"
ffmpeg -hide_banner -loglevel error -f lavfi -i "sine=frequency=440:sample_rate=48000:duration=1" -ac 2 -c:a aac -b:a 128k -y "$SAMPLES/stereo.m4a"
ffmpeg -hide_banner -loglevel error -f lavfi -i "sine=frequency=440:sample_rate=48000:duration=1" -ac 1 -c:a aac -b:a 96k  -y "$SAMPLES/mono.m4a"
ffmpeg -hide_banner -loglevel error -f lavfi -i "sine=frequency=440:sample_rate=48000:duration=1" -ac 6 -c:a aac -b:a 256k -f adts -y "$SAMPLES/5.1.aac"
ffmpeg -hide_banner -loglevel error -f lavfi -i "sine=frequency=440:sample_rate=48000:duration=1" -ac 8 -c:a aac -b:a 384k -f adts -y "$SAMPLES/7.1.aac"
ffmpeg -hide_banner -loglevel error -f lavfi -i "sine=frequency=440:sample_rate=48000:duration=1" -ac 4 -c:a aac -b:a 192k -y "$SAMPLES/quad.m4a"

# per-channel probe tones (LFE gets 60 Hz: AAC band-limits the LFE)
python3 -I "$ROOT/test/make_probes.py" "$SAMPLES"
ffmpeg -hide_banner -loglevel error -i "$SAMPLES/probe6.wav" -c:a aac -b:a 256k -y "$SAMPLES/probe6.m4a"
ffmpeg -hide_banner -loglevel error -i "$SAMPLES/probe8.wav" -c:a aac -b:a 384k -y "$SAMPLES/probe8.m4a"

cd "$WORK"
"$FAAD" -q -o 5.1.wav     "$SAMPLES/5.1.m4a"
"$FAAD" -q -o 7.1.wav     "$SAMPLES/7.1.m4a"
"$FAAD" -q -o stereo.wav  "$SAMPLES/stereo.m4a"
"$FAAD" -q -o mono.wav    "$SAMPLES/mono.m4a"
"$FAAD" -q -o 51adts.wav  "$SAMPLES/5.1.aac"
"$FAAD" -q -o 71adts.wav  "$SAMPLES/7.1.aac"
"$FAAD" -q -o quad.wav    "$SAMPLES/quad.m4a"
"$FAAD" -q -o 5.1probe.wav "$SAMPLES/probe6.m4a"
"$FAAD" -q -o 7.1probe.wav "$SAMPLES/probe8.m4a"
"$FAAD" -q -f 2 -o stereo.pcm "$SAMPLES/stereo.m4a"

python3 -I "$ROOT/test/wav_check.py" "$WORK"
