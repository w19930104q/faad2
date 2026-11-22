/*
 * Hardcoded-assertion tests for the frontend WAV/RAW writer.
 *
 * Covers multi-channel (3-8 ch, including PCE-defined order) channel mask
 * derivation, WAVEFORMATEXTENSIBLE header bytes, and the physical channel
 * reordering for every sample format. Builds against the real audio.c.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "neaacdec.h"
#include "audio.h"

/* neaacdec.h position constants */
#define FC FRONT_CHANNEL_CENTER
#define FL FRONT_CHANNEL_LEFT
#define FR FRONT_CHANNEL_RIGHT
#define BL BACK_CHANNEL_LEFT
#define BR BACK_CHANNEL_RIGHT
#define BC BACK_CHANNEL_CENTER
#define SL SIDE_CHANNEL_LEFT
#define SR SIDE_CHANNEL_RIGHT
#define LFE LFE_CHANNEL
#define UNK UNKNOWN_CHANNEL

#define MS_FL   0x001
#define MS_FR   0x002
#define MS_FC   0x004
#define MS_LFE  0x008
#define MS_BL   0x010
#define MS_BR   0x020
#define MS_BC   0x100
#define MS_SL   0x200
#define MS_SR   0x400

static int failures;

#define CHECK(cond) do { \
    if (!(cond)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); failures++; } \
} while (0)

static uint16_t rd16(const unsigned char *p)
{ return (uint16_t)(p[0] | (p[1] << 8)); }

static uint32_t rd32(const unsigned char *p)
{ return (uint32_t)(p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24)); }

static unsigned char *read_file(const char *path, size_t *out_size)
{
    FILE *f = fopen(path, "rb");
    unsigned char *b;
    long n;
    CHECK(f != NULL);
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fseek(f, 0, SEEK_SET);
    b = malloc((size_t)n);
    *out_size = fread(b, 1, (size_t)n, f);
    fclose(f);
    return b;
}

/* Common checks for the 68-byte extensible header. */
static void check_ext_header(const unsigned char *b, size_t size,
                             int nch, int bits, uint32_t expect_mask,
                             int float_mode, uint32_t data_bytes)
{
    static const unsigned char pcm_guid[16] =
        {1,0,0,0, 0,0, 0x10,0, 0x80,0, 0,0xaa,0,0x38,0x9b,0x71};
    static const unsigned char float_guid[16] =
        {3,0,0,0, 0,0, 0x10,0, 0x80,0, 0,0xaa,0,0x38,0x9b,0x71};
    int bytes = bits / 8;

    CHECK(size == 68u + data_bytes);
    CHECK(memcmp(b, "RIFF", 4) == 0);
    CHECK(rd32(b + 4) == (uint32_t)(size - 8));
    CHECK(memcmp(b + 8, "WAVEfmt ", 8) == 0);
    CHECK(rd32(b + 16) == 40);                 /* fmt chunk size */
    CHECK(rd16(b + 20) == 0xFFFE);             /* WAVE_FORMAT_EXTENSIBLE */
    CHECK(rd16(b + 22) == nch);
    CHECK(rd32(b + 24) == 48000);
    CHECK(rd32(b + 28) == 48000u * bytes * nch);  /* nAvgBytesPerSec */
    CHECK(rd16(b + 32) == bytes * nch);        /* nBlockAlign */
    CHECK(rd16(b + 34) == bits);               /* wBitsPerSample */
    CHECK(rd16(b + 36) == 22);                 /* cbSize */
    CHECK(rd16(b + 38) == bits);               /* wValidBitsPerSample */
    CHECK(rd32(b + 40) == expect_mask);        /* dwChannelMask */
    CHECK(memcmp(b + 44, float_mode ? float_guid : pcm_guid, 16) == 0);
    CHECK(memcmp(b + 60, "data", 4) == 0);
    CHECK(rd32(b + 64) == data_bytes);
}

static void check_plain_header(const unsigned char *b, size_t size,
                               int nch, int bits, uint32_t data_bytes)
{
    int bytes = bits / 8;
    CHECK(size == 44u + data_bytes);
    CHECK(memcmp(b, "RIFF", 4) == 0);
    CHECK(rd32(b + 4) == (uint32_t)(size - 8));
    CHECK(memcmp(b + 8, "WAVEfmt ", 8) == 0);
    CHECK(rd32(b + 16) == 16);
    CHECK(rd16(b + 20) == 0x0001);             /* PCM */
    CHECK(rd16(b + 22) == nch);
    CHECK(rd32(b + 24) == 48000);
    CHECK(rd32(b + 28) == 48000u * bytes * nch);
    CHECK(rd16(b + 32) == bytes * nch);
    CHECK(rd16(b + 34) == bits);
    CHECK(memcmp(b + 36, "data", 4) == 0);
    CHECK(rd32(b + 40) == data_bytes);
}

