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
** $Id: neaacdec.h,v 1.14 2012/03/02 15:29:47 knik Exp $
**/

#ifndef __NEAACDEC_H__
#define __NEAACDEC_H__

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


#if 1
/* MACROS FOR BACKWARDS COMPATIBILITY */
/* structs */
#define faacDecHandle                  NeAACDecHandle
#define faacDecConfiguration           NeAACDecConfiguration
#define faacDecConfigurationPtr        NeAACDecConfigurationPtr
#define faacDecFrameInfo               NeAACDecFrameInfo
/* functions */
#define faacDecGetErrorMessage         NeAACDecGetErrorMessage
#define faacDecSetConfiguration        NeAACDecSetConfiguration
#define faacDecGetCurrentConfiguration NeAACDecGetCurrentConfiguration
#define faacDecInit                    NeAACDecInit
#define faacDecInit2                   NeAACDecInit2
#define faacDecInitDRM                 NeAACDecInitDRM
#define faacDecPostSeekReset           NeAACDecPostSeekReset
#define faacDecOpen                    NeAACDecOpen
#define faacDecClose                   NeAACDecClose
#define faacDecDecode                  NeAACDecDecode
#define AudioSpecificConfig            NeAACDecAudioSpecificConfig
#endif


#ifdef _WIN32
  #pragma pack(push, 8)
  #define NEAACDECAPI __declspec(dllexport)
#elif defined(__GNUC__) && __GNUC__ >= 4
  #define NEAACDECAPI __attribute__((visibility("default")))
#else
  #define NEAACDECAPI
#endif

#ifndef FAAD2_VERSION
  #define FAAD2_VERSION "unknown"
#endif

/* object types for AAC */
#define MAIN       1
#define LC         2
#define SSR        3
#define LTP        4
#define HE_AAC     5
#define ER_LC     17
#define ER_LTP    19
#define LD        23
#define DRM_ER_LC 27 /* special object type for DRM */

/* header types */
#define RAW        0
#define ADIF       1
#define ADTS       2
#define LATM       3

/* SBR signalling */
#define NO_SBR           0
#define SBR_UPSAMPLED    1
#define SBR_DOWNSAMPLED  2
#define NO_SBR_UPSAMPLED 3

/* library output formats */
#define FAAD_FMT_16BIT  1
#define FAAD_FMT_24BIT  2
#define FAAD_FMT_32BIT  3
#define FAAD_FMT_FLOAT  4
#define FAAD_FMT_FIXED  FAAD_FMT_FLOAT
#define FAAD_FMT_DOUBLE 5

/* Capabilities */
#define LC_DEC_CAP           (1<<0) /* Can decode LC */
#define MAIN_DEC_CAP         (1<<1) /* Can decode MAIN */
#define LTP_DEC_CAP          (1<<2) /* Can decode LTP */
#define LD_DEC_CAP           (1<<3) /* Can decode LD */
#define ERROR_RESILIENCE_CAP (1<<4) /* Can decode ER */
#define FIXED_POINT_CAP      (1<<5) /* Fixed point */

/* Channel definitions */
#define FRONT_CHANNEL_CENTER (1)
#define FRONT_CHANNEL_LEFT   (2)
#define FRONT_CHANNEL_RIGHT  (3)
#define SIDE_CHANNEL_LEFT    (4)
#define SIDE_CHANNEL_RIGHT   (5)
#define BACK_CHANNEL_LEFT    (6)
#define BACK_CHANNEL_RIGHT   (7)
#define BACK_CHANNEL_CENTER  (8)
#define LFE_CHANNEL          (9)
#define UNKNOWN_CHANNEL      (0)

/* DRM channel definitions */
#define DRMCH_MONO          1
#define DRMCH_STEREO        2
#define DRMCH_SBR_MONO      3
#define DRMCH_SBR_STEREO    4
#define DRMCH_SBR_PS_STEREO 5


/* FAAD_MIN_STREAMSIZE bytes per decoded channel,
   so at least so much bytes per channel should be available in this stream */
#define FAAD_MIN_STREAMSIZE 768 /* 6144 bits/channel */


typedef void *NeAACDecHandle;

/* ADTS random access ----------------------------------------------------- */

/* Status codes returned by the ADTS index/seek helpers. */
#define NE_AAC_ADTS_OK            0
#define NE_AAC_ADTS_ERR_INVALID   1 /* bad argument or not a valid ADTS stream */
#define NE_AAC_ADTS_ERR_NOTFOUND  2 /* requested position is past the end */
#define NE_AAC_ADTS_ERR_NOMEM     3 /* memory allocation failure */

