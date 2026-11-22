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


/* A decode call can eat up to FAAD_MIN_STREAMSIZE bytes per decoded channel,
   so at least so much bytes per channel should be available in this stream */
#define FAAD_MIN_STREAMSIZE 768 /* 6144 bits/channel */

/* DRC (dynamic range control) compression levels for NeAACDecSetDRCLevel().
 *
 * FAAD_DRC_OFF:   DRC is not applied at all. The output is bit-exact
 *                 identical to a decode in which every DRC gain is zero
 *                 (unity gain), i.e. no compression and no program
 *                 reference level normalisation.
 * FAAD_DRC_LIGHT: half compression. The dyn_rng_ctl compression/boost
 *                 term is scaled by 1/2; the program reference level
 *                 normalisation term is applied in full.
 * FAAD_DRC_HEAVY: full compression (default). This is the historical
 *                 behaviour of the library.
 *
 * The per-band gain factor that a level applies to the spectral data is
 *   compress (dyn_rng_sgn == 1):
 *     gain = 2 ^ ((-ctrl * dyn_rng_ctl - (DRC_REF_LEVEL - prog_ref_level)) / 24)
 *   boost (dyn_rng_sgn == 0):
 *     gain = 2 ^ (( ctrl * dyn_rng_ctl - (DRC_REF_LEVEL - prog_ref_level)) / 24)
 * with ctrl = 1.0 for FAAD_DRC_HEAVY, ctrl = 0.5 for FAAD_DRC_LIGHT and
 * DRC_REF_LEVEL = 80 (i.e. -20 dB in 0.25 dB steps). For FAAD_DRC_OFF the
 * gain is exactly 1.0 for every band.
 */
#define FAAD_DRC_OFF   0
#define FAAD_DRC_LIGHT 1
#define FAAD_DRC_HEAVY 2

/* Per-frame DRC information, see NeAACDecGetDRCFrameInfo() */
typedef struct NeAACDecDRCFrameInfo
{
    /* DRC metadata was signalled in the bitstream */
    unsigned char present;
    /* compression level that is in effect (FAAD_DRC_*) */
    unsigned char level;
    /* number of DRC bands signalled in the bitstream (0 when !present) */
    unsigned char num_bands;
    /* linear gain factor effectively applied per band during the last
       decoded frame; 1.0 for bands/channels to which no gain was applied
       (e.g. level == FAAD_DRC_OFF or excluded channels) */
    double gain[17];
} NeAACDecDRCFrameInfo;


typedef void *NeAACDecHandle;

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

/* Set the DRC compression level (one of FAAD_DRC_*). Takes effect on the
   next decoded frame, so it can be changed at runtime between NeAACDecDecode
   calls. Returns 1 on success, 0 on invalid arguments. The default level is
   FAAD_DRC_HEAVY. Requires the library to be built with DRC application
   enabled (CMake option FAAD_APPLY_DRC, ON by default); otherwise no DRC
   gain is ever applied regardless of the level set here. */
NEAACDECAPI unsigned char NeAACDecSetDRCLevel(NeAACDecHandle hDecoder,
                                              unsigned char level);

/* Get the current DRC compression level (one of FAAD_DRC_*). */
NEAACDECAPI unsigned char NeAACDecGetDRCLevel(NeAACDecHandle hDecoder);

/* Get the DRC gains that were effectively applied while decoding the most
   recent frame. Returns 1 on success, 0 on invalid arguments. */
NEAACDECAPI unsigned char NeAACDecGetDRCFrameInfo(NeAACDecHandle hDecoder,
                                                  NeAACDecDRCFrameInfo *info);

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
