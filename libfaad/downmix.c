/*
** FAAD2 - Freeware Advanced Audio (AAC) Decoder including SBR decoding
** Copyright (C) 2003-2005 M. Bakker, Nero AG, http://www.nero.com
**
** This program is free software; you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation; either version 2 of the License, or
** (at your option) any later version.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
** GNU General Public License for more details.
**
** You should have received a copy of the GNU General Public License
** along with this program; if not, write to the Free Software
** Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
**
** Any non-GPL usage of this software or parts of this software is strictly
** forbidden.
**
** The "appropriate copyright message" mentioned in section 2c of the GPLv2
** must read: "Code from FAAD2 is copyright (c) Nero AG, www.nero.com"
**
** Commercial non-GPL licensing of this software is possible.
** For more info contact Nero AG through Mpeg4AAClicense@nero.com.
**/

/*
 * Configurable multi-channel to stereo downmix.
 *
 * The coefficient and gain tables, the layout classification and the
 * peak control behaviour implemented here are fully specified in
 * docs/downmix.md. Keep that document in sync with this file.
 */

#include "common.h"
#include "structs.h"
#include "downmix.h"

/*
 * Coefficient Q-format note (fixed-point build):
 *   time_out samples are Q14 (REAL_BITS=14, full scale 1<<14), while
 *   MUL_F shifts by FRAC_BITS=31. Coefficients therefore have to be Q31
 *   unit fractions built with FRAC_CONST; the float build uses plain
 *   REAL_CONST. DM_COEF() abstracts the difference.
 */
#ifdef FIXED_POINT
#define DM_COEF(A) FRAC_CONST(A)
#else
#define DM_COEF(A) REAL_CONST(A)
#endif

/* 1/sqrt(2): raw ITU-R BS.775 center / surround coefficient */
#define DM_RSQRT2 DM_COEF(0.7071067811865475244)
/* 1/2: raw coefficient for a back-center channel split to both buses */
#define DM_HALF   DM_COEF(0.5)
/* unity direct gain for L/R */
#define DM_ONE    DM_COEF(1.0)

/* Historical faad2 5.1 normalization:
 * 1/(1 + sqrt(2) + 1/sqrt(2)) */
#define DM_MUL_LEGACY DM_COEF(0.3203772410170407)

/* Internal full-scale magnitude of the float time_out buffers. The PCM
 * converters multiply by FLOAT_SCALE = 1/(1<<15) for float/double
 * output, so clipping here at +/- (1<<15) corresponds to +/- 1.0 in the
 * documented float output scale. */
#ifndef FIXED_POINT
#define DM_FULLSCALE REAL_CONST(32768.0)
#endif

/*
 * Documented headroom constants 1/S for the fixed-point build, which
 * has no runtime division. S is the largest sum of absolute raw
 * coefficients on either output bus:
 *   single surround pair, no back center (5.x):
 *     S = 1 + 2/sqrt(2),              1/S = 0.70710678118654752
 *   one surround pair + back center (6.x):
 *     S = 1 + 2/sqrt(2) + 1/2,        1/S = 0.34314575050761978
 *   two surround pairs, no back center (7.x):
 *     S = 1 + 3/sqrt(2),              1/S = 0.32037724101704074
 */
#ifdef FIXED_POINT
#define DM_HEADROOM_5 DM_COEF(0.70710678118654752)
#define DM_HEADROOM_6 DM_COEF(0.34314575050761978)
#define DM_HEADROOM_7 DM_COEF(0.32037724101704074)
#endif

static void add_term(dm_term_t *bus, uint8_t *count,
                     uint8_t internal_index, real_t weight)
{
    if (*count >= DM_MAX_TERMS)
        return;
    bus[*count].internal_index = internal_index;
    bus[*count].weight = weight;
    (*count)++;
}

#ifndef FIXED_POINT
static real_t sum_abs_weights(const dm_term_t *bus, uint8_t count)
{
    uint8_t i;
    real_t s = 0;
    for (i = 0; i < count; i++)
        s += (bus[i].weight < 0) ? -bus[i].weight : bus[i].weight;
    return s;
}
#endif

