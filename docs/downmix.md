# Configurable stereo downmix

This document specifies the multi-channel to stereo downmix implemented in
`libfaad/downmix.c`. It covers the supported input layouts, the two selectable
matrix profiles (ITU and MPEG), and the documented peak control behaviour.

## 1. Scope and channel roles

When downmixing is enabled (`NeAACDecConfiguration.downMatrix = 1`, or the
frontend `-d` flag), every frame with **five or more full-band channels** is
mixed to two output channels (left, right). The previously supported 5.1 case
is extended to **5.1, 6.1 and 7.1** layouts, including PCE-defined variants.

Channels are identified by their role from `NeAACDecFrameInfo.channel_position`
(see `FRONT_CHANNEL_*`, `SIDE_CHANNEL_*`, `BACK_CHANNEL_*`, `LFE_CHANNEL` in
`neaacdec.h`), not by a hard-coded index, so both implicit channel
configurations and explicit program config elements (PCE) are handled.

| Role                | Symbol        | Used in                  |
|---------------------|---------------|--------------------------|
| front center        | `C`           | 5.1 / 6.1 / 7.1          |
| front left / right  | `L` / `R`     | all                      |
| side  left / right  | `SL` / `SR`   | 6.1 / 7.1 (or 5.1 side)  |
| back  left / right  | `BL` / `BR`   | 5.1 / 7.1                |
| back center         | `BC`          | 6.1                      |
| LFE                 | `LFE`         | discarded by the stereo mix |

The internal AAC channel order for the standard configurations is:

* 5.1 (`channelConfiguration = 6`): `C, L, R, BL, BR, LFE`
* 7.1 (`channelConfiguration = 7`): `C, L, R, SL, SR, BL, BR, LFE`
* 6.1 (PCE, no fixed configuration): `C, L, R, SL/SR pair, BC, LFE`

A frame whose layout contains no stereo pair cannot be represented; the decoder
then falls back to the unmodified multi-channel output instead of producing a
wrong mix.

## 2. Raw matrix coefficients

Both profiles share the same raw per-role coefficients. The only difference is
the post-sum normalization gain (section 3). Let `x(role)` be one input sample
of the given role.

Raw contribution to each output bus:

| Role            | Left bus coefficient | Right bus coefficient |
|-----------------|----------------------|-----------------------|
| `L`             | 1                    | 0                     |
| `R`             | 0                    | 1                     |
| `C`             | 1/√2                 | 1/√2                  |
| `SL`, `BL`      | 1/√2                 | 0                     |
| `SR`, `BR`      | 0                    | 1/√2                  |
| `BC`            | 1/2                  | 1/2                   |
| `LFE`           | 0                    | 0                     |

The `1/√2 ≈ 0.70710678` coefficient is the ITU-R BS.775 family center/surround
level. A back-center channel is split equally (coefficient `1/2`) to both
buses, following the two-stage 7.1 → 5.1 → stereo fold convention.

Define the raw (pre-gain) bus sums

```
L_raw = x(L) + (x(C) + x(SL) + x(BL)) / √2 + x(BC) / 2
R_raw = x(R) + (x(C) + x(SR) + x(BR)) / √2 + x(BC) / 2
```

and the final outputs

```
L_out = G · L_raw
R_out = G · R_raw
```

where `G` depends on the profile and peak policy.

## 3. Matrix profiles

Selected with `NeAACDecConfiguration.downMixProfile`
(frontend: `-D mpeg` / `-D itu`).

### 3.1 `FAAD_DMX_PROFILE_MPEG` (default) — normalized matrix

This profile is normalization-only and preserves the historical faad2 sound.

* **5.0 / 5.1** (exactly one surround pair, no back center): the gain is the
  original faad2 constant

  ```
  G_5 = 1 / (1 + √2 + 1/√2) ≈ 0.3203772410
  ```

  The float and fixed-point 5.1 outputs are **bit identical** to the historical
  faad2 downmix.
* **6.1 / 7.1 and other extended layouts**: the gain is the documented layout
  headroom factor `G = 1/S` (section 4), computed from the actual coefficients
  of the frame.

Because `G ≤ 1/S`, a coherent full-scale input can never exceed full scale on
either bus, so peak clipping never changes an MPEG result.

### 3.2 `FAAD_DMX_PROFILE_ITU` — unnormalized ITU-R BS.775 family matrix

The raw coefficients of section 2 are applied with unity gain

```
G = 1
```

This keeps the direct L/R signal at unity and the conventional center/surround
levels, but the coherent worst case can exceed full scale:

