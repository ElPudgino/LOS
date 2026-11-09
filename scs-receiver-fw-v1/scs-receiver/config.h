#pragma once

#include <stdint.h>

#ifndef BURST_TIMEOUT_MS
#define BURST_TIMEOUT_MS 1000UL
#endif

#ifndef BURST_BUFFER_SIZE
// 3 sensors * (32 measurements + 1 timeout) * 6 binary bytes.
#define BURST_BUFFER_SIZE 594
#endif

static_assert(BURST_TIMEOUT_MS > 0 && BURST_TIMEOUT_MS < 0x80000000UL,
              "Burst timeout must be positive and below 2^31 ms");

static_assert(BURST_BUFFER_SIZE > 0 && BURST_BUFFER_SIZE <= 65535UL,
              "Burst buffer size must fit uint16_t");

static_assert(BURST_BUFFER_SIZE % 6 == 0,
              "Burst buffer size must be a multiple of 6");
