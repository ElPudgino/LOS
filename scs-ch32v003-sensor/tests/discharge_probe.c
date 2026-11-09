/* SWIO diagnostic for forced-GPIO discharge. The test delay is compiled
 * into the production discharge_and_hold(), including complete removal
 * of its delay call at zero. No statistics are collected between that
 * operation and the following charge capture. */
#define main statistics_main
#include "../main.c"
#undef main

#define PROBE_PAIRS 4096u
#define PROBE_WARMUP 64u
#define PROBE_BINS 32u
#define PROBE_RAW 64u

struct discharge_probe_result {
    uint32_t magic;
    uint32_t status;
    uint32_t discharge_loops;
    uint32_t pairs;

    uint32_t reference_sum;
    uint32_t charge_sum;
    uint32_t discharge_sum;

    uint32_t reference_min;
    uint32_t reference_max;
    uint32_t charge_min;
    uint32_t charge_max;
    uint32_t discharge_min;
    uint32_t discharge_max;

    uint32_t reference_outside;
    uint32_t charge_outside;
    uint32_t discharge_outside;

    uint16_t reference_hist[PROBE_BINS];
    uint16_t charge_hist[PROBE_BINS];
    uint16_t discharge_hist[PROBE_BINS];

    uint16_t raw[PROBE_RAW][4]; /* reference charge/discharge, test charge/discharge */
};

volatile struct discharge_probe_result discharge_probe;

int main(void)
{
    hardware_init();

    discharge_probe.magic = 0x44504731u;
    discharge_probe.discharge_loops = DISCHARGE_SETTLE_LOOPS;
    discharge_probe.reference_min = 65535u;
    discharge_probe.charge_min = 65535u;
    discharge_probe.discharge_min = 65535u;
    discharge_probe.status = 1;
    delay_loops(2400u);

    for (uint32_t i = 0; i < PROBE_WARMUP + PROBE_PAIRS; ++i) {
        /* Reference always follows a long forced-discharge hold. */
        b_input();
        uint32_t reference = measure_phase(0);
        delay_loops(CHARGE_SETTLE_LOOPS);
        uint32_t reference_discharge = measure_phase(1);

        /* This interval deliberately contains no histogram/counter work. */
        discharge_and_hold();
        /* Inline the same GPIO operation as b_input(), avoiding an extra
         * diagnostic call that could hide a too-short discharge hold. */
        GPIOC_CFGLR = (GPIOC_CFGLR & ~(0xfu << B_CFG_SHIFT))
                     | (GPIO_FLOAT << B_CFG_SHIFT);
        uint32_t charge = measure_phase(0);
        delay_loops(CHARGE_SETTLE_LOOPS);
        uint32_t discharge = measure_phase(1);
        discharge_and_hold();
        delay_loops(2400u); /* Recover a fully discharged reference state. */

        if (i < PROBE_WARMUP)
            continue;

        discharge_probe.reference_sum += reference;
        discharge_probe.charge_sum += charge;
        discharge_probe.discharge_sum += discharge;

        if (reference < discharge_probe.reference_min)
            discharge_probe.reference_min = reference;
        if (reference > discharge_probe.reference_max)
            discharge_probe.reference_max = reference;

        if (charge < discharge_probe.charge_min)
            discharge_probe.charge_min = charge;
        if (charge > discharge_probe.charge_max)
            discharge_probe.charge_max = charge;

        if (discharge < discharge_probe.discharge_min)
            discharge_probe.discharge_min = discharge;
        if (discharge > discharge_probe.discharge_max)
            discharge_probe.discharge_max = discharge;

        if (reference < PROBE_BINS)
            ++discharge_probe.reference_hist[reference];
        else
            ++discharge_probe.reference_outside;

        if (charge < PROBE_BINS)
            ++discharge_probe.charge_hist[charge];
        else
            ++discharge_probe.charge_outside;

        if (discharge < PROBE_BINS)
            ++discharge_probe.discharge_hist[discharge];
        else
            ++discharge_probe.discharge_outside;

        uint32_t slot = discharge_probe.pairs % PROBE_RAW;
        discharge_probe.raw[slot][0] = (uint16_t)reference;
        discharge_probe.raw[slot][1] = (uint16_t)reference_discharge;
        discharge_probe.raw[slot][2] = (uint16_t)charge;
        discharge_probe.raw[slot][3] = (uint16_t)discharge;
        ++discharge_probe.pairs;
    }

    discharge_probe.status = 2;

    /* Keep A/B low and RAM untouched until a single debugger read. */
    for (;;)
        __asm__ volatile ("nop");
}
