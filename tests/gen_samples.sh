#!/bin/sh
# Regenerates the fixed AAC sample set used by the float/fixed consistency
# regression (tests/run_fixed_float_regression.sh).
#
# The generated .aac/.m4a files are committed to the repository so the
# regression does not depend on ffmpeg at test time. Re-run this script only
# when the sample set must change; every signal below is deterministic
# (fixed seeds / analytic expressions).
#
# Requires: ffmpeg with the native AAC encoder.

set -e

OUT="$(cd "$(dirname "$0")" && pwd)/samples"
mkdir -p "$OUT"

FF="ffmpeg -hide_banner -loglevel error -y"

# 1. 440 Hz sine, mono, 44.1 kHz, 2 s, modest bitrate.
$FF -f lavfi -i "sine=frequency=440:sample_rate=44100:duration=2" \
    -c:a aac -b:a 96k -f adts "$OUT/sine440_mono_44k1.aac"

# 2. Linear chirp 20 Hz -> 16 kHz, stereo (decorrelated L/R), 48 kHz, 3 s.
$FF -f lavfi -i "aevalsrc=0.5*sin(2*PI*(20*t+(16000-20)/(2*3)*t*t))|0.5*sin(2*PI*(25*t+(15000-25)/(2*3)*t*t)):s=48000:d=3" \
    -c:a aac -b:a 128k -f adts "$OUT/chirp_stereo_48k.aac"

# 3. Deterministic white noise, stereo, 32 kHz, 2 s.
$FF -f lavfi -i "anoisesrc=color=white:amplitude=0.4:duration=2:sample_rate=32000:seed=1234" \
    -af "aformat=channel_layouts=stereo" \
    -c:a aac -b:a 96k -f adts "$OUT/noise_stereo_32k.aac"

# 4. Digital silence, mono, 48 kHz, 1 s.
$FF -f lavfi -i "anullsrc=r=48000:cl=mono:d=1" \
    -c:a aac -b:a 64k -f adts "$OUT/silence_mono_48k.aac"

# 5. Impulse train (clicks every 5000 samples), stereo, 44.1 kHz, 2 s.
$FF -f lavfi -i "aevalsrc=if(eq(mod(n\,5000)\,0)\,0.8\,0)|if(eq(mod(n+7\,5000)\,0)\,0.8\,0):s=44100:d=2" \
    -c:a aac -b:a 128k -f adts "$OUT/impulses_stereo_44k1.aac"

# 6. Low-level (-40 dB) 1 kHz sine, mono, 24 kHz, 2 s.
#    Exercises fixed-point precision at small amplitudes.
$FF -f lavfi -i "sine=frequency=1000:sample_rate=24000:duration=2" \
    -af "volume=-40dB" \
    -c:a aac -b:a 48k -f adts "$OUT/sine1k_lowlevel_mono_24k.aac"

# 7. Full-scale 5.1 channel tones (different frequency per channel), 48 kHz, 2 s.
$FF -f lavfi -i "sine=frequency=330:sample_rate=48000:duration=2" \
    -f lavfi -i "sine=frequency=440:sample_rate=48000:duration=2" \
    -f lavfi -i "sine=frequency=550:sample_rate=48000:duration=2" \
    -f lavfi -i "sine=frequency=660:sample_rate=48000:duration=2" \
    -f lavfi -i "sine=frequency=770:sample_rate=48000:duration=2" \
    -f lavfi -i "sine=frequency=880:sample_rate=48000:duration=2" \
    -filter_complex "amerge=inputs=6,aformat=channel_layouts=5.1" \
    -c:a aac -b:a 256k -f adts "$OUT/tones_51_48k.aac"

# 8. Alternating tone/silence bursts, mono, 44.1 kHz, 2 s (transient onsets).
$FF -f lavfi -i "aevalsrc=0.6*sin(2*PI*2000*t)*gt(mod(t\,0.5)\,0.25):s=44100:d=2" \
    -c:a aac -b:a 96k -f adts "$OUT/bursts_mono_44k1.aac"

# 9. Low-bitrate stereo music-like mix, 44.1 kHz, 3 s (stressed quantization).
$FF -f lavfi -i "aevalsrc=0.3*sin(2*PI*220*t)+0.2*sin(2*PI*1770*t)+0.1*sin(2*PI*5000*t)|0.3*sin(2*PI*225*t)+0.2*sin(2*PI*1810*t)+0.1*sin(2*PI*5200*t):s=44100:d=3" \
    -c:a aac -b:a 48k -f adts "$OUT/mix_lowbitrate_stereo_44k1.aac"

# 10. MP4 container variant of sample 1 (exercises the MP4 reader path).
$FF -f lavfi -i "sine=frequency=440:sample_rate=44100:duration=2" \
    -c:a aac -b:a 96k "$OUT/sine440_mono_44k1.m4a"

echo "Sample set written to $OUT"
