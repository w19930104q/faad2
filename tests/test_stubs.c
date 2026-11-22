/* Link stubs for the standalone downmix unit test. The only external
 * symbol pulled in transitively through structs.h -> bits.h is the
 * non-inline bitfile flush routine, which the downmix code never calls. */
#include "common.h"
#include "bits.h"

void faad_flushbits_ex(bitfile *ld, uint32_t bits)
{
    (void)ld;
    (void)bits;
}
