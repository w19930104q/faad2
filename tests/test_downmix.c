/*
 * Unit tests for the configurable stereo downmix (libfaad/downmix.c).
 *
 * The expected values embedded below are recomputed independently by
 * tests/downmix_ref.py from the documented formula (docs/downmix.md).
 * Run:
 *   python3 -I tests/downmix_ref.py            # regenerates expected.h
 *   cc -Ilibfaad -Iinclude tests/test_downmix.c libfaad/downmix.c -lm
 *
 * "Bit exact" below means exact comparison of 32-bit float values for
 * the no-peak-control path.
 */
#include <stdio.h>
#include <string.h>
#include <math.h>

#include "downmix.h"
#include "downmix_expected.h"

#define N 8 /* samples per channel */

static int failures = 0;
static int checks = 0;

static int feq(float a, float b)
{
    checks++;
    if (memcmp(&a, &b, sizeof(float)) == 0)
        return 1;
    failures++;
    printf("  MISMATCH: got %.9g (0x%08x) expected %.9g (0x%08x)\n",
           a, *(unsigned *)&a, b, *(unsigned *)&b);
    return 0;
}

/* build a synthetic input matrix from a per-channel sample table */
static void fill_input(float in[DM_MAX_TERMS][N], float **input, int nch)
{
    int i;
    for (i = 0; i < nch; i++)
        input[i] = in[i];
}

static const uint8_t pos_51[6] = {
    FRONT_CHANNEL_CENTER, FRONT_CHANNEL_LEFT, FRONT_CHANNEL_RIGHT,
    BACK_CHANNEL_LEFT, BACK_CHANNEL_RIGHT, LFE_CHANNEL };
static const uint8_t pos_61[7] = {
    FRONT_CHANNEL_CENTER, FRONT_CHANNEL_LEFT, FRONT_CHANNEL_RIGHT,
    SIDE_CHANNEL_LEFT, SIDE_CHANNEL_RIGHT, BACK_CHANNEL_CENTER, LFE_CHANNEL };
static const uint8_t pos_71[8] = {
    FRONT_CHANNEL_CENTER, FRONT_CHANNEL_LEFT, FRONT_CHANNEL_RIGHT,
    SIDE_CHANNEL_LEFT, SIDE_CHANNEL_RIGHT,
    BACK_CHANNEL_LEFT, BACK_CHANNEL_RIGHT, LFE_CHANNEL };

/* Distinct per-channel sample values, including values that push the
 * ITU mix beyond full scale to exercise peak control. */
static float in51[6][N];
static float in61[7][N];
static float in71[8][N];

static float *input51[6];
static float *input61[7];
static float *input71[8];

/* Internal time_out scale: full-scale magnitude is 32768; the PCM
 * converters divide by 32768 (FLOAT_SCALE) for float output. */
#define FS 32768.0f

static void make_inputs(void)
{
    int ch, i;
    for (i = 0; i < N; i++)
    {
        for (ch = 0; ch < 6; ch++)
            in51[ch][i] = (float)(0.10 * (ch + 1) + 0.01 * i) * FS;
        for (ch = 0; ch < 7; ch++)
            in61[ch][i] = (float)(0.09 * (ch + 1) + 0.01 * i) * FS;
        for (ch = 0; ch < 8; ch++)
            in71[ch][i] = (float)(0.08 * (ch + 1) + 0.01 * i) * FS;
    }
    /* extreme sample: coherent full-scale on all full-band channels */
    for (ch = 0; ch < 6; ch++) in51[ch][N-1] = (ch == 5) ? 0.3f * FS : FS;
    for (ch = 0; ch < 7; ch++) in61[ch][N-1] = (ch == 6) ? 0.3f * FS : FS;
    for (ch = 0; ch < 8; ch++) in71[ch][N-1] = (ch == 7) ? 0.3f * FS : FS;

    fill_input(in51, input51, 6);
    fill_input(in61, input61, 7);
    fill_input(in71, input71, 8);
}