/*
 * Opaque, immutable frame index of an ADTS byte stream.
 *
 * An index is built once by NeAACDecADTSIndexCreate() and may be shared
 * concurrently between any number of threads.  It only references the
 * caller-supplied bitstream buffer (it never modifies it); the buffer must
 * stay alive and unchanged until NeAACDecADTSIndexDestroy() returns.
 */
typedef struct NeAACDecADTSIndexStruct *NeAACDecADTSIndex;

/*
 * Scan an ADTS byte stream and build its frame index.
 *
 * On success *index receives a newly allocated immutable index.  Leading
 * non-sync data (e.g. ID3 tags) is skipped.  frame_count_out, when non-NULL,
 * receives the number of indexed ADTS frames and samplerate_out the output
 * sample rate in Hz (doubled for implicitly signalled SBR, matching the
 * default decoder configuration).  The data buffer must outlive the index.
 *
 * The reported sample rate and samples-per-frame follow the default
 * decoder convention used by NeAACDecInit()/NeAACDecOpen(): implicit SBR on
 * a core rate at or below 24 kHz is upsampled (2048 samples/frame at twice
 * the core rate).  Decode the stream with that default configuration for
 * the index's sample positions to line up exactly with the PCM output.
 */
NEAACDECAPI unsigned char NeAACDecADTSIndexCreate(
    const unsigned char *data,
    unsigned long size,
    NeAACDecADTSIndex *index,
    unsigned long *frame_count_out,
    unsigned long *samplerate_out);

/* Release an index. Safe to call with NULL. */
NEAACDECAPI void NeAACDecADTSIndexDestroy(NeAACDecADTSIndex index);

/* Number of indexed frames. Returns 0 for a NULL index. */
NEAACDECAPI unsigned long NeAACDecADTSIndexFrameCount(NeAACDecADTSIndex index);

/* Total number of output samples per channel. Returns 0 for a NULL index. */
NEAACDECAPI unsigned long NeAACDecADTSIndexSampleCount(NeAACDecADTSIndex index);

/* Output sample rate in Hz. Returns 0 for a NULL index. */
NEAACDECAPI unsigned long NeAACDecADTSIndexSampleRate(NeAACDecADTSIndex index);

/*
 * Map an output sample position (per channel) to the frame containing it.
 * frame_out receives the frame number and frame_sample_offset_out the
 * output sample position (per channel) at which that frame starts.
 * Returns NE_AAC_ADTS_ERR_NOTFOUND when sample_position is past the end.
 */
NEAACDECAPI unsigned char NeAACDecADTSIndexLocateSample(
    NeAACDecADTSIndex index,
    unsigned long sample_position,
    unsigned long *frame_out,
    unsigned long *frame_sample_offset_out);

/*
 * Map a time in seconds to the frame containing it.
 * frame_out receives the frame number and time_offset_out the time at
 * which that frame starts, in seconds. Negative times are rejected.
 */
NEAACDECAPI unsigned char NeAACDecADTSIndexLocateTime(
    NeAACDecADTSIndex index,
    double time_seconds,
    unsigned long *frame_out,
    double *time_offset_out);

/*
 * Get the byte location of an indexed frame inside the original buffer.
 * offset_out receives the offset of the ADTS syncword, length_out the
 * complete frame length in bytes.
 */
NEAACDECAPI unsigned char NeAACDecADTSIndexGetFrame(
    NeAACDecADTSIndex index,
    unsigned long frame_number,
    unsigned long *offset_out,
    unsigned long *length_out);

/*
 * Rebuild decoder state so that the next NeAACDecDecode() call produces
 * frame target_frame, bit-identically to a continuous from-zero decode.
 *
 * All frames before the target are decoded internally and discarded to
 * reconstruct every stateful part of the decoder (overlap-add, predictors,
 * SBR/PS, ...). target_sample_position is the requested sample position
 * (per channel); it only needs to lie within the target frame. On return
 * target_frame_out (if non-NULL) receives the frame containing it and
 * skipped_samples_out the number of output samples per channel before that
 * frame.
 *
 * The index is treated as read-only and may be shared across threads;
 * each thread must use its own NeAACDecHandle.
 */
NEAACDECAPI unsigned char NeAACDecADTSSeek(
    NeAACDecHandle decoder,
    NeAACDecADTSIndex index,
    unsigned long target_sample_position,
    unsigned long *target_frame_out,
    void *reserved,
    unsigned long *skipped_samples_out);

/*
 * Tear down all per-stream state of a decoder handle so it can be
 * re-initialised and reused (e.g. for another file or another seek).
 * Equivalent to NeAACDecClose() followed by NeAACDecOpen(), but keeps the
 * same handle address and preserves the current configuration.
 * Safe to call with NULL.
 */
