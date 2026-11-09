#pragma once

// Milliseconds. Pause before the first poll and after each completed cycle.
#ifndef POLL_INTERVAL_MS
#define POLL_INTERVAL_MS 10000UL
#endif

#ifndef TIMEOUT_MS
#define TIMEOUT_MS 1000UL
#endif

#ifndef FRAME_GAP_MS
#define FRAME_GAP_MS 20UL
#endif

#ifndef PULSE_MS
#define PULSE_MS 2UL
#endif

// Both sensor and radio records are fixed at six bytes.
#ifndef RECORD_SIZE
#define RECORD_SIZE 6
#endif

#ifndef CHANNEL_CAPACITY
#define CHANNEL_CAPACITY 32
#endif

#ifndef DRAIN_LIMIT_MS
#define DRAIN_LIMIT_MS 1000UL
#endif

#if POLL_INTERVAL_MS < 1 || POLL_INTERVAL_MS > 0x7fffffffUL
#error "POLL_INTERVAL_MS must be 1..2147483647"
#endif

#if TIMEOUT_MS < 1 || TIMEOUT_MS > 10666
#error "TIMEOUT_MS must be 1..10666"
#endif

#if FRAME_GAP_MS < 1 || FRAME_GAP_MS >= TIMEOUT_MS
#error "FRAME_GAP_MS must be 1..TIMEOUT_MS-1"
#endif

#if PULSE_MS < 1 || PULSE_MS > 32000
#error "PULSE_MS must be 1..32000"
#endif

#if RECORD_SIZE != 6
#error "RECORD_SIZE must be 6 (binary protocol)"
#endif

#if CHANNEL_CAPACITY != 32
#error "CHANNEL_CAPACITY must be 32 (192 bytes per channel)"
#endif

#if DRAIN_LIMIT_MS < FRAME_GAP_MS || DRAIN_LIMIT_MS > 32000
#error "DRAIN_LIMIT_MS must be FRAME_GAP_MS..32000"
#endif
