# Fixed-point decoder accuracy and the float/fixed consistency bound

FAAD2 can be built in two arithmetic modes:

* **floating point** (default, `libfaad` / `faad`), and
* **fixed point** (`FIXED_POINT`, `libfaad_fixed` / `faad_fixed`).

Both modes decode the same bitstreams. The fixed-point pipeline uses 32-bit
integer arithmetic with `REAL_BITS = 14` fractional bits at the output stage
(see `libfaad/fixed.h`), i.e. the internal sample representation carries 14
fractional bits below the 16-bit output scale. Because the two pipelines round
intermediate results differently, their outputs are not bit-identical, but
they must agree within the quantization bound below.

## Quantization bound

Let `float[n]` and `fixed[n]` be the 16-bit PCM output samples of the two
decoders for the same bitstream. The outputs are **consistent** when

```
|float[n] - fixed[n]|  <=  B_MAX = Q + E_PIPE = 1 + 3 = 4 LSB   (per sample)
rms(float - fixed)     <=  B_RMS = 1.5 LSB                      (per stream)
```

with the terms defined as follows:

* **Q = 1 LSB** — output quantization. Both decoders round the (nearly) same
  real value to the nearest 16-bit integer; two round-to-nearest quantizers
  of values that differ by less than 1 LSB can disagree by at most 1 LSB.
* **E_PIPE = 3 LSB** — allowance for the accumulated fixed-point arithmetic
  error of the fixed-point pipeline, referred to the 16-bit output scale.
  The contributors are the dequantizer's exponent-shift truncation, the
  rounding of the Q28 filterbank/window coefficients, the butterfly-stage
  round-off of the fixed-point IMDCT, and the log2/exp2 table interpolation
  used by PNS energy normalization. Measured on the validation corpus (the
  regression sample set plus an extended sweep of tones, sweeps, transients,
  silence and noise at 8–96 kHz, 24–256 kbit/s, mono/stereo/5.1), the worst
  observed deviation is 3 LSB per sample and 0.97 LSB RMS, so the bound
  carries headroom without masking real defects.

The bound is enforced by `tests/run_fixed_float_regression.sh` (wired into
CTest as the `fixed_float_consistency` test), which decodes every stream in
`tests/samples/` with both CLIs and compares the raw 16-bit PCM outputs with
`tests/compare_pcm.py`. Any deviation beyond the bound is a bug in the
decoder, not a reason to loosen the bound.

## Regression sample set

`tests/samples/` contains a fixed set of ADTS/MP4 AAC streams (LC profile:
tones, chirp, transients, silence, low-level signal, low-bitrate mix, 5.1
multichannel, and noise — the last one exercises perceptual noise
substitution heavily). The set is regenerated only deliberately, with
`tests/gen_samples.sh` (deterministic signals, fixed seeds).

## Notes on the fixed-point PNS decoder

Perceptual noise substitution replaces quantized spectral coefficients with a
pseudo-random vector normalized to the energy signalled by the scale factor.
The fixed-point decoder historically scaled the vector by the *expected*
energy of the RNG output instead of the *realized* energy of the generated
vector, so every PNS band's energy was off by a random factor (up to ~30% for
small bands) compared to the floating point decoder — far outside the bound
above. The fixed-point path now mirrors the floating point one: it
accumulates the exact energy of the generated vector and normalizes with

```
scale = 2^(scale_factor/4) / sqrt(energy)
```

evaluated in fixed point as `pow2_fix(sf/4 - log2_int(energy)/2)` using the
existing `log2_int`/`pow2_fix` helpers (`libfaad/common.c`). Both builds draw
the identical RNG sequence (`ne_rng`), so after exact energy normalization
the PNS output agrees within the general pipeline allowance.
