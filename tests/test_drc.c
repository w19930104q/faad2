/*
** Acceptance tests for the public DRC control API of libfaad.
**
** Covers:
**  1. FAAD_DRC_OFF decodes bit-exact identical to a zero-gain reference
**     decode (same stream, DRC metadata carrying zero gains).
**  2. The per-frame gains reported by NeAACDecGetDRCFrameInfo() match the
**     gains computed independently from the bitstream DRC metadata for
**     every level (fixed anchor values).
**  3. The PCM output is actually scaled by the reported gain.
**  4. Switching the level at runtime takes effect on the next frame.
**
** The test synthesises its own ADTS/AAC-LC streams (mono, 44.1 kHz):
** each raw_data_block contains a fill element with dynamic_range_info()
** followed by a single_channel_element with one non-zero spectral
** quadruple, so the DRC gain of every frame is fully determined by the
** metadata anchors below.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>

#include <neaacdec.h>

#define NFRAMES 8
#define MAX_FRAMES 64

/* When the library is built with FAAD_APPLY_DRC=OFF no DRC gain is ever
   applied; the expected gain for every level is then exactly 1.0. */
#ifdef FAAD_TEST_NO_DRC
#define DRC_COMPILED_IN 0
#else
#define DRC_COMPILED_IN 1
#endif

/* Fixed-point build: FAAD_DRC_LIGHT rounds the halved dyn_rng_ctl value. */
#ifdef FAAD_TEST_FIXED
#define DRC_FIXED_POINT 1
#else
#define DRC_FIXED_POINT 0
#endif

/* DRC metadata anchors, one per frame of drc_sample.aac */
typedef struct { int sgn, ctl, prog_ref; } drc_meta;

static const drc_meta sample_meta[NFRAMES] = {
    {1, 48, 80},  /* heavy 2^-2    light 2^-1     */
    {1, 24, 80},  /* heavy 2^-1    light 2^-1/2   */
    {0, 24, 80},  /* heavy 2^+1    light 2^+1/2   */
    {0, 12, 80},  /* heavy 2^+1/2  light 2^+1/4   */
    {1, 48, 64},  /* heavy 2^-8/3  light 2^-5/3   */
    {0,  0, 72},  /* heavy 2^-1/3  light 2^-1/3   */
    {1,  7, 80},  /* heavy 2^-7/24 light 2^-7/48  */
    {1,  0, 80},  /* unity gain                   */
};

/* zero-gain reference metadata: gain exponent is exactly 0 */
static const drc_meta ref_meta = {1, 0, 80};
/* constant metadata for the PCM scaling test */
static const drc_meta const_meta = {1, 48, 80}; /* heavy 0.25, light 0.5 */

/* ------------------------------------------------------------------ */
/* bitstream writer                                                    */

typedef struct {
    unsigned char buf[512];
    size_t nbits;
} bitw;

static void bw_init(bitw *w)
{
    memset(w->buf, 0, sizeof(w->buf));
    w->nbits = 0;
}

static void bw_put(bitw *w, unsigned int v, int n)
{
    int i;
    for (i = n - 1; i >= 0; i--)
    {
        size_t byte = w->nbits >> 3;
        int bit = 7 - (int)(w->nbits & 7);
        if ((v >> i) & 1)
            w->buf[byte] |= (unsigned char)(1 << bit);
        w->nbits++;
    }
}

