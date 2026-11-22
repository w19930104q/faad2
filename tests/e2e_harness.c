/* E2E harness: decode an ADTS AAC file twice with the same library.
 *
 * Pass 1 (no downmix): dumps the decoded multichannel float PCM and the
 *   per-frame channel positions, so an external reference can rebuild
 *   the matrix from the actual decoded channel roles.
 * Pass 2 (downmix): dumps the stereo float output for the requested
 *   profile / peak policy.
 *
 * Output files (raw little-endian float32):
 *   <prefix>_multi.pcm   interleaved multichannel PCM
 *   <prefix>_pos.u8      channel_position[ch] per frame, channels bytes
 *   <prefix>_meta.txt    first line "frames channels layout_nch"
 *   <prefix>_dmx.pcm     interleaved stereo PCM
 *
 * Usage: e2e_harness in.aac profile peak prefix
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <neaacdec.h>

static int decode_pass(const char *aac, int do_dmx, int profile, int peak,
                       FILE *pcm, FILE *pos, unsigned long *frames,
                       unsigned char *out_ch)
{
    FILE *fh = fopen(aac, "rb");
    if (!fh) return 2;
    unsigned char *buf = (unsigned char *)malloc(65536);
    size_t cap = 65536, n = fread(buf, 1, cap, fh);

    NeAACDecHandle dec = NeAACDecOpen();
    NeAACDecConfigurationPtr cfg = NeAACDecGetCurrentConfiguration(dec);
    cfg->outputFormat = FAAD_FMT_FLOAT;
    cfg->downMatrix = do_dmx ? 1 : 0;
    cfg->downMixProfile = (unsigned char)profile;
    cfg->downMixPeak = (unsigned char)peak;
    if (!NeAACDecSetConfiguration(dec, cfg)) { fprintf(stderr, "bad config\n"); return 3; }

    unsigned long sr; unsigned char ch;
    long used = NeAACDecInit(dec, buf, (unsigned long)n, &sr, &ch);
    size_t off = (size_t)used;

    NeAACDecFrameInfo info;
    *frames = 0;
    for (;;) {
        if (n - off < 8192) {
            memmove(buf, buf + off, n - off);
            n -= off;
            size_t got = fread(buf + n, 1, cap - n, fh);
            n += got;
            off = 0;
            if (n == 0) break;
        }
        void *out = NeAACDecDecode(dec, &info, buf + off, (unsigned long)(n - off));
        off += info.bytesconsumed;
        if (info.error > 0) break;
        if (info.samples == 0) continue;

        unsigned per = info.samples / info.channels;
        if (pcm) fwrite(out, sizeof(float), info.samples, pcm);
        if (pos) fwrite(info.channel_position, 1, info.channels, pos);
        *out_ch = info.channels;
        (*frames)++;
        (void)per;
    }
    free(buf);
    NeAACDecClose(dec);
    fclose(fh);
    return 0;
}

int main(int argc, char **argv)
{
    if (argc < 5) { fprintf(stderr, "usage: %s in.aac profile peak prefix\n", argv[0]); return 2; }
    const char *aac = argv[1];
    int profile = atoi(argv[2]), peak = atoi(argv[3]);
    const char *prefix = argv[4];

    char path[512];
    snprintf(path, sizeof(path), "%s_multi.pcm", prefix);
    FILE *fm = fopen(path, "wb");
    snprintf(path, sizeof(path), "%s_pos.u8", prefix);
    FILE *fp = fopen(path, "wb");
    unsigned long frames_m = 0; unsigned char ch_m = 0;
    int rc = decode_pass(aac, 0, profile, peak, fm, fp, &frames_m, &ch_m);
    fclose(fm); fclose(fp);
    if (rc) return rc;

    snprintf(path, sizeof(path), "%s_dmx.pcm", prefix);
    FILE *fd = fopen(path, "wb");
    unsigned long frames_d = 0; unsigned char ch_d = 0;
    rc = decode_pass(aac, 1, profile, peak, fd, NULL, &frames_d, &ch_d);
    fclose(fd);
    if (rc) return rc;

    snprintf(path, sizeof(path), "%s_meta.txt", prefix);
    FILE *fx = fopen(path, "w");
    fprintf(fx, "%lu %u %u %lu\n", frames_m, ch_m, ch_d, frames_d);
    fclose(fx);
    return 0;
}
