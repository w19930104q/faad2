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
**
** $Id: pns.c,v 1.39 2010/06/04 20:47:56 menno Exp $
**/

#include "common.h"
#include "structs.h"

#include "pns.h"


/* static function declarations */
static void gen_rand_vector(real_t *spec, int16_t scale_factor, uint16_t size,
                            uint8_t sub,
                            /* RNG states */ uint32_t *__r1, uint32_t *__r2);


/* The function gen_rand_vector(addr, size) generates a vector of length
   <size> with signed random values of average energy MEAN_NRG per random
   value. A suitable random number generator can be realized using one
   multiplication/accumulation per random value.
*/
static INLINE void gen_rand_vector(real_t *spec, int16_t scale_factor, uint16_t size,
                                   uint8_t sub,
                                   /* RNG states */ uint32_t *__r1, uint32_t *__r2)
{
#ifndef FIXED_POINT
    uint16_t i;
    real_t energy = 0.0;
    (void)sub;

    scale_factor = min(max(scale_factor, -120), 120);

    for (i = 0; i < size; i++)
    {
        real_t tmp = (real_t)(int32_t)ne_rng(__r1, __r2);
        spec[i] = tmp;
        energy += tmp*tmp;
    }

    if (energy > 0)
    {
        real_t scale = (real_t)1.0/(real_t)sqrt(energy);
        scale *= (real_t)pow(2.0, 0.25 * scale_factor);
        for (i = 0; i < size; i++)
        {
            spec[i] *= scale;
        }
    }
#else
    uint16_t i;
    real_t scale;
    int32_t log2_scale;
    uint64_t energy = 0;

    /* IMDCT pre-scaling */
    scale_factor -= 4 * sub;

    // 52 stands for 2**13 == 8192 factor; larger factor causes overflows later (in cfft).
    scale_factor = min(max(scale_factor, -(REAL_BITS * 4)), 52);

    /* Generate the random vector (same RNG sequence as the floating point
       build) and accumulate its energy. The values are shifted down by 8
       bits so that size * tmp^2 always fits in an uint64; the shift cancels
       out in the energy normalisation below. */
    for (i = 0; i < size; i++)
    {
        int32_t tmp = (int32_t)ne_rng(__r1, __r2) >> 8;
        spec[i] = tmp;
        energy += (uint64_t)((int64_t)tmp * (int64_t)tmp);
    }

    if (energy > 0)
    {
        /* Normalise to the exact energy signalled by the scale factor, like
           the floating point path does:
             scale = 2^(scale_factor/4) / sqrt(energy)
           computed in fixed point via log2/exp2:
             log2(scale) = scale_factor/4 - log2(energy)/2   [Q(REAL_BITS)] */
        log2_scale = (scale_factor * (1 << (REAL_BITS - 2)))
                   - (int32_t)(log2_int(energy) >> 1);
        /* The random values above are plain integers, not Q(REAL_BITS)
           fixed point, so the multiplier is kept in Q(COEF_BITS) and the
           product is shifted down by REAL_BITS below:
             spec = (tmp * scale_Q28) >> REAL_BITS
           Saturate the exponent at the largest value pow2_fix() can
           represent; it can only be reached by a near-zero random vector
           combined with the maximum scale factor, and keeps the multiply
           below from overflowing. */
        log2_scale += (COEF_BITS - REAL_BITS) << REAL_BITS;
        if (log2_scale > (16 << REAL_BITS))
            log2_scale = 16 << REAL_BITS;
        scale = pow2_fix(log2_scale);
        for (i = 0; i < size; i++)
        {
            spec[i] = MUL_R(spec[i], scale);
        }
    } else {
        for (i = 0; i < size; i++)
        {
            spec[i] = 0;
        }
    }
#endif
}

