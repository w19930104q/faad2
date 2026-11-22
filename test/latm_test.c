/*
** Test harness for the FAAD2 LATM library interface.
**
** Modes:
**   latm_test decode-legacy <in.loas> <out.pcm>
**       Decode through the existing entry points (NeAACDecInit +
**       NeAACDecDecode), the same path the faad frontend uses.
**   latm_test decode-latm <in.loas> <out.pcm> [chunksize]
**       Decode through the LATM entry points (NeAACDecInitLATM +
**       NeAACDecLatmFeed + NeAACDecLatmDecode), feeding the stream in
**       chunksize-byte slices (default: whole file at once).
**   latm_test expect-init-fail <in.loas>
**       NeAACDecInitLATM must fail deterministically (same result twice).
**   latm_test expect-decode-error <in.loas>
**       Streaming decode must report a deterministic decode error.
**
** All PCM output is 16 bit interleaved, matching the frontend defaults.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "neaacdec.h"

static unsigned char *read_file(const char *path, unsigned long *size)
{
    FILE *f = fopen(path, "rb");
    unsigned char *buf;
    long len;

    if (!f)
        return NULL;
    fseek(f, 0, SEEK_END);
    len = ftell(f);
    fseek(f, 0, SEEK_SET);
    buf = (unsigned char *)malloc(len > 0 ? len : 1);
    if (!buf) {
        fclose(f);
        return NULL;
    }
    if (len > 0 && fread(buf, 1, len, f) != (size_t)len) {
        free(buf);
        fclose(f);
        return NULL;
    }
    fclose(f);
    *size = (unsigned long)len;
    return buf;
}

static int write_pcm(FILE *out, void *sample_buffer, NeAACDecFrameInfo *fi)
{
    if (sample_buffer && fi->samples > 0) {
        if (fwrite(sample_buffer, sizeof(short), fi->samples, out) != fi->samples)
            return -1;
    }
    return 0;
}

/* Decode using the existing NeAACDecInit/NeAACDecDecode entry points */
static int decode_legacy(const char *in_path, const char *out_path)
{
    unsigned long size, pos;
    unsigned char *data = read_file(in_path, &size);
    unsigned long samplerate = 0;
    unsigned char channels = 0;
    NeAACDecHandle h;
    NeAACDecFrameInfo fi;
    long consumed;
    FILE *out;
    void *s;
    int ret = 1;

    if (!data) {
        fprintf(stderr, "cannot read %s\n", in_path);
        return 1;
    }
    out = fopen(out_path, "wb");
    if (!out) {
        free(data);
        return 1;
    }

    h = NeAACDecOpen();
    consumed = NeAACDecInit(h, data, size, &samplerate, &channels);
    if (consumed < 0) {
        fprintf(stderr, "NeAACDecInit failed\n");
        goto done;
    }
    fprintf(stderr, "init: samplerate=%lu channels=%d\n", samplerate, channels);
    /* same as the frontend: keep the first frame */
    NeAACDecPostSeekReset(h, 1);

    pos = (unsigned long)consumed;
    while (pos < size) {
        s = NeAACDecDecode(h, &fi, data + pos, size - pos);
        pos += fi.bytesconsumed;
        if (fi.error) {
            fprintf(stderr, "decode error: %s\n", NeAACDecGetErrorMessage(fi.error));
            goto done;
        }
        if (write_pcm(out, s, &fi) != 0)
            goto done;
        if (fi.bytesconsumed == 0)
            break; /* no complete frame left */
    }
    ret = 0;

done:
    NeAACDecClose(h);
    fclose(out);
    free(data);
    return ret;
}

/* Decode using the LATM streaming entry points, feeding chunk_size bytes
   at a time */
static int decode_latm(const char *in_path, const char *out_path, unsigned long chunk_size)
{
    unsigned long size, pos;
    unsigned char *data = read_file(in_path, &size);
    unsigned long samplerate = 0;
    unsigned char channels = 0;
    NeAACDecHandle h;
    NeAACDecFrameInfo fi;
    FILE *out;
    void *s;
    int ret = 1;

    if (!data) {
        fprintf(stderr, "cannot read %s\n", in_path);
        return 1;
    }
    out = fopen(out_path, "wb");
    if (!out) {
        free(data);
        return 1;
    }

    h = NeAACDecOpen();
    if (NeAACDecInitLATM(h, data, size, &samplerate, &channels) < 0) {
        fprintf(stderr, "NeAACDecInitLATM failed\n");
        goto done;
    }
    fprintf(stderr, "init: samplerate=%lu channels=%d\n", samplerate, channels);
    NeAACDecPostSeekReset(h, 1);

    pos = 0;
    for (;;) {
        if (pos < size) {
            unsigned long n = size - pos;
            if (n > chunk_size)
                n = chunk_size;
            if (NeAACDecLatmFeed(h, data + pos, n) != 0) {
                fprintf(stderr, "NeAACDecLatmFeed failed\n");
                goto done;
            }
            pos += n;
        }
        /* drain all complete frames */
        for (;;) {
            s = NeAACDecLatmDecode(h, &fi);
            if (fi.error) {
                fprintf(stderr, "decode error: %s\n", NeAACDecGetErrorMessage(fi.error));
                goto done;
            }
            if (s == NULL)
                break; /* need more data */
            if (write_pcm(out, s, &fi) != 0)
                goto done;
        }
        if (pos >= size)
            break;
    }
    ret = 0;

done:
    NeAACDecClose(h);
    fclose(out);
    free(data);
    return ret;
}

