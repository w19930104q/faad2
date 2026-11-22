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
** ADTS sample-accurate random access.
**
** The index is an immutable table of ADTS frame locations built in one
** linear scan; it references the caller's bitstream buffer read-only and is
** therefore safe to share between threads.  A seek restores the decoder to
** the exact internal state a continuous from-zero decode would have at the
** target frame by resetting the handle and fully decoding (and discarding)
** every preceding frame.  No shortcuts are taken: overlap-add history,
** predictors, PNS RNG state and SBR/PS state are all rebuilt the same way,
** which is what makes the result bit-identical for both plain AAC and
** HE-AAC (SBR/PS).
*/

#include "common.h"
#include "structs.h"

#include <stdlib.h>
#include <string.h>

#include "syntax.h"
#include "error.h"

/* ADTS fixed header is 7 bytes without CRC protection, 9 with CRC; the
 * frame length field always spans bytes 3..5. */
#define ADTS_HEADER_MIN_SIZE 7

typedef struct adts_frame_loc
{
    uint32_t offset;                 /* byte offset of the syncword */
    uint32_t length;                 /* total frame length in bytes  */
} adts_frame_loc;

struct NeAACDecADTSIndexStruct
{
    const unsigned char *data;       /* referenced, never owned        */
    unsigned long size;
    adts_frame_loc *frames;
    unsigned long frame_count;
    unsigned long samples_per_frame; /* output samples per channel     */
    unsigned long sample_rate;       /* output sample rate in Hz       */
};

/* Standard MPEG-2/4 sample rate table, indexed by the 4-bit ADTS field. */
static const uint32_t adts_sample_rates[16] =
{
    96000, 88200, 64000, 48000, 44100, 32000,
    24000, 22050, 16000, 12000, 11025, 8000, 7350, 0, 0, 0
};

/* Parse the fields of one ADTS header at data[pos]. Only structural header
 * validation is done (syncword, sane length, known sample rate); payload
 * correctness is left to the decoder. Returns 1 on success. */
static uint8_t adts_parse_header(const unsigned char *data,
                                 unsigned long size,
                                 unsigned long pos,
                                 unsigned long *frame_length,
                                 uint8_t *sf_index,
                                 uint8_t *channel_config)
{
    if (pos + ADTS_HEADER_MIN_SIZE > size)
        return 0;

    if ((data[pos] != 0xFF) || ((data[pos + 1] & 0xF6) != 0xF0))
        return 0;

    *sf_index = (uint8_t)((data[pos + 2] & 0x3C) >> 2);
    if (adts_sample_rates[*sf_index] == 0)
        return 0;

    *channel_config = (uint8_t)(((data[pos + 2] & 0x01) << 2) |
                                ((data[pos + 3] & 0xC0) >> 6));

    *frame_length = ((unsigned long)(data[pos + 3] & 0x03) << 11) |
                    ((unsigned long)data[pos + 4] << 3) |
                    ((unsigned long)(data[pos + 5] & 0xE0) >> 5);

    if (*frame_length < ADTS_HEADER_MIN_SIZE)
        return 0;
    if (pos + *frame_length > size)
        return 0;

    return 1;
}

static unsigned long idx_frame_offset(NeAACDecADTSIndex index,
                                      unsigned long frame_number)
{
    return (unsigned long)index->frames[frame_number].offset;
}

/* Core seek routine.  start_frame selects the frame counter convention for
 * the first priming frame, matching NeAACDecPostSeekReset():
 *   0 -> first frame counts as frame 0 (its output is suppressed, the
 *        standard library convention);
 *   1 -> first frame counts as frame 1 (output preserved, the convention
 *        used by the faad CLI continuous decode path). */
