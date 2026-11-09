#!/usr/bin/env python3
"""Host-only checks; production firmware uses neither stdio nor floating point."""
import os
from pathlib import Path
import random
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
MAX_TOTAL = 64 * 131 * 65535
HARNESS = r'''
#include <inttypes.h>
#include <stdio.h>
#include "statistics.h"
int main(int argc, char **argv)
{
    (void)argv;
    if (argc > 1) {
        uint64_t total;
        while (scanf("%" SCNu64, &total) == 1)
            printf("%" PRIu32 "\n", deviation_milli(total));
        return 0;
    }
    struct { uint32_t before, values[WINDOW_BLOCKS], after; } window;
    for (;;) {
        window.before = window.after = 0x12345678u;
        for (unsigned i = 0; i < WINDOW_BLOCKS; ++i)
            if (scanf("%" SCNu32, &window.values[i]) != 1)
                return i == 0 ? 0 : 1;
        uint64_t total = deviation_total(window.values);
        if (window.before != 0x12345678u || window.after != 0x12345678u)
            return 2;
        for (unsigned i = 1; i < WINDOW_BLOCKS; ++i)
            if (window.values[i - 1] > window.values[i])
                return 3;
        printf("%" PRIu64 "\n", total);
    }
}
'''


class StatisticsTest(unittest.TestCase):
    def test_windows_and_fixed_point(self):
        rng = random.Random(42)
        cases = [
            [0] * 128,
            [416] * 128,
            [2**32 - 1] * 128,
            [0] * 64 + [1] * 64,  # Fractional mathematical median.
            [0] * 127 + [1],
            [0] * 127 + [16],
            [0] * 127 + [127],
            [0] * 64 + [2**32 - 1] * 64,  # 64-bit accumulator, independent of scaling.
            list(range(128)),
            list(reversed(range(128))),
            [0] * 64 + [65535 * 131] * 64,  # Maximum valid spread.
        ]
        for limit in (2, 8, 1024, 2**32):
            cases.extend([[rng.randrange(limit) for _ in range(128)] for _ in range(50)])
        # All fractional residues, including ties (total % 16 == 8),
        # integer boundaries and the largest allowed result.
        totals = list(range(257)) + list(range(MAX_TOTAL - 256, MAX_TOTAL + 1))
        totals += [rng.randrange(MAX_TOTAL + 1) for _ in range(1000)]
        with tempfile.TemporaryDirectory() as temp:
            source = Path(temp) / 'test.c'
            binary = Path(temp) / 'test'
            source.write_text(HARNESS)
            subprocess.run([
                os.environ.get('HOST_CC', 'cc'), '-std=c11', '-O2',
                '-Wall', '-Wextra', '-Werror', '-I', str(ROOT),
                str(source), '-o', str(binary),
            ], check=True)
            result = subprocess.run(
                [str(binary)], input='\n'.join(' '.join(map(str, c)) for c in cases),
                text=True, capture_output=True, check=True,
            )
            scaled = subprocess.run(
                [str(binary), '--milli'], input='\n'.join(map(str, totals)),
                text=True, capture_output=True, check=True,
            )
        expected_totals = [sum(abs(v - sorted(case)[63]) for v in case) for case in cases]
        self.assertEqual(list(map(int, result.stdout.splitlines())), expected_totals)
        self.assertEqual(list(map(int, scaled.stdout.splitlines())),
                         [(total * 1000 + 64) // 128 for total in totals])
        self.assertEqual((MAX_TOTAL * 1000 + 64) // 128, 4292542500)
        self.assertLessEqual((MAX_TOTAL * 1000 + 64) // 128, 2**32 - 1)


if __name__ == '__main__':
    unittest.main()