/* Init must fail, deterministically */
static int expect_init_fail(const char *in_path)
{
    unsigned long size;
    unsigned char *data = read_file(in_path, &size);
    unsigned long samplerate = 0;
    unsigned char channels = 0;
    long r1, r2;
    NeAACDecHandle h;

    if (!data)
        return 1;

    h = NeAACDecOpen();
    r1 = NeAACDecInitLATM(h, data, size, &samplerate, &channels);
    NeAACDecClose(h);

    h = NeAACDecOpen();
    r2 = NeAACDecInitLATM(h, data, size, &samplerate, &channels);
    NeAACDecClose(h);
    free(data);

    if (r1 >= 0 || r2 >= 0 || r1 != r2) {
        fprintf(stderr, "expected deterministic init failure, got %ld/%ld\n", r1, r2);
        return 1;
    }
    fprintf(stderr, "init failed deterministically: %ld\n", r1);
    return 0;
}

/* Streaming decode must hit a deterministic decode error */
static int expect_decode_error(const char *in_path)
{
    unsigned long size;
    unsigned char *data = read_file(in_path, &size);
    int run, ret = 0;
    int err_seen[2] = {0, 0};

    if (!data)
        return 1;

    for (run = 0; run < 2; run++) {
        unsigned long samplerate = 0;
        unsigned char channels = 0;
        unsigned long pos = 0;
        NeAACDecHandle h = NeAACDecOpen();
        NeAACDecFrameInfo fi;

        if (NeAACDecInitLATM(h, data, size, &samplerate, &channels) < 0) {
            fprintf(stderr, "run %d: unexpected init failure\n", run);
            NeAACDecClose(h);
            free(data);
            return 1;
        }
        while (pos < size) {
            unsigned long n = size - pos;
            void *s;
            if (n > 13)
                n = 13; /* arbitrary small slices */
            if (NeAACDecLatmFeed(h, data + pos, n) != 0) {
                fprintf(stderr, "run %d: feed failed\n", run);
                NeAACDecClose(h);
                free(data);
                return 1;
            }
            pos += n;
            for (;;) {
                s = NeAACDecLatmDecode(h, &fi);
                if (fi.error) {
                    err_seen[run] = fi.error;
                    break;
                }
                if (s == NULL)
                    break;
            }
            if (err_seen[run])
                break;
        }
        NeAACDecClose(h);
        if (!err_seen[run]) {
            fprintf(stderr, "run %d: expected a decode error, saw none\n", run);
            ret = 1;
        }
    }

    if (err_seen[0] != err_seen[1]) {
        fprintf(stderr, "non-deterministic errors: %d/%d\n", err_seen[0], err_seen[1]);
        ret = 1;
    } else if (!ret) {
        fprintf(stderr, "decode failed deterministically: %s\n",
                NeAACDecGetErrorMessage((unsigned char)err_seen[0]));
    }

    free(data);
    return ret;
}

int main(int argc, char **argv)
{
    if (argc >= 4 && strcmp(argv[1], "decode-legacy") == 0)
        return decode_legacy(argv[2], argv[3]);
    if (argc >= 4 && strcmp(argv[1], "decode-latm") == 0) {
        unsigned long chunk = 1 << 30;
        if (argc >= 5)
            chunk = strtoul(argv[4], NULL, 0);
        return decode_latm(argv[2], argv[3], chunk);
    }
    if (argc >= 3 && strcmp(argv[1], "expect-init-fail") == 0)
        return expect_init_fail(argv[2]);
    if (argc >= 3 && strcmp(argv[1], "expect-decode-error") == 0)
        return expect_decode_error(argv[2]);

    fprintf(stderr, "usage: %s decode-legacy|decode-latm|expect-init-fail|expect-decode-error ...\n", argv[0]);
    return 2;
}
