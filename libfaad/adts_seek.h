/*
** FAAD2 - internal declarations for ADTS sample-accurate random access.
** The public API lives in neaacdec.h; this header is only used inside the
** library and by the faad command line frontend.
*/
#ifndef __ADTS_SEEK_H__
#define __ADTS_SEEK_H__

#include <neaacdec.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Same as NeAACDecADTSSeek() but uses the faad CLI frame-counter
 * convention: the first decoded frame counts as frame 1 and its output is
 * preserved, matching NeAACDecPostSeekReset(handle, 1) used by the normal
 * command line decode path.  NeAACDecADTSSeek() itself suppresses the first
 * frame (frame 0 convention), which is the standard library behaviour.
 */
unsigned char NeAACDecADTSSeekCLI(NeAACDecHandle decoder,
                                  NeAACDecADTSIndex index,
                                  unsigned long target_sample_position,
                                  unsigned long *target_frame_out,
                                  unsigned long *skipped_samples_out);

#ifdef __cplusplus
}
#endif

#endif /* __ADTS_SEEK_H__ */