/* Build one ADTS frame: FIL(dynamic_range_info) + SCE + END */
static size_t build_frame(unsigned char *out, const drc_meta *meta, int with_drc)
{
    bitw w;
    size_t payload, frame_length;

    bw_init(&w);

    if (with_drc)
    {
        bw_put(&w, 6, 3);                 /* ID_FIL */
        bw_put(&w, 3, 4);                 /* count = 3 bytes */
        bw_put(&w, 11, 4);                /* EXT_DYNAMIC_RANGE */
        bw_put(&w, 0, 1);                 /* pce_present */
        bw_put(&w, 0, 1);                 /* excluded_chns_present */
        bw_put(&w, 0, 1);                 /* bands_present */
        bw_put(&w, 1, 1);                 /* prog_ref_present */
        bw_put(&w, (unsigned)meta->prog_ref, 7);
        bw_put(&w, 0, 1);                 /* reserved */
        bw_put(&w, (unsigned)meta->sgn, 1);
        bw_put(&w, (unsigned)meta->ctl, 7);
    }

    /* single_channel_element, one non-zero spectral quadruple (1,1,0,0);
       global_gain 168 keeps the PCM well above the 16-bit and fixed-point
       rounding floors while leaving headroom for the x2 boost anchors */
    bw_put(&w, 0, 3);                     /* ID_SCE */
    bw_put(&w, 0, 4);                     /* element_instance_tag */
    bw_put(&w, 168, 8);                   /* global_gain */
    bw_put(&w, 0, 1);                     /* ics_reserved_bit */
    bw_put(&w, 0, 2);                     /* ONLY_LONG_SEQUENCE */
    bw_put(&w, 0, 1);                     /* window_shape: sine */
    bw_put(&w, 1, 6);                     /* max_sfb = 1 */
    bw_put(&w, 0, 1);                     /* predictor_data_present */
    bw_put(&w, 1, 4);                     /* sect_cb = 1 */
    bw_put(&w, 1, 5);                     /* sect_len = 1 */
    bw_put(&w, 0, 1);                     /* scalefactor delta 0 */
    bw_put(&w, 0, 1);                     /* pulse_data_present */
    bw_put(&w, 0, 1);                     /* tns_data_present */
    bw_put(&w, 0, 1);                     /* gain_control_data_present */
    bw_put(&w, 0x19, 5);                  /* hcb1 prefix "11001" */
    bw_put(&w, 0x2, 2);                   /* extra "10" -> (1,1,0,0) */

    bw_put(&w, 7, 3);                     /* ID_END */

    payload = (w.nbits + 7) / 8;
    frame_length = 7 + payload;

    /* ADTS fixed+variable header, MPEG-4 AAC LC, 44.1 kHz, mono */
    out[0] = 0xFF;
    out[1] = 0xF1;
    out[2] = (1 << 6) | (4 << 2);
    out[3] = (1 << 6) | (unsigned char)((frame_length >> 11) & 0x3);
    out[4] = (unsigned char)((frame_length >> 3) & 0xFF);
    out[5] = (unsigned char)((frame_length & 0x7) << 5) | 0x1F;
    out[6] = 0xFC;
    memcpy(out + 7, w.buf, payload);

    return frame_length;
}

static size_t build_stream(unsigned char *out, const drc_meta *meta_per_frame,
                           int const_meta_mode)
{
    size_t off = 0;
    int k;
    for (k = 0; k < NFRAMES; k++)
    {
        const drc_meta *m = const_meta_mode ? meta_per_frame : &meta_per_frame[k];
        off += build_frame(out + off, m, 1);
    }
    return off;
}

/* ------------------------------------------------------------------ */
/* expected gain, computed independently from the bitstream metadata   */

static double expected_gain(int level, int sgn, int ctl, int prog_ref)
{
    double ctrl, e;
    if (!DRC_COMPILED_IN || level == FAAD_DRC_OFF)
        return 1.0;
    if (DRC_FIXED_POINT && level == FAAD_DRC_LIGHT)
    {
        /* fixed-point LIGHT rounds the halved control value and then
           applies the full formula */
        ctl = (ctl + 1) >> 1;
        ctrl = 1.0;
    }
    else
    {
        ctrl = (level == FAAD_DRC_LIGHT) ? 0.5 : 1.0;
    }
    if (sgn) /* compress */
        e = (-ctrl * ctl - (80 - prog_ref)) / 24.0;
    else     /* boost */
        e = ( ctrl * ctl - (80 - prog_ref)) / 24.0;
    return pow(2.0, e);
}

/* ------------------------------------------------------------------ */
/* decode helper                                                       */

typedef struct {
    int nframes;
    int fmt;
    size_t stride;                       /* bytes per sample */
    long frame_samples[MAX_FRAMES];      /* samples per frame (0: discarded) */
    long frame_offset[MAX_FRAMES];       /* sample offset into pcm */
    long total_samples;
    unsigned char pcm[512 * 1024];
    NeAACDecDRCFrameInfo drc[MAX_FRAMES];
} decode_result;

