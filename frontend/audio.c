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
** $Id: audio.c,v 1.30 2015/01/22 09:40:52 knik Exp $
**/

#ifdef _WIN32
#include <io.h>
#endif
#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <string.h>
#include <neaacdec.h>
#include <stdint.h>

#include "unicode_support.h"
#include "audio.h"

static size_t write_wav_header(audio_file *aufile);
static size_t write_wav_extensible_header(audio_file *aufile, long channelMask);
static size_t write_audio_16bit(audio_file *aufile, void *sample_buffer,
                                unsigned int samples);
static size_t write_audio_24bit(audio_file *aufile, void *sample_buffer,
                                unsigned int samples);
static size_t write_audio_32bit(audio_file *aufile, void *sample_buffer,
                                unsigned int samples);
static size_t write_audio_float(audio_file *aufile, void *sample_buffer,
                                unsigned int samples);

/* Map a decoder channel position (neaacdec.h FRONT_CHANNEL_*, ...) to the
   corresponding Microsoft speaker bit used in dwChannelMask of a
   WAVEFORMATEXTENSIBLE structure. Returns 0 for positions that cannot be
   represented in a standard channel mask. */
static long channel_position_to_speaker_bit(unsigned char position)
{
    switch (position)
    {
    case FRONT_CHANNEL_LEFT:   return 0x00001L; /* SPEAKER_FRONT_LEFT */
    case FRONT_CHANNEL_RIGHT:  return 0x00002L; /* SPEAKER_FRONT_RIGHT */
    case FRONT_CHANNEL_CENTER: return 0x00004L; /* SPEAKER_FRONT_CENTER */
    case LFE_CHANNEL:          return 0x00008L; /* SPEAKER_LOW_FREQUENCY */
    case BACK_CHANNEL_LEFT:    return 0x00010L; /* SPEAKER_BACK_LEFT */
    case BACK_CHANNEL_RIGHT:   return 0x00020L; /* SPEAKER_BACK_RIGHT */
    case BACK_CHANNEL_CENTER:  return 0x00100L; /* SPEAKER_BACK_CENTER */
    case SIDE_CHANNEL_LEFT:    return 0x00200L; /* SPEAKER_SIDE_LEFT */
    case SIDE_CHANNEL_RIGHT:   return 0x00400L; /* SPEAKER_SIDE_RIGHT */
    default:                   return 0L;
    }
}

long faad_wave_channel_mask(const unsigned char *channelPosition,
                            unsigned int channels)
{
    unsigned int i;
    long channelMask = 0;

    /* WAVEFORMATEXTENSIBLE is only needed for multi-channel content;
       mono and stereo keep the plain PCM header. */
    if (channels < 3)
        return 0;

    for (i = 0; i < channels; i++)
    {
        long bit = channel_position_to_speaker_bit(channelPosition[i]);

        /* An unknown position, or two channels claiming the same speaker,
           cannot be described faithfully by a standard channel mask. */
        if (bit == 0 || (channelMask & bit))
            return 0;

        channelMask |= bit;
    }

    return channelMask;
}

/* Build the channel permutation so that interleaved frames are written in
   the speaker order defined by the channel mask (ascending speaker bit
   order, as required by WAVEFORMATEXTENSIBLE). channelOrder[slot] is the
   source channel index in the decoded bitstream. Falls back to the
   identity permutation (and a zero mask) when the layout cannot be
   represented. */
static void build_channel_order(audio_file *aufile)
{
    unsigned int i;
    unsigned long remaining;
    unsigned int slot = 0;

    for (i = 0; i < WAV_MAX_CHANNELS; i++)
        aufile->channelOrder[i] = (unsigned char)i;

    aufile->channelMask = 0;

    if (aufile->fileType != OUTPUT_WAV)
        return;

    aufile->channelMask = faad_wave_channel_mask(aufile->channelPosition,
                                                 aufile->channels);
    if (aufile->channelMask == 0)
        return;

    /* walk the set speaker bits from low to high (mask order) */
    remaining = (unsigned long)aufile->channelMask;
    while (remaining)
    {
        unsigned long bit = remaining & (0UL - remaining);

        remaining &= remaining - 1;
        for (i = 0; i < aufile->channels; i++)
        {
            if ((unsigned long)channel_position_to_speaker_bit(aufile->channelPosition[i]) == bit)
            {
                aufile->channelOrder[slot++] = (unsigned char)i;
                break;
            }
        }
    }
}

