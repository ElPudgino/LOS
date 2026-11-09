#!/usr/bin/env python3
"""Host register-model checks, not a simulation of hardware sleep/timing."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
HARNESS = r'''
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include "registers.h"

static struct { uint32_t address, value; } regs[] = {
    {0x40021000u, 2}, {0x40021004u, 0xf0}, {0x4002101cu, 0},
    {0x40021024u, 2}, {0x40022000u, 3}, {0x40007000u, 0x10},
    {0x40007008u, 0}, {0x4000700cu, 0}, {0x40007010u, 0},
    {0x40010400u, 0xffffffffu}, {0x40010404u, 0x20},
    {0x40010408u, 0xffffffffu}, {0x4001040cu, 0x20},
    {0x40010414u, 0}, {0xe000ed10u, 0x10}
};
static uint16_t user_option;
static uint32_t *fake32(uint32_t address)
{
    for (unsigned i = 0; i < sizeof(regs)/sizeof(regs[0]); ++i)
        if (regs[i].address == address)
            return &regs[i].value;
    abort();
}
static uint16_t *fake16(uint32_t address)
{
    assert(address == 0x1ffff802u);
    return &user_option;
}
#undef REG32
#undef REG16
#define REG32(a) (*(volatile uint32_t *)fake32(a))
#define REG16(a) (*(volatile uint16_t *)fake16(a))
#define main firmware_main
#include "main.c"
#undef main

static unsigned wfe_calls;
static unsigned request_on_drain;
void standby_wfe(void)
{
    assert((PFIC_SCTLR & (SLEEPDEEP | WFITOWFE)) == (SLEEPDEEP | WFITOWFE));
    assert(!(PFIC_SCTLR & SETEVENT));
    assert(PWR_CTLR & PWR_PDDS);
    if ((++wfe_calls & 1u) == 1u) {
        assert(!(PWR_AWUCSR & AWU_ENABLE));
        if (request_on_drain)
            EXTI_INTFR = REQUEST_EVENT;
    } else {
        assert(PWR_AWUCSR & AWU_ENABLE);
        EXTI_INTFR = 0; /* Event-only wake need not set pending. */
    }
    PFIC_SCTLR &= ~WFITOWFE; /* Require caller to set it again. */
}

int main(void)
{
    user_option = 0;
    assert(standby_init() == 0);
    assert(!(RCC_APB1PCENR & (1u << 28)));
    assert(!(RCC_RSTSCKR & 1u));

    user_option = 4;
    assert(standby_init() == 1);
    assert(RCC_APB1PCENR & (1u << 28));
    assert(RCC_RSTSCKR & 1u);
    assert((PWR_AWUPSC & 15u) == 7);
    assert((PWR_AWUWR & 63u) == AWU_WINDOW);
    assert(!(PWR_AWUCSR & AWU_ENABLE));
    assert(!(EXTI_INTENR & AWU_EVENT));
    assert(!(EXTI_RTENR & AWU_EVENT));
    assert(EXTI_FTENR == (0x20u | AWU_EVENT));
    assert(EXTI_EVENR == (0x20u | AWU_EVENT));
    /* The register model stores the W1C write; it does not emulate EXTI. */
    assert(EXTI_INTFR == AWU_EVENT);

    for (unsigned i = 1; i <= 2; ++i) {
        standby_between_blocks();
        assert(wfe_calls == 2*i);
        assert(PFIC_SCTLR == 0x10);
        assert(PWR_CTLR == 0x10);
        assert(!(PWR_AWUCSR & AWU_ENABLE));
        assert(EXTI_INTFR == AWU_EVENT);
        assert(RCC_CTLR & 1u);
        assert(!(RCC_CFGR0 & 0xf0u));
        assert(!(FLASH_ACTLR & 3u));
    }
    /* A PC4 edge consumed with the synthetic event must prevent the second
     * WFE, rather than delaying the reply until AWU. */
    request_on_drain = 1;
    standby_between_blocks();
    assert(wfe_calls == 5);
    assert(PFIC_SCTLR == 0x10);
    assert(!(PWR_AWUCSR & AWU_ENABLE));
}
'''


class StandbyTest(unittest.TestCase):
    def test_event_setup_double_wfe_and_restore(self):
        with tempfile.TemporaryDirectory() as temp:
            source = Path(temp) / 'test.c'
            binary = Path(temp) / 'test'
            source.write_text(HARNESS)
            subprocess.run([
                os.environ.get('HOST_CC', 'cc'), '-std=c11', '-O2',
                '-Wall', '-Wextra', '-Werror',
                '-DBLOCK_STANDBY=1', '-DAWU_WINDOW=22', '-I', str(ROOT),
                str(source), '-o', str(binary),
            ], check=True)
            subprocess.run([str(binary)], check=True)


if __name__ == '__main__':
    unittest.main()