static long adts_frame_length(const unsigned char *p)
{
    return ((p[3] & 0x3) << 11) | ((long)p[4] << 3) | ((p[5] >> 5) & 0x7);
}

/* Decode a whole ADTS stream. When switch_frame >= 0, the level is
   changed to switch_level right before decoding that frame. */
static int decode_stream(const unsigned char *data, size_t size,
                         int level, int fmt,
                         int switch_frame, int switch_level,
                         decode_result *res)
{
    NeAACDecHandle dec;
    NeAACDecConfigurationPtr cfg;
    NeAACDecFrameInfo info;
    unsigned long samplerate = 0;
    unsigned char channels = 0;
    size_t off = 0;
    void *out;

    memset(res, 0, sizeof(*res));
    res->fmt = fmt;

    dec = NeAACDecOpen();
    if (!dec)
        return -1;

    cfg = NeAACDecGetCurrentConfiguration(dec);
    cfg->defObjectType = LC; /* MAIN is unsupported in fixed-point builds */
    cfg->outputFormat = (unsigned char)fmt;
    if (!NeAACDecSetConfiguration(dec, cfg))
    {
        NeAACDecClose(dec);
        return -1;
    }

    if (NeAACDecInit(dec, (unsigned char *)data, (unsigned long)size,
                     &samplerate, &channels) < 0)
    {
        NeAACDecClose(dec);
        return -1;
    }

    if (!NeAACDecSetDRCLevel(dec, (unsigned char)level))
    {
        NeAACDecClose(dec);
        return -1;
    }

    res->stride = (fmt == FAAD_FMT_16BIT) ? 2 : ((fmt == FAAD_FMT_FLOAT) ? 4 : 8);

    while (off + 7 <= size && res->nframes < MAX_FRAMES)
    {
        long flen = adts_frame_length(data + off);
        int fi = res->nframes;
        if (flen <= 0 || off + (size_t)flen > size)
            break;

        if (fi == switch_frame)
        {
            if (!NeAACDecSetDRCLevel(dec, (unsigned char)switch_level))
            {
                NeAACDecClose(dec);
                return -1;
            }
        }

        out = NeAACDecDecode(dec, &info, (unsigned char *)(data + off),
                             (unsigned long)flen);
        if (info.error != 0)
        {
            fprintf(stderr, "decode error frame %d: %s\n", fi,
                    NeAACDecGetErrorMessage(info.error));
            NeAACDecClose(dec);
            return -1;
        }
        if (out == NULL)
        {
            fprintf(stderr, "no output for frame %d\n", fi);
            NeAACDecClose(dec);
            return -1;
        }

        if (!NeAACDecGetDRCFrameInfo(dec, &res->drc[fi]))
        {
            NeAACDecClose(dec);
            return -1;
        }

        res->frame_offset[fi] = res->total_samples;
        res->frame_samples[fi] = (long)info.samples;
        if (res->total_samples + (long)info.samples >
            (long)(sizeof(res->pcm) / res->stride))
        {
            NeAACDecClose(dec);
            return -1;
        }
        memcpy(res->pcm + res->total_samples * res->stride, out,
               (size_t)info.samples * res->stride);
        res->total_samples += (long)info.samples;
        res->nframes++;

        off += (size_t)flen;
    }

    NeAACDecClose(dec);
    return 0;
}

/* ------------------------------------------------------------------ */
/* test helpers                                                        */

static int failures = 0;

#define CHECK(cond, ...) do { \
    if (!(cond)) { \
        printf("FAIL %s:%d: ", __FILE__, __LINE__); \
        printf(__VA_ARGS__); \
        printf("\n"); \
        failures++; \
    } \
} while (0)