static unsigned char adts_seek_core(NeAACDecHandle decoder,
                                    NeAACDecADTSIndex index,
                                    unsigned long target_sample_position,
                                    long start_frame,
                                    unsigned long *target_frame_out,
                                    unsigned long *skipped_samples_out)
{
    NeAACDecStruct *hDecoder;
    unsigned long target_frame;
    unsigned long frame_offset;
    unsigned long samplerate;
    unsigned char channels;
    unsigned long f, off, len;
    unsigned char rc;
    NeAACDecFrameInfo fi;

    if (target_frame_out)
        *target_frame_out = 0;
    if (skipped_samples_out)
        *skipped_samples_out = 0;

    if (decoder == NULL || index == NULL || index->data == NULL)
        return NE_AAC_ADTS_ERR_INVALID;
    hDecoder = (NeAACDecStruct*)decoder;

    rc = NeAACDecADTSIndexLocateSample(index, target_sample_position,
                                       &target_frame, &frame_offset);
    if (rc != NE_AAC_ADTS_OK)
        return rc;
    (void)frame_offset;

    /* Tear down every piece of per-stream state (filter bank overlap,
     * predictors, SBR/PS, sample buffers, ...) and start from scratch.
     * The handle address and configuration are preserved. */
    NeAACDecReset(decoder);
    hDecoder = (NeAACDecStruct*)decoder;

    /* Re-establish stream parameters from the first ADTS frame. */
    off = idx_frame_offset(index, 0);
    len = (unsigned long)index->frames[0].length;
    if (NeAACDecInit(hDecoder, (unsigned char*)index->data + off, len,
                     &samplerate, &channels) < 0)
        return NE_AAC_ADTS_ERR_INVALID;

    /* Frame numbering must match a continuous decode, and the seek flag
     * primes the SBR/PS reset paths.  This MUST happen before the priming
     * loop; doing it afterwards leaves the SBR state one frame behind and
     * breaks bit-exactness. */
    NeAACDecPostSeekReset(hDecoder, start_frame);

    /* Rebuild the complete decoder state by replaying frames 0 .. target-1
     * exactly as a from-zero decode would. The produced PCM is discarded. */
    for (f = 0; f < target_frame; f++)
    {
        off = idx_frame_offset(index, f);
        len = (unsigned long)index->frames[f].length;
        memset(&fi, 0, sizeof(fi));
        if (NeAACDecDecode(hDecoder, &fi,
                           (unsigned char*)index->data + off, len) == NULL
            && fi.error != 0)
        {
            return NE_AAC_ADTS_ERR_INVALID;
        }
    }

    if (target_frame_out)
        *target_frame_out = target_frame;
    if (skipped_samples_out)
        *skipped_samples_out = target_frame * index->samples_per_frame;

    return NE_AAC_ADTS_OK;
}

/* Internal variant preserving the first frame's output, matching the
 * faad command line tool's NeAACDecPostSeekReset(hDecoder, 1) convention.
 * Not part of the public API. */
unsigned char NeAACDecADTSSeekCLI(NeAACDecHandle decoder,
                                  NeAACDecADTSIndex index,
                                  unsigned long target_sample_position,
                                  unsigned long *target_frame_out,
                                  unsigned long *skipped_samples_out)
{
    return adts_seek_core(decoder, index, target_sample_position, 1,
                          target_frame_out, skipped_samples_out);
}

unsigned char NeAACDecADTSIndexCreate(const unsigned char *data,
                                      unsigned long size,
                                      NeAACDecADTSIndex *index,
                                      unsigned long *frame_count_out,
                                      unsigned long *samplerate_out)
{
    NeAACDecADTSIndex idx = NULL;
    unsigned long pos = 0;
    unsigned long cap = 0;
    uint8_t sf_index = 0;
    uint8_t channel_config = 0;
    uint8_t have_params = 0;
    uint32_t core_rate;

    if (frame_count_out)
        *frame_count_out = 0;
    if (samplerate_out)
        *samplerate_out = 0;

    /* No place to publish the result: do not touch anything. */
    if (index == NULL)
        return NE_AAC_ADTS_ERR_INVALID;
    *index = NULL;

    if (data == NULL || size == 0)
        return NE_AAC_ADTS_ERR_INVALID;

    idx = (NeAACDecADTSIndex)faad_malloc(sizeof(*idx));
    if (idx == NULL)
        return NE_AAC_ADTS_ERR_NOMEM;
    memset(idx, 0, sizeof(*idx));

    while (pos + ADTS_HEADER_MIN_SIZE <= size)
    {
        unsigned long frame_length;
        uint8_t cur_sf, cur_ch;

        if (!adts_parse_header(data, size, pos, &frame_length,
                               &cur_sf, &cur_ch))
        {
            if (idx->frame_count == 0)
            {
                /* Still in the leading junk region: scan for the next
                 * sync candidate. */
                pos++;
                continue;
            }
            /* Once a frame chain has started, a broken header ends the
             * stream (trailing tags, padding, ...). */
            break;
        }

        if (idx->frame_count == 0)
        {
            /* Reject accidental syncwords in non-ADTS data (LATM, tags,
             * garbage): a real ADTS frame is followed by another valid
             * header exactly one declared frame length away, or it ends
             * precisely at end of file. */
            unsigned long next = pos + frame_length;
            unsigned long next_len;
            uint8_t next_sf, next_ch;

            if (next < size)
            {
                if (!adts_parse_header(data, size, next, &next_len,
                                       &next_sf, &next_ch) ||
                    next_sf != cur_sf || next_ch != cur_ch)
                {
                    pos++;
                    continue;
                }
            } else if (next != size) {
                pos++;
                continue;
            }

            sf_index = cur_sf;
            channel_config = cur_ch;
            have_params = 1;
        } else if (cur_sf != sf_index || cur_ch != channel_config) {
            /* A parameter change cannot be indexed with one sample rate;
             * stop at the boundary so the indexed region stays uniform. */
            break;
        }

        if (idx->frame_count == cap)
        {
            unsigned long new_cap = cap ? cap * 2 : 256;
            adts_frame_loc *grown = (adts_frame_loc*)faad_realloc(
                idx->frames, new_cap * sizeof(adts_frame_loc));
            if (grown == NULL)
            {
                if (idx->frames) faad_free(idx->frames);
                faad_free(idx);
                return NE_AAC_ADTS_ERR_NOMEM;
            }
            idx->frames = grown;
            cap = new_cap;
        }

        idx->frames[idx->frame_count].offset = (uint32_t)pos;
        idx->frames[idx->frame_count].length = (uint32_t)frame_length;
        idx->frame_count++;
        pos += frame_length;
    }

    if (!have_params || idx->frame_count == 0)
    {
        if (idx->frames) faad_free(idx->frames);
        faad_free(idx);
        return NE_AAC_ADTS_ERR_INVALID;
    }

    idx->data = data;
    idx->size = size;
    core_rate = adts_sample_rates[sf_index];

    /* Mirror NeAACDecInit(): with the default configuration an ADTS core
     * sample rate at or below 24 kHz is implicitly SBR upsampled, so the
     * decoder emits 2048 samples/frame at twice the core rate. */
    if (core_rate <= 24000)
    {
        idx->samples_per_frame = 2048;
        idx->sample_rate = core_rate * 2;
    } else {
        idx->samples_per_frame = 1024;
        idx->sample_rate = core_rate;
    }

    *index = idx;
    if (frame_count_out)
        *frame_count_out = idx->frame_count;
    if (samplerate_out)
        *samplerate_out = idx->sample_rate;

    return NE_AAC_ADTS_OK;
}