void pns_decode(ic_stream *ics_left, ic_stream *ics_right,
                real_t *spec_left, real_t *spec_right, uint16_t frame_len,
                uint8_t channel_pair, uint8_t object_type,
                /* RNG states */ uint32_t *__r1, uint32_t *__r2)
{
    uint8_t g, sfb, b;
    uint16_t begin, end;

    uint8_t group = 0;
    uint16_t nshort = frame_len >> 3;

    uint8_t sub = 0;

#ifdef FIXED_POINT
    /* IMDCT scaling */
    if (object_type == LD)
    {
        sub = 9 /*9*/;
    } else {
        if (ics_left->window_sequence == EIGHT_SHORT_SEQUENCE)
            sub = 7 /*7*/;
        else
            sub = 10 /*10*/;
    }
#else
    (void)object_type;
#endif

    for (g = 0; g < ics_left->num_window_groups; g++)
    {
        /* Do perceptual noise substitution decoding */
        for (b = 0; b < ics_left->window_group_length[g]; b++)
        {
            uint16_t base = group * nshort;
            for (sfb = 0; sfb < ics_left->max_sfb; sfb++)
            {
                uint32_t r1_dep = 0, r2_dep = 0;

                if (is_noise(ics_left, g, sfb))
                {
#ifdef LTP_DEC
                    /* Simultaneous use of LTP and PNS is not prevented in the
                       syntax. If both LTP, and PNS are enabled on the same
                       scalefactor band, PNS takes precedence, and no prediction
                       is applied to this band.
                    */
                    ics_left->ltp.long_used[sfb] = 0;
                    ics_left->ltp2.long_used[sfb] = 0;
#endif

#ifdef MAIN_DEC
                    /* For scalefactor bands coded using PNS the corresponding
                       predictors are switched to "off".
                    */
                    ics_left->pred.prediction_used[sfb] = 0;
#endif
                    /* swb_offset_max is per-window, not per-frame; clamping
                       group>0 offsets against it zeroed out begin/end. */
                    begin = min(base + ics_left->swb_offset[sfb], frame_len);
                    end = min(base + ics_left->swb_offset[sfb+1], frame_len);

                    r1_dep = *__r1;
                    r2_dep = *__r2;

                    /* Generate random vector */
                    gen_rand_vector(&spec_left[begin],
                        ics_left->scale_factors[g][sfb], end - begin, sub, __r1, __r2);
                }

/* From the spec:
   If the same scalefactor band and group is coded by perceptual noise
   substitution in both channels of a channel pair, the correlation of
   the noise signal can be controlled by means of the ms_used field: While
   the default noise generation process works independently for each channel
   (separate generation of random vectors), the same random vector is used
   for both channels if ms_used[] is set for a particular scalefactor band
   and group. In this case, no M/S stereo coding is carried out (because M/S
   stereo coding and noise substitution coding are mutually exclusive).
   If the same scalefactor band and group is coded by perceptual noise
   substitution in only one channel of a channel pair the setting of ms_used[]
   is not evaluated.
*/
                if ((ics_right != NULL)
                    && is_noise(ics_right, g, sfb))
                {
#ifdef LTP_DEC
                    /* See comment above. */
                    ics_right->ltp.long_used[sfb] = 0;
                    ics_right->ltp2.long_used[sfb] = 0;
#endif
#ifdef MAIN_DEC
                    /* See comment above. */
                    ics_right->pred.prediction_used[sfb] = 0;
#endif

                    if (channel_pair && is_noise(ics_left, g, sfb) &&
                        (((ics_left->ms_mask_present == 1) &&
                        (ics_left->ms_used[g][sfb])) ||
                        (ics_left->ms_mask_present == 2)))
                    {
                        /*uint16_t c;*/

                        begin = min(base + ics_right->swb_offset[sfb], frame_len);
                        end = min(base + ics_right->swb_offset[sfb+1], frame_len);

                        /* Generate random vector dependent on left channel*/
                        gen_rand_vector(&spec_right[begin],
                            ics_right->scale_factors[g][sfb], end - begin, sub, &r1_dep, &r2_dep);

                    } else /*if (ics_left->ms_mask_present == 0)*/ {
                        begin = min(base + ics_right->swb_offset[sfb], frame_len);
                        end = min(base + ics_right->swb_offset[sfb+1], frame_len);

                        /* Generate random vector */
                        gen_rand_vector(&spec_right[begin],
                            ics_right->scale_factors[g][sfb], end - begin, sub, __r1, __r2);
                    }
                }
            } /* sfb */
            group++;
        } /* b */
    } /* g */
}