audio_file *open_audio_file(char *infile, int samplerate, int channels,
                            int outputFormat, int fileType,
                            unsigned char *channelPosition)
{
    audio_file *aufile = malloc(sizeof(audio_file));
    unsigned int i;

    aufile->outputFormat = outputFormat;

    aufile->samplerate = samplerate;
    aufile->channels = channels;
    aufile->total_samples = 0;
    aufile->fileType = fileType;
    aufile->channelMask = 0;

    for (i = 0; i < WAV_MAX_CHANNELS; i++)
        aufile->channelPosition[i] = (i < (unsigned int)channels && channelPosition)
            ? channelPosition[i] : UNKNOWN_CHANNEL;

    switch (outputFormat)
    {
    case FAAD_FMT_16BIT:
        aufile->bits_per_sample = 16;
        break;
    case FAAD_FMT_24BIT:
        aufile->bits_per_sample = 24;
        break;
    case FAAD_FMT_32BIT:
    case FAAD_FMT_FLOAT:
        aufile->bits_per_sample = 32;
        break;
    default:
        if (aufile) free(aufile);
        return NULL;
    }

    if(infile[0] == '-')
    {
#ifdef _WIN32
        _setmode(_fileno(stdout), O_BINARY);
#endif
        aufile->sndfile = stdout;
        aufile->toStdio = 1;
    } else {
        aufile->toStdio = 0;
        aufile->sndfile = faad_fopen(infile, "wb");
    }

    if (aufile->sndfile == NULL)
    {
        if (aufile) free(aufile);
        return NULL;
    }

    build_channel_order(aufile);

    if (aufile->fileType == OUTPUT_WAV)
    {
        if (aufile->channelMask)
            write_wav_extensible_header(aufile, aufile->channelMask);
        else
            write_wav_header(aufile);
    }

    return aufile;
}

size_t write_audio_file(audio_file *aufile, void *sample_buffer, int samples)
{
    char *buf = (char *)sample_buffer;
    switch (aufile->outputFormat)
    {
    case FAAD_FMT_16BIT:
        return write_audio_16bit(aufile, buf, samples);
    case FAAD_FMT_24BIT:
        return write_audio_24bit(aufile, buf, samples);
    case FAAD_FMT_32BIT:
        return write_audio_32bit(aufile, buf, samples);
    case FAAD_FMT_FLOAT:
        return write_audio_float(aufile, buf, samples);
    default:
        return 0;
    }
    // return 0;
}

void close_audio_file(audio_file *aufile)
{
    if ((aufile->fileType == OUTPUT_WAV) && (aufile->toStdio == 0))
    {
        fseek(aufile->sndfile, 0, SEEK_SET);

        if (aufile->channelMask)
            write_wav_extensible_header(aufile, aufile->channelMask);
        else
            write_wav_header(aufile);
    }

    if (aufile->toStdio == 0)
        fclose(aufile->sndfile);

    if (aufile) free(aufile);
}