typedef struct
{
    const char *name;
    const uint8_t *pos;
    const uint8_t *map;
    uint8_t channels;
    float **input;
    const dmx_expected_t *expected;
} layout_case_t;

static void run_case(const layout_case_t *tc, uint8_t profile, uint8_t peak)
{
    downmix_plan_t plan;
    int ok = downmix_build_plan(&plan, tc->pos, tc->map, tc->channels,
                                profile, peak);
    if (!ok) { printf("FAIL %s: build plan rejected layout\n", tc->name); failures++; return; }

    for (int i = 0; i < N; i++)
    {
        float l = downmix_apply(&plan, tc->input, (uint16_t)i, 0);
        float r = downmix_apply(&plan, tc->input, (uint16_t)i, 1);
        const dmx_expected_t *e = &tc->expected[profile * 3 * N + peak * N + i];
        feq(l, e->left);
        feq(r, e->right);
    }
}

int main(void)
{
    static const uint8_t identity[8] = {0,1,2,3,4,5,6,7};
    layout_case_t cases[] = {
        { "5.1", pos_51, identity, 6, input51, EXPECTED_51 },
        { "6.1", pos_61, identity, 7, input61, EXPECTED_61 },
        { "7.1", pos_71, identity, 8, input71, EXPECTED_71 },
    };
    int c;

    make_inputs();

    for (c = 0; c < 3; c++)
    {
        uint8_t profile, peak;
        for (profile = 0; profile <= 1; profile++)
            for (peak = 0; peak <= 2; peak++)
            {
                printf("case %s profile=%s peak=%d\n", cases[c].name,
                       profile == 0 ? "MPEG" : "ITU", peak);
                run_case(&cases[c], profile, peak);
            }
    }

    /* plan rejection: no stereo pair must fall back */
    {
        downmix_plan_t plan;
        const uint8_t pos_mono[5] = { FRONT_CHANNEL_CENTER, BACK_CHANNEL_CENTER,
            LFE_CHANNEL, LFE_CHANNEL, LFE_CHANNEL };
        if (downmix_build_plan(&plan, pos_mono, identity, 5,
                               FAAD_DMX_PROFILE_ITU, FAAD_DMX_PEAK_DEFAULT))
        {
            printf("FAIL: layout without stereo pair was accepted\n");
            failures++;
        }
    }

    /* hard clip keeps every float sample within +/- 1.0 (ITU only;
     * the MPEG profile never clips explicitly) */
    for (c = 0; c < 3; c++)
    {
        downmix_plan_t plan;
        downmix_build_plan(&plan, cases[c].pos, identity, cases[c].channels,
                           FAAD_DMX_PROFILE_ITU, FAAD_DMX_PEAK_CLIP);
        float l = downmix_apply(&plan, cases[c].input, N-1, 0);
        float r = downmix_apply(&plan, cases[c].input, N-1, 1);
        if (l > FS || l < -FS || r > FS || r < -FS)
        {
            printf("FAIL %s: PEAK_CLIP produced out-of-range sample\n",
                   cases[c].name);
            failures++;
        }
        checks += 2;

        /* MPEG + CLIP must be identical to MPEG + DEFAULT */
        downmix_plan_t p_def, p_clip;
        downmix_build_plan(&p_def, cases[c].pos, identity, cases[c].channels,
                           FAAD_DMX_PROFILE_MPEG, FAAD_DMX_PEAK_DEFAULT);
        downmix_build_plan(&p_clip, cases[c].pos, identity, cases[c].channels,
                           FAAD_DMX_PROFILE_MPEG, FAAD_DMX_PEAK_CLIP);
        for (int i = 0; i < N; i++)
        {
            feq(downmix_apply(&p_clip, cases[c].input, i, 0),
                downmix_apply(&p_def, cases[c].input, i, 0));
            feq(downmix_apply(&p_clip, cases[c].input, i, 1),
                downmix_apply(&p_def, cases[c].input, i, 1));
        }
    }

    printf("\n%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
