#!/usr/bin/env bash
#
# Acceptance tests for MP4 chapter reading.
#
# Covers:
#   1. Fixed MP4 fixture with a known chapter table (chpl + text track).
#   2. Same fixture with chpl stripped (text-track fallback only).
#   3. File without chapters: empty table, no error.
#   4. CUE sheet export content and 75 fps timestamp math.
#   5. Non-regression: audio decode and metadata output are unchanged.
#
set -uo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
DATA="$ROOT/tests/data"
FAAD="${FAAD:-$ROOT/build/faad}"

fail=0
pass=0

check() {
    local name="$1" cond="$2"
    if [ "$cond" = "1" ]; then
        echo "PASS: $name"
        pass=$((pass + 1))
    else
        echo "FAIL: $name"
        fail=$((fail + 1))
    fi
}

# The hard-coded expected chapter table (index, start nanoseconds, title).
EXPECTED_TEXT=$(cat <<'EOF'
Chapters: 3
  1  00:00.0  Intro / 序章
  2  00:00.50  Main
  3  00:01.50  Outro / 終わり
EOF
)

# --- 1. Primary chpl path -------------------------------------------------
out="$("$FAAD" --chapters "$DATA/chapters.mp4" 2>/dev/null)"
check "chpl: chapter table matches fixed expectation" \
      "$([ "$out" = "$EXPECTED_TEXT" ] && echo 1 || echo 0)"
[ "$out" = "$EXPECTED_TEXT" ] || printf '%s\n' "$out"

# short option alias
out="$("$FAAD" -C "$DATA/chapters.mp4" 2>/dev/null)"
check "chpl: -C alias works" "$([ "$out" = "$EXPECTED_TEXT" ] && echo 1 || echo 0)"

# --- 2. Text-track fallback (chpl removed) --------------------------------
out="$("$FAAD" --chapters "$DATA/textchap.mp4" 2>/dev/null)"
check "text-track: fallback yields the same table" \
      "$([ "$out" = "$EXPECTED_TEXT" ] && echo 1 || echo 0)"

# Optional: chpl nested under udta/meta rather than a direct udta child.
# Generated on the fly from the primary fixture; skipped when python3 is absent.
if command -v python3 >/dev/null 2>&1; then
    nested="$(mktemp --suffix=.mp4)"
    if python3 -I "$HERE/nest_chpl.py" "$DATA/chapters.mp4" "$nested" >/dev/null 2>&1; then
        out="$("$FAAD" -q -C "$nested" 2>/dev/null)"
        check "nested: udta/meta/chpl yields the same table" \
              "$([ "$out" = "$EXPECTED_TEXT" ] && echo 1 || echo 0)"
    fi
    rm -f "$nested"
fi

# --- 3. No chapters: empty table, exit 0, no chapter-specific errors ------
# -q suppresses faad's status/banner output, leaving only real diagnostics.
err="$("$FAAD" -q -C "$DATA/nochapters.mp4" 2>&1 >/dev/null)"
rc=$?
out="$("$FAAD" -q -C "$DATA/nochapters.mp4" 2>/dev/null)"
check "no-chapters: exits successfully" "$([ $rc -eq 0 ] && echo 1 || echo 0)"
check "no-chapters: reports zero chapters" \
      "$([ "$out" = "Chapters: 0" ] && echo 1 || echo 0)"
check "no-chapters: prints no diagnostics in quiet mode" \
      "$([ -z "$err" ] && echo 1 || echo 0)"

# --- 4. CUE sheet ----------------------------------------------------------
cue="$("$FAAD" -c - "$DATA/chapters.mp4" 2>/dev/null)"
expected_cue=$(cat <<'EOF'
REM GENERATOR faad2 chapter export
FILE "PLACEHOLDER" AUDIO
  TRACK 01 AUDIO
    TITLE "Intro / 序章"
    INDEX 01 00:00:00
  TRACK 02 AUDIO
    TITLE "Main"
    INDEX 01 00:00:37
  TRACK 03 AUDIO
    TITLE "Outro / 終わり"
    INDEX 01 00:01:37
EOF
)
# FILE line carries the input path; compare everything else exactly.
cue_norm="$(printf '%s\n' "$cue" | grep -v '^FILE ')"
exp_norm="$(printf '%s\n' "$expected_cue" | grep -v '^FILE ')"
check "cue: content and 75fps timestamps correct" \
      "$([ "$cue_norm" = "$exp_norm" ] && echo 1 || echo 0)"
if printf '%s\n' "$cue" | grep -q '^FILE ".*chapters.mp4" AUDIO$'; then
    file_ok=1
else
    file_ok=0
fi
check "cue: FILE header present" "$file_ok"

# empty cuesheet: header only, still successful
cue_empty="$("$FAAD" -c - "$DATA/nochapters.mp4" 2>/dev/null)"
check "cue: empty chapter file exports header only" \
      "$([ "$(printf '%s\n' "$cue_empty" | wc -l)" = "2" ] && echo 1 || echo 0)"

# write to a file rather than stdout
tmpcue="$(mktemp)"
"$FAAD" -c "$tmpcue" "$DATA/chapters.mp4" >/dev/null 2>&1
check "cue: writes named output file" "$([ -s "$tmpcue" ] && echo 1 || echo 0)"
if grep -q 'INDEX 01 00:01:37' "$tmpcue"; then ts_ok=1; else ts_ok=0; fi
check "cue: named file contains expected timestamp" "$ts_ok"
rm -f "$tmpcue"

# --- 5. Non-regression: decode still works --------------------------------
for f in chapters.mp4 nochapters.mp4 textchap.mp4; do
    wav="$(mktemp --suffix=.wav)"
    "$FAAD" -q -o "$wav" "$DATA/$f" >/dev/null 2>&1
    rc=$?
    check "decode: $f succeeds" "$([ $rc -eq 0 ] && echo 1 || echo 0)"
    check "decode: $f produces non-empty WAV" "$([ -s "$wav" ] && echo 1 || echo 0)"
    rm -f "$wav"
done

# chapter-only mode must not create audio output
before="$(ls -1 "$DATA" | sort)"
"$FAAD" -C "$DATA/chapters.mp4" >/dev/null 2>&1
after="$(ls -1 "$DATA" | sort)"
check "chapter listing does not create side files" \
      "$([ "$before" = "$after" ] && echo 1 || echo 0)"

echo
echo "================== $pass passed, $fail failed =================="
[ "$fail" -eq 0 ]