static size_t write_wav_header(audio_file *aufile)
{
    unsigned char header[44];
    unsigned char* p = header;
    unsigned int bytes = (aufile->bits_per_sample + 7) / 8;
    float data_size = (float)bytes * aufile->total_samples;
    unsigned long word32;

    *p++ = 'R'; *p++ = 'I'; *p++ = 'F'; *p++ = 'F';

    word32 = (data_size + (44 - 8) < (float)MAXWAVESIZE) ?
        (unsigned long)data_size + (44 - 8)  :  (unsigned long)MAXWAVESIZE;
    *p++ = (unsigned char)(word32 >>  0);
    *p++ = (unsigned char)(word32 >>  8);
    *p++ = (unsigned char)(word32 >> 16);
    *p++ = (unsigned char)(word32 >> 24);

    *p++ = 'W'; *p++ = 'A'; *p++ = 'V'; *p++ = 'E';

    *p++ = 'f'; *p++ = 'm'; *p++ = 't'; *p++ = ' ';

    *p++ = 0x10; *p++ = 0x00; *p++ = 0x00; *p++ = 0x00;

    if (aufile->outputFormat == FAAD_FMT_FLOAT)
    {
        *p++ = 0x03; *p++ = 0x00;
    } else {
        *p++ = 0x01; *p++ = 0x00;
    }

    *p++ = (unsigned char)(aufile->channels >> 0);
    *p++ = (unsigned char)(aufile->channels >> 8);

    word32 = (unsigned long)(aufile->samplerate + 0.5);
    *p++ = (unsigned char)(word32 >>  0);
    *p++ = (unsigned char)(word32 >>  8);
    *p++ = (unsigned char)(word32 >> 16);
    *p++ = (unsigned char)(word32 >> 24);

    word32 = aufile->samplerate * bytes * aufile->channels;
    *p++ = (unsigned char)(word32 >>  0);
    *p++ = (unsigned char)(word32 >>  8);
    *p++ = (unsigned char)(word32 >> 16);
    *p++ = (unsigned char)(word32 >> 24);

    word32 = bytes * aufile->channels;
    *p++ = (unsigned char)(word32 >>  0);
    *p++ = (unsigned char)(word32 >>  8);

    *p++ = (unsigned char)(aufile->bits_per_sample >> 0);
    *p++ = (unsigned char)(aufile->bits_per_sample >> 8);

    *p++ = 'd'; *p++ = 'a'; *p++ = 't'; *p++ = 'a';

    word32 = data_size < MAXWAVESIZE ?
        (unsigned long)data_size : (unsigned long)MAXWAVESIZE;
    *p++ = (unsigned char)(word32 >>  0);
    *p++ = (unsigned char)(word32 >>  8);
    *p++ = (unsigned char)(word32 >> 16);
    *p++ = (unsigned char)(word32 >> 24);

    return fwrite(header, sizeof(header), 1, aufile->sndfile);
}