static int frames_bitexact(const decode_result *a, const decode_result *b,
                           int first_frame, int last_frame)
{
    int k, ok = 1;
    for (k = first_frame; k <= last_frame; k++)
    {
        long na = a->frame_samples[k], nb = b->frame_samples[k];
        if (na != nb)
        {
            printf("FAIL frame %d: sample count %ld != %ld\n", k, na, nb);
            failures++;
            ok = 0;
            continue;
        }
        if (na == 0)
            continue;
        if (memcmp(a->pcm + a->frame_offset[k] * a->stride,
                   b->pcm + b->frame_offset[k] * b->stride,
                   (size_t)na * a->stride) != 0)
        {
            printf("FAIL frame %d: PCM not bit-exact\n", k);
            failures++;
            ok = 0;
        }
    }
    return ok;
}

/* ------------------------------------------------------------------ */

int main(void)
{
    static unsigned char sample_stream[4096], ref_stream[4096], const_stream[4096];
    size_t sample_size, ref_size, const_size;
    decode_result r_off, r_ref, r_light, r_heavy, r_sw, r_all;
    decode_result c_off, c_heavy, c_light;
    int k;
    FILE *f;

    /* ---- generate the fixed samples (also written to disk) ---- */
    sample_size = build_stream(sample_stream, sample_meta, 0);
    {
        drc_meta m[NFRAMES];
        for (k = 0; k < NFRAMES; k++) m[k] = ref_meta;
        ref_size = build_stream(ref_stream, m, 0);
    }
    {
        drc_meta m[NFRAMES];
        for (k = 0; k < NFRAMES; k++) m[k] = const_meta;
        const_size = build_stream(const_stream, m, 0);
    }

    f = fopen("drc_sample.aac", "wb");
    if (f) { fwrite(sample_stream, 1, sample_size, f); fclose(f); }
    f = fopen("drc_ref.aac", "wb");
    if (f) { fwrite(ref_stream, 1, ref_size, f); fclose(f); }
    f = fopen("drc_const.aac", "wb");
    if (f) { fwrite(const_stream, 1, const_size, f); fclose(f); }

    /* ---- API sanity ---- */
    {
        NeAACDecHandle dec = NeAACDecOpen();
        CHECK(dec != NULL, "NeAACDecOpen failed");
        CHECK(NeAACDecGetDRCLevel(dec) == FAAD_DRC_HEAVY,
              "default level is not FAAD_DRC_HEAVY");
        CHECK(NeAACDecSetDRCLevel(dec, 3) == 0, "invalid level accepted");
        CHECK(NeAACDecGetDRCLevel(dec) == FAAD_DRC_HEAVY,
              "level changed by invalid set");
        CHECK(NeAACDecSetDRCLevel(dec, FAAD_DRC_LIGHT) == 1, "set LIGHT failed");
        CHECK(NeAACDecGetDRCLevel(dec) == FAAD_DRC_LIGHT, "level not LIGHT");
        NeAACDecClose(dec);
    }

    /* ---- 1. OFF is bit-exact identical to the zero-gain reference ---- */
    CHECK(decode_stream(sample_stream, sample_size, FAAD_DRC_OFF,
                        FAAD_FMT_16BIT, -1, 0, &r_off) == 0, "decode off failed");
    CHECK(decode_stream(ref_stream, ref_size, FAAD_DRC_HEAVY,
                        FAAD_FMT_16BIT, -1, 0, &r_ref) == 0, "decode ref failed");
    CHECK(r_off.nframes == NFRAMES && r_ref.nframes == NFRAMES,
          "unexpected frame count %d/%d", r_off.nframes, r_ref.nframes);
    frames_bitexact(&r_off, &r_ref, 0, NFRAMES - 1);

    /* same check in float output format (the fixed-point library only
       produces meaningful PCM in the integer formats) */
    if (!DRC_FIXED_POINT)
    {
        CHECK(decode_stream(sample_stream, sample_size, FAAD_DRC_OFF,
                            FAAD_FMT_FLOAT, -1, 0, &r_off) == 0, "decode off float failed");
        CHECK(decode_stream(ref_stream, ref_size, FAAD_DRC_HEAVY,
                            FAAD_FMT_FLOAT, -1, 0, &r_ref) == 0, "decode ref float failed");
        frames_bitexact(&r_off, &r_ref, 0, NFRAMES - 1);
    }

    /* ---- 2. reported per-frame gains match the metadata anchors ---- */
    CHECK(decode_stream(sample_stream, sample_size, FAAD_DRC_LIGHT,
                        FAAD_FMT_16BIT, -1, 0, &r_light) == 0, "decode light failed");
    CHECK(decode_stream(sample_stream, sample_size, FAAD_DRC_HEAVY,
                        FAAD_FMT_16BIT, -1, 0, &r_heavy) == 0, "decode heavy failed");
    for (k = 0; k < NFRAMES; k++)
    {
        const drc_meta *m = &sample_meta[k];
        double eg_light = expected_gain(FAAD_DRC_LIGHT, m->sgn, m->ctl, m->prog_ref);
        double eg_heavy = expected_gain(FAAD_DRC_HEAVY, m->sgn, m->ctl, m->prog_ref);

        CHECK(r_light.drc[k].present == 1, "frame %d: not present (light)", k);
        CHECK(r_light.drc[k].level == FAAD_DRC_LIGHT, "frame %d: wrong level", k);
        CHECK(r_light.drc[k].num_bands == 1, "frame %d: num_bands != 1", k);
        CHECK(fabs(r_light.drc[k].gain[0] - eg_light) <= 2e-6 * eg_light,
              "frame %d: light gain %.9g != expected %.9g",
              k, r_light.drc[k].gain[0], eg_light);

        CHECK(r_heavy.drc[k].present == 1, "frame %d: not present (heavy)", k);
        CHECK(r_heavy.drc[k].level == FAAD_DRC_HEAVY, "frame %d: wrong level", k);
        CHECK(fabs(r_heavy.drc[k].gain[0] - eg_heavy) <= 2e-6 * eg_heavy,
              "frame %d: heavy gain %.9g != expected %.9g",
              k, r_heavy.drc[k].gain[0], eg_heavy);

        CHECK(fabs(r_off.drc[k].gain[0] - 1.0) == 0.0,
              "frame %d: off gain != 1.0", k);
    }

    /* ---- 3. PCM is actually scaled by the reported gain ---- */
    /* float build: compare FAAD_FMT_FLOAT output with a tight tolerance;
       fixed-point build: compare FAAD_FMT_16BIT output with 1 LSB + 5% */
    {
        const int pcm_fmt = DRC_FIXED_POINT ? FAAD_FMT_16BIT : FAAD_FMT_FLOAT;
        const double tol = DRC_FIXED_POINT ? 5e-2 : 1e-5;
        const double abstol = DRC_FIXED_POINT ? 1.0 : 1e-9;
        const double sig = DRC_FIXED_POINT ? 0.25 : 1e-9;
        int nonzero = 0;
        double g_heavy, g_light;

        CHECK(decode_stream(const_stream, const_size, FAAD_DRC_OFF,
                            pcm_fmt, -1, 0, &c_off) == 0, "decode const off failed");
        CHECK(decode_stream(const_stream, const_size, FAAD_DRC_HEAVY,
                            pcm_fmt, -1, 0, &c_heavy) == 0, "decode const heavy failed");
        CHECK(decode_stream(const_stream, const_size, FAAD_DRC_LIGHT,
                            pcm_fmt, -1, 0, &c_light) == 0, "decode const light failed");

        g_heavy = expected_gain(FAAD_DRC_HEAVY, 1, 48, 80); /* 0.25 */
        g_light = expected_gain(FAAD_DRC_LIGHT, 1, 48, 80); /* 0.5  */

        for (k = 1; k < NFRAMES; k++)
        {
            long n = c_off.frame_samples[k];
            double maxabs = 0.0;
            long i;
            for (i = 0; i < n; i++)
            {
                double v = (pcm_fmt == FAAD_FMT_FLOAT)
                    ? (double)((const float *)(c_off.pcm + c_off.frame_offset[k] * 4))[i]
                    : (double)((const int16_t *)(c_off.pcm + c_off.frame_offset[k] * 2))[i];
                if (fabs(v) > maxabs) maxabs = fabs(v);
            }
            for (i = 0; i < n; i++)
            {
                double ref = (pcm_fmt == FAAD_FMT_FLOAT)
                    ? (double)((const float *)(c_off.pcm + c_off.frame_offset[k] * 4))[i]
                    : (double)((const int16_t *)(c_off.pcm + c_off.frame_offset[k] * 2))[i];
                double hv = (pcm_fmt == FAAD_FMT_FLOAT)
                    ? (double)((const float *)(c_heavy.pcm + c_heavy.frame_offset[k] * 4))[i]
                    : (double)((const int16_t *)(c_heavy.pcm + c_heavy.frame_offset[k] * 2))[i];
                double lv = (pcm_fmt == FAAD_FMT_FLOAT)
                    ? (double)((const float *)(c_light.pcm + c_light.frame_offset[k] * 4))[i]
                    : (double)((const int16_t *)(c_light.pcm + c_light.frame_offset[k] * 2))[i];
                if (fabs(ref) > sig * maxabs && fabs(ref) > abstol)
                {
                    nonzero++;
                    CHECK(fabs(hv - g_heavy * ref) <= tol * fabs(g_heavy * ref) + abstol,
                          "frame %d sample %ld: heavy %.9g != %.9g * %.9g",
                          k, i, hv, g_heavy, ref);
                    CHECK(fabs(lv - g_light * ref) <= tol * fabs(g_light * ref) + abstol,
                          "frame %d sample %ld: light %.9g != %.9g * %.9g",
                          k, i, lv, g_light, ref);
                }
            }
        }
        CHECK(nonzero > 0, "const stream decoded to all zeros");
    }

    /* ---- 4. runtime switching takes effect on the next frame ---- */
    /* HEAVY for frames 0..3, OFF from frame 4 on */
    CHECK(decode_stream(sample_stream, sample_size, FAAD_DRC_HEAVY,
                        FAAD_FMT_16BIT, 4, FAAD_DRC_OFF, &r_sw) == 0,
          "decode switch heavy->off failed");
    /* reference: OFF for the whole stream */
    CHECK(decode_stream(sample_stream, sample_size, FAAD_DRC_OFF,
                        FAAD_FMT_16BIT, -1, 0, &r_all) == 0, "decode all-off failed");
    /* the first frame after the switch already reports unity gain */
    CHECK(r_sw.drc[4].level == FAAD_DRC_OFF, "switch: level not OFF at frame 4");
    CHECK(r_sw.drc[4].gain[0] == 1.0,
          "switch: gain not 1.0 at frame 4 (got %.9g)", r_sw.drc[4].gain[0]);
    /* frame 4 still carries the HEAVY tail of frame 3 in its overlap;
       from frame 5 on the output must be bit-exact identical to the
       all-OFF decode */
    frames_bitexact(&r_sw, &r_all, 5, NFRAMES - 1);

    /* OFF for frames 0..3, HEAVY from frame 4 on */
    CHECK(decode_stream(sample_stream, sample_size, FAAD_DRC_OFF,
                        FAAD_FMT_16BIT, 4, FAAD_DRC_HEAVY, &r_sw) == 0,
          "decode switch off->heavy failed");
    CHECK(decode_stream(sample_stream, sample_size, FAAD_DRC_HEAVY,
                        FAAD_FMT_16BIT, -1, 0, &r_all) == 0, "decode all-heavy failed");
    {
        const drc_meta *m = &sample_meta[4];
        double eg = expected_gain(FAAD_DRC_HEAVY, m->sgn, m->ctl, m->prog_ref);
        CHECK(r_sw.drc[4].level == FAAD_DRC_HEAVY, "switch: level not HEAVY at frame 4");
        CHECK(fabs(r_sw.drc[4].gain[0] - eg) <= 2e-6 * eg,
              "switch: gain %.9g != expected %.9g at frame 4",
              r_sw.drc[4].gain[0], eg);
    }
    frames_bitexact(&r_sw, &r_all, 5, NFRAMES - 1);

    if (failures == 0)
        printf("All DRC tests passed.\n");
    else
        printf("%d DRC test failure(s).\n", failures);

    return failures ? 1 : 0;
}