| Layout | worst-case coherent gain `S` | peak overshoot |
|--------|------------------------------|-----------------|
| 5.1    | 1 + 2/√2 ≈ 2.4142            | +3.25 dB        |
| 6.1    | 1 + 2/√2 + 1/2 ≈ 2.9142      | +5.29 dB        |
| 7.1    | 1 + 3/√2 ≈ 3.1213            | +5.90 dB        |

The overshoot must therefore be handled with an explicit peak control policy
(section 5).

## 4. Layout headroom factor `1/S`

For each frame, `S` is the largest sum of absolute raw coefficients on either
output bus. Multiplying the mix by `1/S` guarantees that the coherent
worst-case output stays at or below full scale.

| Layout class                          | `S`                 | `1/S` (approx)      |
|---------------------------------------|---------------------|---------------------|
| one surround pair, no BC (5.0 / 5.1)  | 1 + 2/√2            | 0.7071067812        |
| one surround pair + BC (6.1)          | 1 + 2/√2 + 1/2      | 0.3431457505        |
| two surround pairs, no BC (7.1)       | 1 + 3/√2            | 0.3203772410        |

In the floating-point build `1/S` is evaluated at run time from the resolved
coefficient table, so PCE layouts with an unusual number of surround pairs get
an exact factor. The fixed-point build has no division and uses the three
documented constants above, selected by the layout class.

## 5. Peak control

Selected with `NeAACDecConfiguration.downMixPeak`
(frontend: `-P default|clip|scale`).

### 5.1 `FAAD_DMX_PEAK_DEFAULT` (default) — no extra peak control

The matrix result is passed through unchanged.

* **Integer PCM output (16/24/32 bit):** the existing per-format saturation in
  the PCM converter still applies, exactly as in historical faad2. Samples that
  would exceed the format range are hard-limited to that range.
* **Floating-point output (32-bit float / 64-bit double):** no saturation is
  applied; with the ITU profile the output may exceed the nominal `±1.0`
  full-scale range. The MPEG profile never exceeds full scale.

### 5.2 `FAAD_DMX_PEAK_CLIP` — explicit hard clipping

Only meaningful with the ITU profile. After the matrix sum, any sample outside
the internal full-scale range is hard-clipped:

* floating-point output: clipped at `±1.0` in the float output scale, which
  equals `±32768` in the internal `time_out` scale (the PCM converter applies
  `FLOAT_SCALE = 1/32768`);
* integer PCM output: clipped at the internal full scale and then saturated
  again by the per-format conversion if necessary;
* fixed-point build: the clipping is performed by the existing integer PCM
  saturation step (the internal Q-format accumulator is not clipped, to retain
  headroom for the format conversion).

With the MPEG profile this mode is a no-op by construction (the normalized
coefficients cannot reach the clipping threshold).

### 5.3 `FAAD_DMX_PEAK_SCALE` — documented scale factor

The whole mix is multiplied by the layout headroom factor `1/S` from
section 4. The coherent worst case then stays at or below full scale, so **no
clipping occurs** on any output format. This is the recommended policy when
guaranteed headroom is required and clipping artefacts are unacceptable.

## 6. Numeric precision

* The floating-point build performs every multiply-add in binary32 (`float`)
  in the documented term order. The no-peak-control and peak-scaled results are
  covered by bit-exact tests against an independent Python reference
  (`tests/downmix_ref.py`, `tests/e2e_downmix.py`).
* The fixed-point build stores input samples in Q14 and matrix coefficients as
  Q31 unit fractions consumed by `MUL_F` (31-bit fractional shift). The 5.1
  MPEG/default path reproduces the original `DM_MUL` / `BOTH` accumulation
  structure exactly and remains bit identical to historical faad2.

## 7. API summary

`neaacdec.h`:

| Field             | Values                                            |
|-------------------|---------------------------------------------------|
| `downMatrix`      | 0 = passthrough, 1 = enable stereo downmix        |
| `downMixProfile`  | `FAAD_DMX_PROFILE_MPEG` (0), `FAAD_DMX_PROFILE_ITU` (1) |
| `downMixPeak`     | `FAAD_DMX_PEAK_DEFAULT` (0), `FAAD_DMX_PEAK_CLIP` (1), `FAAD_DMX_PEAK_SCALE` (2) |

Frontend:

```
faad -d                         # downmix, MPEG profile, default peak control
faad -d -D itu -P clip in.aac   # ITU matrix with hard clipping
faad -d -D itu -P scale in.aac  # ITU matrix with 1/S headroom scaling
faad -d -D mpeg -P scale in.aac # MPEG coefficients with explicit 1/S gain
```