static size_t write_wav_extensible_header(audio_file *aufile, long channelMask)
{
    unsigned char header[68];
    unsigned char* p = header;
    unsigned int bytes = (aufile->bits_per_sample + 7) / 8;
    float data_size = (float)bytes * aufile->total_samples;
    unsigned long word32;

    *p++ = 'R'; *p++ = 'I'; *p++ = 'F'; *p++ = 'F';

    word32 = (data_size + (68 - 8) < (float)MAXWAVESIZE) ?
        (unsigned long)data_size + (68 - 8)  :  (unsigned long)MAXWAVESIZE;
    *p++ = (unsigned char)(word32 >>  0);
    *p++ = (unsigned char)(word32 >>  8);
    *p++ = (unsigned char)(word32 >> 16);
    *p++ = (unsigned char)(word32 >> 24);

    *p++ = 'W'; *p++ = 'A'; *p++ = 'V'; *p++ = 'E';

    *p++ = 'f'; *p++ = 'm'; *p++ = 't'; *p++ = ' ';

    *p++ = /*0x10*/0x28; *p++ = 0x00; *p++ = 0x00; *p++ = 0x00;

    /* WAVE_FORMAT_EXTENSIBLE */
    *p++ = 0xFE; *p++ = 0xFF;

    *p++ = (unsigned char)(aufile->channels >> 0);
    *p++ = (unsigned char)(aufile->channels >> 8);

    word32 = (unsigned long)(aufile->samplerate + 0.5);
    *p++ = (unsigned char)(word32 >>  0);
    *p++ = (unsigned char)(word32 >>  8);
    *p++ = (unsigned char)(word32 >> 16);
    *p++ = (unsigned char)(word32 >> 24);

    word32 = aufile->samplerate * bytes * aufile->channels;
    *p++ = (unsigned char)(word32 >>  0);
    *p++ = (unsigned char)(word32 >>  8);
    *p++ = (unsigned char)(word32 >> 16);
    *p++ = (unsigned char)(word32 >> 24);

    word32 = bytes * aufile->channels;
    *p++ = (unsigned char)(word32 >>  0);
    *p++ = (unsigned char)(word32 >>  8);

    *p++ = (unsigned char)(aufile->bits_per_sample >> 0);
    *p++ = (unsigned char)(aufile->bits_per_sample >> 8);

    /* cbSize */
    *p++ = (unsigned char)(22);
    *p++ = (unsigned char)(0);

    /* WAVEFORMATEXTENSIBLE */

    /* wValidBitsPerSample */
    *p++ = (unsigned char)(aufile->bits_per_sample >> 0);
    *p++ = (unsigned char)(aufile->bits_per_sample >> 8);

    /* dwChannelMask */
    word32 = channelMask;
    *p++ = (unsigned char)(word32 >>  0);
    *p++ = (unsigned char)(word32 >>  8);
    *p++ = (unsigned char)(word32 >> 16);
    *p++ = (unsigned char)(word32 >> 24);

    /* SubFormat */
    if (aufile->outputFormat == FAAD_FMT_FLOAT)
    {
        /* KSDATAFORMAT_SUBTYPE_IEEE_FLOAT: 00000003-0000-0010-8000-00aa00389b71 */
        *p++ = 0x03;
        *p++ = 0x00;
        *p++ = 0x00;
        *p++ = 0x00;
        *p++ = 0x00; *p++ = 0x00; *p++ = 0x10; *p++ = 0x00; *p++ = 0x80; *p++ = 0x00;
        *p++ = 0x00; *p++ = 0xaa; *p++ = 0x00; *p++ = 0x38; *p++ = 0x9b; *p++ = 0x71;
    } else {
        /* KSDATAFORMAT_SUBTYPE_PCM: 00000001-0000-0010-8000-00aa00389b71 */
        *p++ = 0x01;
        *p++ = 0x00;
        *p++ = 0x00;
        *p++ = 0x00;
        *p++ = 0x00; *p++ = 0x00; *p++ = 0x10; *p++ = 0x00; *p++ = 0x80; *p++ = 0x00;
        *p++ = 0x00; *p++ = 0xaa; *p++ = 0x00; *p++ = 0x38; *p++ = 0x9b; *p++ = 0x71;
    }

    /* end WAVEFORMATEXTENSIBLE */

    *p++ = 'd'; *p++ = 'a'; *p++ = 't'; *p++ = 'a';

    word32 = data_size < MAXWAVESIZE ?
        (unsigned long)data_size : (unsigned long)MAXWAVESIZE;
    *p++ = (unsigned char)(word32 >>  0);
    *p++ = (unsigned char)(word32 >>  8);
    *p++ = (unsigned char)(word32 >> 16);
    *p++ = (unsigned char)(word32 >> 24);

    return fwrite(header, sizeof(header), 1, aufile->sndfile);
}

static void reorder_channels(audio_file *aufile, void *sample_buffer,
                             unsigned int samples, unsigned int sample_size)
{
    unsigned char *buf = (unsigned char *)sample_buffer;
    unsigned int channels = aufile->channels;
    unsigned int frames, f, slot;

    if (aufile->channelMask == 0 || channels < 2)
        return;

    frames = samples / channels;
    for (f = 0; f < frames; f++)
    {
        unsigned char frame[WAV_MAX_CHANNELS * 4]; /* largest sample: 32 bit */
        unsigned char *src = buf + (size_t)f * channels * sample_size;

        for (slot = 0; slot < channels; slot++)
        {
            memcpy(frame + (size_t)slot * sample_size,
                   src + (size_t)aufile->channelOrder[slot] * sample_size,
                   sample_size);
        }
        memcpy(src, frame, (size_t)channels * sample_size);
    }
}

