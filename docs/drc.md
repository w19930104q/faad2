# Dynamic Range Control (DRC) in libfaad

libfaad decodes the MPEG-4 AAC dynamic range control metadata
(`dynamic_range_info()` carried in fill elements) and can apply the
signalled gain to the decoded audio. Since the DRC metadata is only
useful if the player can control it, the compression level and the
effectively applied per-frame gains are exposed through the public
library API.

## Compression levels

Three levels are defined in `neaacdec.h`:

| Level           | Value | Meaning |
|-----------------|-------|---------|
| `FAAD_DRC_OFF`   | 0 | DRC not applied. Output is bit-exact identical to a decode in which every DRC gain is zero (unity gain): no compression, no program reference level normalisation. |
| `FAAD_DRC_LIGHT` | 1 | Half compression: the `dyn_rng_ctl` compression/boost term is scaled by 1/2. The program reference level normalisation term is applied in full. |
| `FAAD_DRC_HEAVY` | 2 | Full compression. This is the default and the historical behaviour of the library. |

The default level is `FAAD_DRC_HEAVY`, so existing applications see no
behaviour change.

## Gain formula

For each DRC band the applied linear gain factor is

```
compress (dyn_rng_sgn == 1):
    gain = 2 ^ ((-ctrl * dyn_rng_ctl - (DRC_REF_LEVEL - prog_ref_level)) / 24)
boost (dyn_rng_sgn == 0):
    gain = 2 ^ (( ctrl * dyn_rng_ctl - (DRC_REF_LEVEL - prog_ref_level)) / 24)
```

with

- `ctrl = 1.0` for `FAAD_DRC_HEAVY`, `ctrl = 0.5` for `FAAD_DRC_LIGHT`,
- `DRC_REF_LEVEL = 80` (i.e. -20 dB expressed in 0.25 dB steps),
- `dyn_rng_ctl`, `dyn_rng_sgn` and `prog_ref_level` taken from the
  bitstream DRC metadata.

For `FAAD_DRC_OFF` the gain is exactly 1.0 for every band (the DRC
stage is bypassed entirely).

Note: in fixed-point builds (`FIXED_POINT`) the halved `dyn_rng_ctl`
value used by `FAAD_DRC_LIGHT` is rounded to the nearest integer,
because the fixed-point gain table has 1/24-resolution exponents.

Note: in fixed-point builds the compress-case formula previously
added (instead of subtracted) the program reference level
normalisation term, disagreeing with both the floating-point build and
the formula above. This has been corrected; fixed-point output now
matches the floating-point build for streams that signal a
`prog_ref_level` different from the reference level.

## Execution order relative to downmixing

The DRC gain is applied per channel in the **spectral domain**, after
TNS decoding and **before** the IMDCT filter bank. Downmixing
(`downMatrix`, e.g. 5.1 to stereo) is performed in the **time domain**
on the filter bank output, after SBR/PS processing. The full pipeline
order is therefore:

```
bitstream -> dequantisation -> TNS -> DRC gain -> IMDCT -> SBR/PS -> downmix -> PCM output
```

Consequences:

- DRC always runs **before** downmixing. The DRC gain is applied to
  each full-resolution channel independently; the downmix matrix then
  combines the already-gained channels.
- Channels listed in the DRC `excluded_channels` mask never receive DRC
  gain, but they still take part in the downmix.
- Switching the DRC level therefore never changes the channel count or
  the downmix configuration, only the sample values.

## API

```c
#define FAAD_DRC_OFF   0
#define FAAD_DRC_LIGHT 1
#define FAAD_DRC_HEAVY 2

unsigned char NeAACDecSetDRCLevel(NeAACDecHandle hDecoder, unsigned char level);
unsigned char NeAACDecGetDRCLevel(NeAACDecHandle hDecoder);
unsigned char NeAACDecGetDRCFrameInfo(NeAACDecHandle hDecoder,
                                      NeAACDecDRCFrameInfo *info);
```

- `NeAACDecSetDRCLevel()` sets the compression level. It takes effect
  on the **next** decoded frame, so the level can be switched at
  runtime between `NeAACDecDecode()` calls without re-initialising the
  decoder. Returns 1 on success, 0 on invalid arguments.
- `NeAACDecGetDRCLevel()` returns the current level.
- `NeAACDecGetDRCFrameInfo()` reports what was applied while decoding
  the most recent frame:

```c
typedef struct NeAACDecDRCFrameInfo
{
    unsigned char present;   /* DRC metadata signalled in the bitstream */
    unsigned char level;     /* level in effect (FAAD_DRC_*) */
    unsigned char num_bands; /* signalled DRC bands (0 when !present) */
    double gain[17];         /* linear gain effectively applied per band */
} NeAACDecDRCFrameInfo;
```

`gain[bd]` is the linear factor that was applied to band `bd` during
the last decoded frame; it is 1.0 for bands to which no gain was
applied (level `FAAD_DRC_OFF`, excluded channels, or frames without
DRC metadata).

## Notes

- DRC application must be compiled in (CMake option `FAAD_APPLY_DRC`,
  ON by default; Bazel builds always enable it). If it is compiled out,
  no gain is ever applied and `NeAACDecGetDRCFrameInfo()` reports 1.0
  gains regardless of the level.
- Following the historical behaviour of the library, DRC metadata is
  "sticky": once signalled, the most recently received `dyn_rng_ctl`
  values remain in effect for subsequent frames until new metadata
  arrives.
- A `dynamic_range_info()` payload placed in a fill element *before*
  the channel element of the same raw data block applies to that frame;
  one placed after it applies from the next frame on.

## Tests

`tests/test_drc.c` contains acceptance tests for this API. They
synthesise ADTS streams with known DRC metadata (fixed anchors) and
verify, for both the floating-point and the fixed-point build:

- `FAAD_DRC_OFF` decodes bit-exact identical to a zero-gain reference
  decode (same stream with all DRC gains signalled as zero);
- the per-frame gains reported by `NeAACDecGetDRCFrameInfo()` match the
  gains computed independently from the bitstream DRC metadata for
  every level, and the PCM output is scaled by exactly those gains;
- switching the level at runtime takes effect on the next decoded
  frame.

Build and run with:

```sh
cmake -S . -B build -DFAAD_BUILD_TESTS=ON
cmake --build build
(cd build && ctest)
```

