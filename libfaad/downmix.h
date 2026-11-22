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
 * See docs/downmix.md for the full matrix specification, the ITU/MPEG
 * coefficient tables and the documented peak control behaviour.
 */

#ifndef __DOWNMIX_H__
#define __DOWNMIX_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "common.h"
#include "neaacdec.h"

/* Maximum number of channel roles that can feed one output bus. */
#define DM_MAX_TERMS 6

/* Internal channel roles. The numeric values match the public
 * FRONT / BACK / SIDE / LFE channel definitions in neaacdec.h. */
enum
{
    DM_ROLE_C   = FRONT_CHANNEL_CENTER,  /* front center            */
    DM_ROLE_L   = FRONT_CHANNEL_LEFT,    /* front left              */
    DM_ROLE_R   = FRONT_CHANNEL_RIGHT,   /* front right             */
    DM_ROLE_SL  = SIDE_CHANNEL_LEFT,     /* side / surround left    */
    DM_ROLE_SR  = SIDE_CHANNEL_RIGHT,    /* side / surround right   */
    DM_ROLE_BL  = BACK_CHANNEL_LEFT,     /* back left               */
    DM_ROLE_BR  = BACK_CHANNEL_RIGHT,    /* back right              */
    DM_ROLE_BC  = BACK_CHANNEL_CENTER,   /* back center (6.1)       */
    DM_ROLE_LFE = LFE_CHANNEL            /* low frequency effects   */
};

/* One weighted contribution of an internal channel to an output bus. */
typedef struct
{
    uint8_t internal_index; /* index into the time_out channel array */
    real_t weight;          /* raw profile coefficient (1, 1/sqrt2, 1/2) */
} dm_term_t;

/* Fully resolved downmix plan for one frame. */
typedef struct
{
    uint8_t enabled;        /* 0: plan is empty, passthrough */
    uint8_t profile;        /* FAAD_DMX_PROFILE_* */
    uint8_t peak;           /* FAAD_DMX_PEAK_* */
    uint8_t legacy_5x;      /* 1: 5.0/5.1 layout, single surround pair */
    uint8_t n_left;         /* number of terms feeding the left bus */
    uint8_t n_right;        /* number of terms feeding the right bus */
    dm_term_t left[DM_MAX_TERMS];
    dm_term_t right[DM_MAX_TERMS];
    real_t gain;            /* post-sum gain: profile normalization,
                               possibly combined with peak scaling */
    real_t headroom;        /* 1/S, S = worst-case coherent sum of
                               absolute raw coefficients of this layout */
} downmix_plan_t;

/*
 * Build a downmix plan from the frame's channel positions.
 *
 * channel_position[i] / internal_channel[i] describe output channel i
 * (i = 0 .. channels-1); channels is the number of decoded full-band
 * channels including LFE.
 *
 * Returns 1 when the frame should be downmixed to stereo, 0 when the
 * layout cannot be represented (caller must pass through unchanged).
 */
uint8_t downmix_build_plan(downmix_plan_t *plan,
                           const uint8_t *channel_position,
                           const uint8_t *internal_channel,
                           uint8_t channels,
                           uint8_t profile, uint8_t peak);

/* Apply the plan to one sample of one output bus (0 = left, 1 = right).
 * The returned value is in the internal real_t scale, already including
 * the configured gain and any explicit clipping. */
real_t downmix_apply(const downmix_plan_t *plan, real_t **input,
                     uint16_t sample, uint8_t bus);

#ifdef __cplusplus
}
#endif
#endif