NEAACDECAPI void NeAACDecReset(NeAACDecHandle decoder);

typedef struct mp4AudioSpecificConfig
{
    /* Audio Specific Info */
    unsigned char objectTypeIndex;
    unsigned char samplingFrequencyIndex;
    unsigned long samplingFrequency;
    unsigned char channelsConfiguration;

    /* GA Specific Info */
    unsigned char frameLengthFlag;
    unsigned char dependsOnCoreCoder;
    unsigned short coreCoderDelay;
    unsigned char extensionFlag;
    unsigned char aacSectionDataResilienceFlag;
    unsigned char aacScalefactorDataResilienceFlag;
    unsigned char aacSpectralDataResilienceFlag;
    unsigned char epConfig;

    char sbr_present_flag;
    char forceUpSampling;
    char downSampledSBR;
    char ps_present_flag;
} mp4AudioSpecificConfig;

typedef struct NeAACDecConfiguration
{
    unsigned char defObjectType;
    unsigned long defSampleRate;
    unsigned char outputFormat;
    unsigned char downMatrix;
    unsigned char useOldADTSFormat;
    unsigned char dontUpSampleImplicitSBR;
} NeAACDecConfiguration, *NeAACDecConfigurationPtr;

typedef struct NeAACDecFrameInfo
{
    unsigned long bytesconsumed;
    unsigned long samples;
    unsigned char channels;
    unsigned char error;
    unsigned long samplerate;

    /* SBR: 0: off, 1: on; upsample, 2: on; downsampled, 3: off; upsampled */
    unsigned char sbr;

    /* MPEG-4 ObjectType */
    unsigned char object_type;

    /* AAC header type; MP4 will be signalled as RAW also */
    unsigned char header_type;

    /* multichannel configuration */
    unsigned char num_front_channels;
    unsigned char num_side_channels;
    unsigned char num_back_channels;
    unsigned char num_lfe_channels;
    unsigned char channel_position[64];

    /* PS: 0: off, 1: on */
    unsigned char ps;
} NeAACDecFrameInfo;

NEAACDECAPI char* NeAACDecGetErrorMessage(unsigned char errcode);

NEAACDECAPI unsigned long NeAACDecGetCapabilities(void);

NEAACDECAPI NeAACDecHandle NeAACDecOpen(void);

NEAACDECAPI NeAACDecConfigurationPtr NeAACDecGetCurrentConfiguration(NeAACDecHandle hDecoder);

NEAACDECAPI unsigned char NeAACDecSetConfiguration(NeAACDecHandle hDecoder,
                                                   NeAACDecConfigurationPtr config);

/* Init the library based on info from the AAC file (ADTS/ADIF) */
NEAACDECAPI long NeAACDecInit(NeAACDecHandle hDecoder,
                              unsigned char *buffer,
                              unsigned long buffer_size,
                              unsigned long *samplerate,
                              unsigned char *channels);

/* Init the library using a DecoderSpecificInfo */
NEAACDECAPI char NeAACDecInit2(NeAACDecHandle hDecoder,
                               unsigned char *pBuffer,
                               unsigned long SizeOfDecoderSpecificInfo,
                               unsigned long *samplerate,
                               unsigned char *channels);

/* Init the library for DRM */
NEAACDECAPI char NeAACDecInitDRM(NeAACDecHandle *hDecoder, unsigned long samplerate,
                                 unsigned char channels);

NEAACDECAPI void NeAACDecPostSeekReset(NeAACDecHandle hDecoder, long frame);

NEAACDECAPI void NeAACDecClose(NeAACDecHandle hDecoder);

NEAACDECAPI void* NeAACDecDecode(NeAACDecHandle hDecoder,
                                 NeAACDecFrameInfo *hInfo,
                                 unsigned char *buffer,
                                 unsigned long buffer_size);

NEAACDECAPI void* NeAACDecDecode2(NeAACDecHandle hDecoder,
                                  NeAACDecFrameInfo *hInfo,
                                  unsigned char *buffer,
                                  unsigned long buffer_size,
                                  void **sample_buffer,
                                  unsigned long sample_buffer_size);

NEAACDECAPI char NeAACDecAudioSpecificConfig(unsigned char *pBuffer,
                                             unsigned long buffer_size,
                                             mp4AudioSpecificConfig *mp4ASC);

/* Get version and copyright strings */
NEAACDECAPI int NeAACDecGetVersion(char **faad_id_string,
                                   char **faad_copyright_string);

#ifdef _WIN32
  #pragma pack(pop)
#endif

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif
