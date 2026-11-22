#!/usr/bin/env bash
# Regenerates the MP4 fixtures used by the chapter tests.
# Requires ffmpeg with the native AAC encoder and mp4 muxer.
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
DATA="$HERE/../data"
mkdir -p "$DATA"

# 3 seconds of stereo 44.1 kHz tone; small and fully deterministic.
SRC=(-f lavfi -i "sine=frequency=440:sample_rate=44100:duration=3")
ENC=(-c:a aac -b:a 64k)

# 1) File with a fixed chapter table (incl. non-ASCII UTF-8 titles).
ffmpeg -hide_banner -loglevel error -y "${SRC[@]}" -f ffmetadata -i "$HERE/chapters.ffmeta" \
  -map_metadata 1 "${ENC[@]}" -metadata title="Fixture Album" \
  -f mp4 "$DATA/chapters.mp4"

# 2) Same kind of content, no chapters at all.
ffmpeg -hide_banner -loglevel error -y "${SRC[@]}" "${ENC[@]}" \
  -metadata title="No Chapters Fixture" \
  -f mp4 "$DATA/nochapters.mp4"

echo "fixtures written to $DATA"