void NeAACDecADTSIndexDestroy(NeAACDecADTSIndex index)
{
    if (index == NULL)
        return;
    if (index->frames)
        faad_free(index->frames);
    faad_free(index);
}

unsigned long NeAACDecADTSIndexFrameCount(NeAACDecADTSIndex index)
{
    if (index == NULL)
        return 0;
    return index->frame_count;
}

unsigned long NeAACDecADTSIndexSampleCount(NeAACDecADTSIndex index)
{
    if (index == NULL)
        return 0;
    return index->frame_count * index->samples_per_frame;
}

unsigned long NeAACDecADTSIndexSampleRate(NeAACDecADTSIndex index)
{
    if (index == NULL)
        return 0;
    return index->sample_rate;
}

unsigned char NeAACDecADTSIndexLocateSample(
    NeAACDecADTSIndex index,
    unsigned long sample_position,
    unsigned long *frame_out,
    unsigned long *frame_sample_offset_out)
{
    unsigned long frame;

    if (index == NULL || frame_out == NULL)
        return NE_AAC_ADTS_ERR_INVALID;

    if (sample_position >= index->frame_count * index->samples_per_frame)
        return NE_AAC_ADTS_ERR_NOTFOUND;

    frame = sample_position / index->samples_per_frame;

    *frame_out = frame;
    if (frame_sample_offset_out)
        *frame_sample_offset_out = frame * index->samples_per_frame;
    return NE_AAC_ADTS_OK;
}

unsigned char NeAACDecADTSIndexLocateTime(
    NeAACDecADTSIndex index,
    double time_seconds,
    unsigned long *frame_out,
    double *time_offset_out)
{
    unsigned long sample_position;

    if (index == NULL || frame_out == NULL || index->sample_rate == 0)
        return NE_AAC_ADTS_ERR_INVALID;
    if (!(time_seconds >= 0.0)) /* rejects negatives and NaN */
        return NE_AAC_ADTS_ERR_INVALID;

    sample_position = (unsigned long)(time_seconds *
                                      (double)index->sample_rate);

    if (sample_position >= index->frame_count * index->samples_per_frame)
        return NE_AAC_ADTS_ERR_NOTFOUND;

    *frame_out = sample_position / index->samples_per_frame;
    if (time_offset_out)
    {
        /* Time at the start of the located frame, in seconds. */
        *time_offset_out = (double)(*frame_out * index->samples_per_frame) /
                           (double)index->sample_rate;
    }
    return NE_AAC_ADTS_OK;
}

unsigned char NeAACDecADTSIndexGetFrame(
    NeAACDecADTSIndex index,
    unsigned long frame_number,
    unsigned long *offset_out,
    unsigned long *length_out)
{
    if (index == NULL || frame_number >= index->frame_count)
        return NE_AAC_ADTS_ERR_NOTFOUND;

    if (offset_out)
        *offset_out = idx_frame_offset(index, frame_number);
    if (length_out)
        *length_out = (unsigned long)index->frames[frame_number].length;
    return NE_AAC_ADTS_OK;
}

unsigned char NeAACDecADTSSeek(NeAACDecHandle decoder,
                               NeAACDecADTSIndex index,
                               unsigned long target_sample_position,
                               unsigned long *target_frame_out,
                               void *reserved,
                               unsigned long *skipped_samples_out)
{
    (void)reserved;
    return adts_seek_core(decoder, index, target_sample_position, 0,
                          target_frame_out, skipped_samples_out);
}
