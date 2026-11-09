#!/usr/bin/env python3
"""Host checks for binary records and requests, not electrical EXTI timing."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
HARNESS = r'''
#include <assert.h>
#include <stdint.h>
#include <string.h>
#include "registers.h"

static uint32_t pending, exticr, intenr, evenr, rtenr, ftenr;
static uint16_t data;
static uint8_t output[4096];
static unsigned output_used, data_written, status_reads;
static unsigned expected_bytes, tc_reads;
static uint16_t status;
static void assert_batch_retained(void);

static uint32_t *fake32(uint32_t address)
{
    switch (address) {
    case 0x40010008u: return &exticr;
    case 0x40010400u: return &intenr;
    case 0x40010404u: return &evenr;
    case 0x40010408u: return &rtenr;
    case 0x4001040cu: return &ftenr;
    case 0x40010414u: return &pending;
    default: assert(0); return 0;
    }
}
static uint16_t *fake16(uint32_t address)
{
    if (address == 0x40013800u) {
        ++status_reads;
        if (data_written) {
            assert(output_used < sizeof output);
            assert(data <= UINT8_MAX);
            output[output_used++] = (uint8_t)data;
            data_written = 0;
            /* A second request during TX must not survive the batch. */
            pending = REQUEST_EVENT;
        }
        assert(output_used <= expected_bytes);
        assert_batch_retained();
        status = USART_TXE;
        if (output_used == expected_bytes && ++tc_reads >= 3)
            status |= USART_TC;
        return &status;
    }
    assert(address == 0x40013804u);
    assert(!data_written);
    data_written = 1;
    return &data;
}
#undef REG32
#undef REG16
#define REG32(a) (*(volatile uint32_t *)fake32(a))
#define REG16(a) (*(volatile uint16_t *)fake16(a))
#define BLOCK_STANDBY 0
#define main firmware_main
#include "main.c"
#undef main

static uint32_t saved_count, saved_head;
static uint32_t saved_values[BUFFER_CAPACITY];
static uint8_t saved_sequences[BUFFER_CAPACITY];

static void assert_batch_retained(void)
{
    /* Also checked on the third TC read, before TC is made visible. */
    assert(saved_count > 0 && result_count == saved_count);
    assert(result_head == saved_head);
    assert(memcmp(result_buffer, saved_values, sizeof saved_values) == 0);
    assert(memcmp(result_sequences, saved_sequences, sizeof saved_sequences) == 0);
}

/* Independent oracle: polynomial long division of message * x^8 by 0x107.
 * Unlike the firmware's XOR-byte update, feed bits then append eight zeros. */
static uint8_t reference_crc(const uint8_t *bytes, unsigned length)
{
    uint16_t remainder = 0;
    for (unsigned i = 0; i <= length; ++i) {
        unsigned byte = i < length ? bytes[i] : 0;
        for (unsigned bit = 0; bit < 8; ++bit) {
            remainder = (uint16_t)((remainder << 1) | ((byte >> (7 - bit)) & 1u));
            if (remainder & 0x100u)
                remainder ^= 0x107u;
        }
    }
    return (uint8_t)remainder;
}

static uint8_t firmware_crc(const uint8_t *bytes, unsigned length)
{
    uint8_t crc = 0;
    for (unsigned i = 0; i < length; ++i)
        crc = crc8_update(crc, bytes[i]);
    return crc;
}

static void make_record(uint8_t record[6], uint8_t sequence, uint32_t value)
{
    record[0] = sequence;
    record[1] = (uint8_t)(value >> 24);
    record[2] = (uint8_t)(value >> 16);
    record[3] = (uint8_t)(value >> 8);
    record[4] = (uint8_t)value;
    record[5] = reference_crc(record, 5);
}

static void check_corruptions(const uint8_t record[6])
{
    assert(reference_crc(record, 5) == record[5]);
    assert(firmware_crc(record, 5) == record[5]);
    for (unsigned position = 0; position < 5; ++position) {
        for (unsigned delta = 1; delta <= UINT8_MAX; ++delta) {
            uint8_t corrupted[6];
            memcpy(corrupted, record, sizeof corrupted);
            corrupted[position] ^= (uint8_t)delta;
            uint8_t crc = reference_crc(corrupted, 5);
            assert(crc != corrupted[5]);
            assert(firmware_crc(corrupted, 5) == crc);
        }
    }
}

static void test_crc(void)
{
    const uint8_t digits[] = {0x31, 0x32, 0x33, 0x34, 0x35,
                              0x36, 0x37, 0x38, 0x39};
    const uint8_t first[] = {0x00, 0x00, 0x00, 0x00, 0x7d, 0x74};
    const uint8_t second[] = {0x7f, 0x00, 0x00, 0x00, 0x7d, 0xda};
    assert(reference_crc(digits, sizeof digits) == 0xf4);
    assert(firmware_crc(digits, sizeof digits) == 0xf4);
    check_corruptions(first);
    check_corruptions(second);
    /* Every possible crc8_update input, including nonzero initial CRCs. */
    for (unsigned crc = 0; crc <= UINT8_MAX; ++crc) {
        for (unsigned byte = 0; byte <= UINT8_MAX; ++byte) {
            uint8_t combined = (uint8_t)(crc ^ byte);
            assert(crc8_update((uint8_t)crc, (uint8_t)byte)
                   == reference_crc(&combined, 1));
        }
    }
}

static void reset_output(void)
{
    assert(!data_written);
    output_used = status_reads = 0;
    /* Model the previous W1C write having cleared the hardware latch. */
    pending = 0;
}

static void request(void)
{
    saved_count = result_count;
    saved_head = result_head;
    memcpy(saved_values, result_buffer, sizeof saved_values);
    memcpy(saved_sequences, result_sequences, sizeof saved_sequences);
    expected_bytes = 6u * result_count;
    tc_reads = 0;
    pending = REQUEST_EVENT;
    uart_send_if_requested();
    assert(output_used == expected_bytes && !data_written);
    assert(result_count == 0);
    if (expected_bytes) {
        assert(tc_reads == 3);
        assert(result_head == (saved_head + saved_count) % BUFFER_CAPACITY);
    } else {
        assert(status_reads == 0 && tc_reads == 0);
    }
    /* The model stores the W1C mask rather than emulating EXTI. */
    assert(pending == REQUEST_EVENT);
    pending = 0;
    unsigned reads = status_reads;
    uart_send_if_requested();
    assert(output_used == expected_bytes && status_reads == reads);
}

static void assert_output(const uint8_t *expected, unsigned length)
{
    assert(output_used == length);
    assert(memcmp(output, expected, length) == 0);
}

int main(void)
{
    test_crc();
    exticr = 0xffffffffu;
    intenr = evenr = 0x200u;
    rtenr = 0xffffffffu;
    ftenr = 0x200u;
    request_init();
    assert((exticr & (3u << 8)) == (2u << 8));
    assert((exticr & ~(3u << 8)) == (0xffffffffu & ~(3u << 8)));
    assert(intenr == (0x200u | REQUEST_EVENT));
    assert(evenr == (0x200u | REQUEST_EVENT));
    assert(!(rtenr & REQUEST_EVENT));
    assert(ftenr == (0x200u | REQUEST_EVENT));
    assert(pending == REQUEST_EVENT);

    reset_output();
    request();
    assert(output_used == 0 && status_reads == 0);
    assert(next_sequence == 0);
    unsigned generated = 0;
    uint8_t expected[6 * BUFFER_CAPACITY];
    /* Every SEQ, including FE -> FF -> 00 -> 01 across separate requests. */
    for (unsigned i = 0; i < 512; ++i) {
        result_push(125);
        ++generated;
        reset_output();
        uart_send_if_requested();
        assert(output_used == 0 && status_reads == 0 && result_count == 1);
        request();
        make_record(expected, (uint8_t)i, 125);
        assert_output(expected, 6);
        if (i == 0) {
            const uint8_t vector[] = {0x00, 0x00, 0x00, 0x00, 0x7d, 0x74};
            assert_output(vector, sizeof vector);
        }
        if (i == 0x7f) {
            const uint8_t vector[] = {0x7f, 0x00, 0x00, 0x00, 0x7d, 0xda};
            assert_output(vector, sizeof vector);
        }
        assert(next_sequence == (uint8_t)generated);
        reset_output();
        request();
        assert(output_used == 0 && next_sequence == (uint8_t)generated);
    }

    /* Multiple buffer and SEQ wraparounds, retaining the newest entries. */
    unsigned sequence_base = generated;
    unsigned pushed = 3 * BUFFER_CAPACITY + 258;
    for (unsigned i = 0; i < pushed; ++i)
        result_push(0xff000000u + i);
    assert(result_count == BUFFER_CAPACITY);
    generated += pushed;
    assert(next_sequence == (uint8_t)generated);
    unsigned first = pushed - BUFFER_CAPACITY;
    for (unsigned i = 0; i < BUFFER_CAPACITY; ++i) {
        unsigned index = (result_head + i) % BUFFER_CAPACITY;
        uint32_t value = 0xff000000u + first + i;
        uint8_t sequence = (uint8_t)(sequence_base + first + i);
        assert(result_buffer[index] == value);
        assert(result_sequences[index] == sequence);
        make_record(expected + 6 * i, sequence, value);
    }
    reset_output();
    uart_send_if_requested();
    assert(output_used == 0 && status_reads == 0);
    assert(result_count == BUFFER_CAPACITY);
    request();
    assert_output(expected, sizeof expected);
    assert(next_sequence == (uint8_t)generated);

    /* Embedded NUL/LF/CR/FF are data, not terminators or escaping triggers. */
    const uint32_t values[] = {0, 1, 0x000a0dffu, 0xff0d0a00u, UINT32_MAX};
    for (unsigned i = 0; i < sizeof values / sizeof values[0]; ++i) {
        reset_output();
        result_push(values[i]);
        request();
        make_record(expected, (uint8_t)generated++, values[i]);
        assert_output(expected, 6);
        check_corruptions(output);
        assert(next_sequence == (uint8_t)generated);
    }
    reset_output();
    request();
    assert(output_used == 0 && next_sequence == (uint8_t)generated);
}
'''


class ProtocolTest(unittest.TestCase):
    def test_buffer_and_requests(self):
        for capacity in (1, 3, 32, 128):
            with self.subTest(capacity=capacity), tempfile.TemporaryDirectory() as temp:
                source = Path(temp) / 'test.c'
                binary = Path(temp) / 'test'
                source.write_text(HARNESS)
                subprocess.run([
                    os.environ.get('HOST_CC', 'cc'), '-std=c11', '-O2',
                    '-Wall', '-Wextra', '-Werror', '-I', str(ROOT),
                    f'-DBUFFER_CAPACITY={capacity}', str(source), '-o', str(binary),
                ], check=True)
                subprocess.run([str(binary)], check=True)


if __name__ == '__main__':
    unittest.main()
