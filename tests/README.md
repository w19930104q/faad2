# Downmix tests

The configurable stereo downmix (`libfaad/downmix.c`) is verified at two
levels. Both use independent references rather than reusing the
implementation under test.

## Unit tests — matrix math on synthetic samples

* `test_downmix.c` — drives `downmix_build_plan()` / `downmix_apply()` for the
  5.1, 6.1 and 7.1 role tables across every profile (MPEG/ITU) and peak policy
  (default/clip/scale), including coherent full-scale extreme samples.
* `downmix_ref.py` — independent Python reimplementation of the documented
  formula (`../docs/downmix.md`). All arithmetic is binary32 with the same
  operation order as the C code, and it regenerates `downmix_expected.h`,
  which holds the hard-coded expected outputs (each value also carries its
  exact float bit pattern in a comment).
* `test_stubs.c` — minimal link stubs for running the unit test without the
  rest of libfaad.

Regenerate expectations and run:

```
python3 -I tests/downmix_ref.py
cc -DHAVE_INTTYPES_H=1 -DHAVE_STRING_H=1 -Ilibfaad -Iinclude \
   tests/test_downmix.c libfaad/downmix.c tests/test_stubs.c -lm -o test_downmix
./test_downmix
```

or via CMake:

```
cmake -S . -B build -DFAAD_BUILD_TESTS=ON
cmake --build build
ctest --test-dir build
```

The no-peak-control and peak-scaled cases must match the reference **bit for
bit**; the clip cases must match the documented hard-clip rule.

## End-to-end tests — real AAC streams

* `e2e_harness.c` — decodes an ADTS AAC file twice with libfaad (multichannel
  pass and stereo-downmix pass) and dumps the float PCM plus the per-frame
  channel-position table.
* `e2e_downmix.py` — recomputes every stereo sample from the decoded
  multichannel PCM and the emitted channel roles, then compares against the
  library downmix output bit-for-bit for all 18 layout/profile/policy
  combinations.

The multichannel AAC inputs (5.1 / 6.1 / 7.1) are produced with ffmpeg, one
distinct sine per channel, so a wrong channel-role mapping is detected
immediately.

## Fixed-point

`downmix.c` compiles under `-DFIXED_POINT` as part of the `faad_fixed` library.
A fixed-point driver (kept outside the repo's build graph) checks the resolved
term tables, positive/normalized output ranges, overflow-free headroom, and the
bit-exact 5.1 legacy `DM_MUL`/`BOTH` accumulation.
