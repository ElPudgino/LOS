#ifndef STATISTICS_H
#define STATISTICS_H

#include <stdint.h>

#define WINDOW_BLOCKS 128u

/* Sort a completed, non-overlapping window in place. The lower median
 * gives the same sum of absolute distances as the midpoint of the two
 * central values, including when the mathematical median is fractional. */
static uint64_t deviation_total(uint32_t values[WINDOW_BLOCKS])
{
    for (uint32_t i = 1; i < WINDOW_BLOCKS; ++i) {
        uint32_t value = values[i];
        uint32_t j = i;

        while (j && values[j - 1] > value) {
            values[j] = values[j - 1];
            --j;
        }

        values[j] = value;
    }

    uint32_t median = values[WINDOW_BLOCKS / 2 - 1];
    uint64_t total = 0;
    for (uint32_t i = 0; i < WINDOW_BLOCKS; ++i)
        total += values[i] >= median ? values[i] - median : median - values[i];

    return total;
}

/* Equivalent to (total * 1000 + 64) >> 7: nearest, ties upward.
 * Split before scaling to avoid a 64-bit multiplication helper.
 * SAMPLES <= 131 bounds total by 64 * 131 * 65535; both the scaled
 * whole part and the rounded result fit in uint32_t. */
static uint32_t deviation_milli(uint64_t total)
{
    uint32_t whole = (uint32_t)(total >> 7) * 1000u;
    uint32_t fraction = (((uint32_t)total & 127u) * 1000u + 64u) >> 7;

    return whole + fraction;
}

#endif