static size_t write_audio_16bit(audio_file *aufile, void *sample_buffer,
                                unsigned int samples)
{
    size_t ret;
    unsigned int i;
    short *sample_buffer16 = (short*)sample_buffer;
    char *data = malloc(samples*aufile->bits_per_sample*sizeof(char)/8);

    aufile->total_samples += samples;

    reorder_channels(aufile, sample_buffer, samples, sizeof(short));

    for (i = 0; i < samples; i++)
    {
        data[i*2] = (char)(sample_buffer16[i] & 0xFF);
        data[i*2+1] = (char)((sample_buffer16[i] >> 8) & 0xFF);
    }

    ret = fwrite(data, samples, aufile->bits_per_sample/8, aufile->sndfile);

    if (data) free(data);

    return ret;
}

static size_t write_audio_24bit(audio_file *aufile, void *sample_buffer,
                                unsigned int samples)
{
    size_t ret;
    unsigned int i;
    int32_t *sample_buffer24 = (int32_t*)sample_buffer;
    char *data = malloc(samples*aufile->bits_per_sample*sizeof(char)/8);

    aufile->total_samples += samples;

    reorder_channels(aufile, sample_buffer, samples, sizeof(int32_t));

    for (i = 0; i < samples; i++)
    {
        data[i*3] = (char)(sample_buffer24[i] & 0xFF);
        data[i*3+1] = (char)((sample_buffer24[i] >> 8) & 0xFF);
        data[i*3+2] = (char)((sample_buffer24[i] >> 16) & 0xFF);
    }

    ret = fwrite(data, samples, aufile->bits_per_sample/8, aufile->sndfile);

    if (data) free(data);

    return ret;
}

static size_t write_audio_32bit(audio_file *aufile, void *sample_buffer,
                                unsigned int samples)
{
    size_t ret;
    unsigned int i;
    int32_t *sample_buffer32 = (int32_t*)sample_buffer;
    char *data = malloc(samples*aufile->bits_per_sample*sizeof(char)/8);

    aufile->total_samples += samples;

    reorder_channels(aufile, sample_buffer, samples, sizeof(int32_t));

    for (i = 0; i < samples; i++)
    {
        data[i*4] = (char)(sample_buffer32[i] & 0xFF);
        data[i*4+1] = (char)((sample_buffer32[i] >> 8) & 0xFF);
        data[i*4+2] = (char)((sample_buffer32[i] >> 16) & 0xFF);
        data[i*4+3] = (char)((sample_buffer32[i] >> 24) & 0xFF);
    }

    ret = fwrite(data, samples, aufile->bits_per_sample/8, aufile->sndfile);

    if (data) free(data);

    return ret;
}

static size_t write_audio_float(audio_file *aufile, void *sample_buffer,
                                unsigned int samples)
{
    size_t ret;
    unsigned int i;
    float *sample_buffer_f = (float*)sample_buffer;
    unsigned char *data = malloc(samples*aufile->bits_per_sample*sizeof(char)/8);

    aufile->total_samples += samples;

    reorder_channels(aufile, sample_buffer, samples, sizeof(float));

    for (i = 0; i < samples; i++)
    {
        /* Standard IEEE-754 binary32 bitcast; WAVE float data is little
           endian, same as the host endianness assumed by this frontend
           (see the PCM writers above). Preserves +/-0, inf and NaN. */
        union { float f; uint32_t u; } cvt;
        uint32_t bits;

        cvt.f = sample_buffer_f[i];
        bits = cvt.u;
        data[i*4]   = (char)(bits & 0xFF);
        data[i*4+1] = (char)((bits >> 8) & 0xFF);
        data[i*4+2] = (char)((bits >> 16) & 0xFF);
        data[i*4+3] = (char)((bits >> 24) & 0xFF);
    }

    ret = fwrite(data, samples, aufile->bits_per_sample/8, aufile->sndfile);

    if (data) free(data);

    return ret;
}