int main(void)
{
    const char *tmp = "/tmp/faad_audio_test.wav";

    /* 1. 5.1: AAC order C,L,R,BL,BR,LFE -> WAVE mask 0x3F, order L,R,C,LFE,BL,BR */
    {
        unsigned char pos[6] = {FC, FL, FR, BL, BR, LFE};
        audio_file *af = open_audio_file((char *)tmp, 48000, 6,
                                         FAAD_FMT_16BIT, OUTPUT_WAV, pos);
        int16_t frame[6];
        int c;
        CHECK(af != NULL);
        CHECK(af->channelMask == (long)(MS_FL|MS_FR|MS_FC|MS_LFE|MS_BL|MS_BR));
        for (c = 0; c < 6; c++) frame[c] = (int16_t)(c + 1);
        /* fwrite returns item count, item size = bits/8 = 2 for 16-bit */
        CHECK(write_audio_file(af, frame, 6) == 2);
        close_audio_file(af);

        size_t size;
        unsigned char *b = read_file(tmp, &size);
        check_ext_header(b, size, 6, 16, 0x3F, 0, 12);
        /* source values: C=1,L=2,R=3,BL=4,BR=5,LFE=6
           WAVE order:  L,R,C,LFE,BL,BR -> 2,3,1,6,4,5 */
        static const int16_t expect[6] = {2, 3, 1, 6, 4, 5};
        for (c = 0; c < 6; c++)
            CHECK((int16_t)rd16(b + 68 + c * 2) == expect[c]);
        free(b);
    }

    /* 2. 7.1: AAC order C,L,R,SL,SR,BL,BR,LFE -> mask 0x63F */
    {
        unsigned char pos[8] = {FC, FL, FR, SL, SR, BL, BR, LFE};
        audio_file *af = open_audio_file((char *)tmp, 48000, 8,
                                         FAAD_FMT_16BIT, OUTPUT_WAV, pos);
        int16_t frame[8];
        int c;
        CHECK(af != NULL);
        CHECK(af->channelMask ==
              (long)(MS_FL|MS_FR|MS_FC|MS_LFE|MS_BL|MS_BR|MS_SL|MS_SR));
        for (c = 0; c < 8; c++) frame[c] = (int16_t)(c + 1);
        CHECK(write_audio_file(af, frame, 8) == 2);
        close_audio_file(af);

        size_t size;
        unsigned char *b = read_file(tmp, &size);
        check_ext_header(b, size, 8, 16, 0x63F, 0, 16);
        /* src: C=1,L=2,R=3,SL=4,SR=5,BL=6,BR=7,LFE=8
           WAVE: L,R,C,LFE,BL,BR,SL,SR -> 2,3,1,8,6,7,4,5 */
        static const int16_t expect[8] = {2, 3, 1, 8, 6, 7, 4, 5};
        for (c = 0; c < 8; c++)
            CHECK((int16_t)rd16(b + 68 + c * 2) == expect[c]);
        free(b);
    }

    /* 3. PCE-defined 4ch order C,L,R,BC (ISO config-4 style): mask 0x107 */
    {
        unsigned char pos[4] = {FC, FL, FR, BC};
        audio_file *af = open_audio_file((char *)tmp, 48000, 4,
                                         FAAD_FMT_16BIT, OUTPUT_WAV, pos);
        CHECK(af != NULL);
        CHECK(af->channelMask == (long)(MS_FL|MS_FR|MS_FC|MS_BC));
        close_audio_file(af);
        size_t size;
        unsigned char *b = read_file(tmp, &size);
        check_ext_header(b, size, 4, 16, 0x107, 0, 0);
        free(b);
    }

    /* 4. PCE-defined 3ch (front L/R + back center), arbitrary bitstream order */
    {
        unsigned char pos[3] = {BC, FL, FR};
        audio_file *af = open_audio_file((char *)tmp, 48000, 3,
                                         FAAD_FMT_16BIT, OUTPUT_WAV, pos);
        int16_t frame[3] = {10, 20, 30}; /* BC, FL, FR */
        CHECK(af != NULL);
        CHECK(af->channelMask == (long)(MS_FL|MS_FR|MS_BC));
        CHECK(write_audio_file(af, frame, 3) == 2);
        close_audio_file(af);
        size_t size;
        unsigned char *b = read_file(tmp, &size);
        check_ext_header(b, size, 3, 16, MS_FL|MS_FR|MS_BC, 0, 6);
        /* WAVE order FL,FR,BC -> 20,30,10 */
        CHECK(rd16(b + 68) == 20);
        CHECK(rd16(b + 70) == 30);
        CHECK(rd16(b + 72) == 10);
        free(b);
    }

    /* 5. Unknown channel -> no mask, plain header, identity order */
    {
        unsigned char pos[3] = {FL, FR, UNK};
        audio_file *af = open_audio_file((char *)tmp, 48000, 3,
                                         FAAD_FMT_16BIT, OUTPUT_WAV, pos);
        int16_t frame[3] = {7, 8, 9};
        CHECK(af != NULL);
        CHECK(af->channelMask == 0);
        CHECK(write_audio_file(af, frame, 3) == 2);
        close_audio_file(af);
        size_t size;
        unsigned char *b = read_file(tmp, &size);
        check_plain_header(b, size, 3, 16, 6);
        CHECK(rd16(b + 44) == 7);
        CHECK(rd16(b + 46) == 8);
        CHECK(rd16(b + 48) == 9);
        free(b);
    }

    /* 6. Mono / stereo: plain header regardless of positions */
    {
        unsigned char mono[1] = {FC};
        unsigned char stereo[2] = {FL, FR};
        CHECK(faad_wave_channel_mask(mono, 1) == 0);
        CHECK(faad_wave_channel_mask(stereo, 2) == 0);
    }

    /* 7. 24/32-bit and float reorder for 5.1 */
    {
        unsigned char pos[6] = {FC, FL, FR, BL, BR, LFE};
        int fmt;
        for (fmt = FAAD_FMT_24BIT; fmt <= FAAD_FMT_FLOAT; fmt++)
        {
            int bytes = (fmt == FAAD_FMT_24BIT) ? 3 : 4;
            audio_file *af = open_audio_file((char *)tmp, 48000, 6, fmt,
                                             OUTPUT_WAV, pos);
            int32_t frame32[6];
            float framef[6];
            void *frame;
            int c;
            CHECK(af != NULL);
            if (!af) continue;
            CHECK(af->channelMask == 0x3F);
            /* distinct value per source channel, survives 24-bit trim */
            for (c = 0; c < 6; c++)
            {
                frame32[c] = c + 1;
                framef[c] = (float)(c + 1);
            }
            frame = (fmt == FAAD_FMT_FLOAT) ? (void *)framef : (void *)frame32;
            CHECK(write_audio_file(af, frame, 6) == (size_t)bytes);
            close_audio_file(af);

            size_t size;
            unsigned char *b = read_file(tmp, &size);
            check_ext_header(b, size, 6, bytes * 8, 0x3F,
                             fmt == FAAD_FMT_FLOAT, (uint32_t)(6 * bytes));
            /* WAVE order L,R,C,LFE,BL,BR; source channels held values 1..6 */
            static const int expect_src[6] = {2, 3, 1, 6, 4, 5};
            if (fmt == FAAD_FMT_FLOAT)
            {
                for (c = 0; c < 6; c++)
                {
                    uint32_t bits32 = rd32(b + 68 + c * 4);
                    float fv;
                    memcpy(&fv, &bits32, 4);
                    CHECK(fv == (float)expect_src[c]);
                }
            }
            else
            {
                for (c = 0; c < 6; c++)
                    CHECK(b[68 + c * bytes] == (unsigned char)(expect_src[c]));
            }
            free(b);
        }
    }

    /* 8. RAW output: no header, bytes untouched, no mask/order bookkeeping */
    {
        unsigned char pos[6] = {FC, FL, FR, BL, BR, LFE};
        const char *raw = "/tmp/faad_audio_test.pcm";
        audio_file *af = open_audio_file((char *)raw, 48000, 6,
                                         FAAD_FMT_16BIT, OUTPUT_RAW, pos);
        int16_t frame[6] = {1, 2, 3, 4, 5, 6};
        CHECK(af != NULL);
        if (af)
        {
            CHECK(af->channelMask == 0);
            CHECK(write_audio_file(af, frame, 6) == 2);
            close_audio_file(af);
        }
        size_t size;
        unsigned char *b = read_file(raw, &size);
        CHECK(size == 12);
        CHECK(memcmp(b, "RIFF", 4) != 0);
        {
            int c;
            for (c = 0; c < 6; c++)
                CHECK((int16_t)rd16(b + c * 2) == c + 1); /* identity */
        }
        free(b);
    }

    /* 9. duplicate speaker positions -> cannot build a mask */
    {
        unsigned char pos[3] = {FL, FL, FR};
        CHECK(faad_wave_channel_mask(pos, 3) == 0);
    }

    if (failures)
    {
        fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    printf("all audio writer checks passed\n");
    return 0;
}