uint8_t downmix_build_plan(downmix_plan_t *plan,
                           const uint8_t *channel_position,
                           const uint8_t *internal_channel,
                           uint8_t channels,
                           uint8_t profile, uint8_t peak)
{
    uint8_t i;
    uint8_t have_l = 0, have_r = 0;
    uint8_t have_legacy_surround = 0; /* exactly one surround pair: 5.x */
    uint8_t surround_pairs = 0;        /* side/back stereo pairs present */
    uint8_t have_bc = 0;
#ifdef FIXED_POINT
    real_t headroom;
#else
    real_t s_left, s_right, s_worst, headroom;
#endif

    memset(plan, 0, sizeof(downmix_plan_t));

    if (channels < 5)
        return 0;

    plan->profile = profile;
    plan->peak = peak;

    /* First pass: direct L/R. The term order (direct L/R, center,
     * surround pair(s), back center) matches the historical 5.1
     * accumulation order so the float MPEG/DEFAULT 5.1 path stays bit
     * identical. */
    for (i = 0; i < channels; i++)
    {
        uint8_t role = channel_position[i];
        uint8_t idx = internal_channel[i];

        switch (role)
        {
        case DM_ROLE_L:
            add_term(plan->left, &plan->n_left, idx, DM_ONE);
            have_l = 1;
            break;
        case DM_ROLE_R:
            add_term(plan->right, &plan->n_right, idx, DM_ONE);
            have_r = 1;
            break;
        default:
            break;
        }
    }

    for (i = 0; i < channels; i++)
    {
        uint8_t role = channel_position[i];
        uint8_t idx = internal_channel[i];

        switch (role)
        {
        case DM_ROLE_C:
            add_term(plan->left, &plan->n_left, idx, DM_RSQRT2);
            add_term(plan->right, &plan->n_right, idx, DM_RSQRT2);
            break;
        case DM_ROLE_SL:
        case DM_ROLE_BL:
            add_term(plan->left, &plan->n_left, idx, DM_RSQRT2);
            surround_pairs++;
            break;
        case DM_ROLE_SR:
        case DM_ROLE_BR:
            add_term(plan->right, &plan->n_right, idx, DM_RSQRT2);
            break;
        case DM_ROLE_BC:
            add_term(plan->left, &plan->n_left, idx, DM_HALF);
            add_term(plan->right, &plan->n_right, idx, DM_HALF);
            have_bc = 1;
            break;
        case DM_ROLE_LFE:
        case DM_ROLE_L:
        case DM_ROLE_R:
        default:
            /* LFE is discarded by a stereo downmix; L/R handled above */
            break;
        }
    }

    if (!have_l || !have_r)
        return 0;

    /* Layout classification against the raw ITU family coefficients.
     * A "surround pair" is a side or back L/R pair. */
    if (surround_pairs == 1 && !have_bc)
        have_legacy_surround = 1;

    /*
     * Headroom factor 1/S, S = largest sum of absolute raw coefficients
     * on either output bus.
     */
#ifdef FIXED_POINT
    if (have_legacy_surround)
        headroom = DM_HEADROOM_5;
    else if (have_bc)
        headroom = DM_HEADROOM_6;
    else
        headroom = DM_HEADROOM_7;
#else
    s_left = sum_abs_weights(plan->left, plan->n_left);
    s_right = sum_abs_weights(plan->right, plan->n_right);
    s_worst = (s_left > s_right) ? s_left : s_right;
    headroom = REAL_CONST(1.0) / s_worst;
#endif
    plan->headroom = headroom;

    /*
     * Post-sum gain.
     *
     * MPEG profile:
     *   - 5.0/5.1 (single surround pair, no back center): the historical
     *     faad2 constant, bit identical to the original downmix.
     *   - 6.1/7.1 and every other extended layout: the documented 1/S
     *     layout normalization.
     * ITU profile: unity gain unless peak scaling is requested.
     */
    if (profile == FAAD_DMX_PROFILE_MPEG)
    {
        if (have_legacy_surround)
            plan->gain = DM_MUL_LEGACY;
        else
            plan->gain = headroom;

        if (peak == FAAD_DMX_PEAK_SCALE)
            plan->gain = headroom;
    } else { /* FAAD_DMX_PROFILE_ITU */
        plan->gain = (peak == FAAD_DMX_PEAK_SCALE) ? headroom
                                                   : DM_ONE;
    }

    plan->legacy_5x = have_legacy_surround;
    plan->enabled = 1;
    return 1;
}

static INLINE real_t apply_bus(const downmix_plan_t *plan,
                               real_t **input, uint16_t sample,
                               const dm_term_t *terms, uint8_t count)
{
    uint8_t i;
    real_t acc = 0;

#ifdef FIXED_POINT
    /*
     * Bit-exact legacy fixed-point 5.0/5.1 path. The original output.c
     * folded the normalization into two constants and accumulated in a
     * specific order:
     *   BOTH = DM_MUL * 1/sqrt(2)
     *   L = DM_MUL*Lfront + BOTH*C + BOTH*Ls
     *   R = DM_MUL*Rfront + BOTH*C + BOTH*Rs
     * Reproducing it here keeps the fixed MPEG/DEFAULT 5.1 output
     * identical to historical faad2. Terms are ordered L/R, C, surround.
     */
    if (plan->legacy_5x && plan->profile == FAAD_DMX_PROFILE_MPEG
        && plan->peak != FAAD_DMX_PEAK_SCALE)
    {
        const real_t both = MUL_F(DM_MUL_LEGACY, DM_RSQRT2);
        real_t core = MUL_F(input[terms[0].internal_index][sample], DM_MUL_LEGACY);
        real_t c = MUL_F(input[terms[1].internal_index][sample], both);
        real_t s = MUL_F(input[terms[2].internal_index][sample], both);
        return core + c + s;
    }
#endif

    for (i = 0; i < count; i++)
        acc += MUL_F(input[terms[i].internal_index][sample], terms[i].weight);

    acc = MUL_F(acc, plan->gain);

#ifdef FIXED_POINT
    /* Fixed-point PCM conversion saturates per output format; see the
     * PEAK_CLIP documentation in docs/downmix.md. No clip is applied on
     * the internal Q-format value. */
#else
    /* Explicit full-scale clipping is only part of the ITU profile's
     * PEAK_CLIP policy; the MPEG profile is normalization-only and the
     * integer PCM converters saturate per output format anyway. The
     * threshold uses the internal +/-32768 time_out scale, i.e. +/- 1.0
     * after the float-output FLOAT_SCALE conversion. */
    if (plan->profile == FAAD_DMX_PROFILE_ITU &&
        plan->peak == FAAD_DMX_PEAK_CLIP)
    {
        if (acc > DM_FULLSCALE)
            acc = DM_FULLSCALE;
        else if (acc < -DM_FULLSCALE)
            acc = -DM_FULLSCALE;
    }
#endif

    return acc;
}

real_t downmix_apply(const downmix_plan_t *plan, real_t **input,
                     uint16_t sample, uint8_t bus)
{
    if (bus == 0)
        return apply_bus(plan, input, sample, plan->left, plan->n_left);
    return apply_bus(plan, input, sample, plan->right, plan->n_right);
}
