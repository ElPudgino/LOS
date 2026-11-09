/* Standalone SWIO sweep: use the production capture/delay functions,
 * keep the forced-GPIO discharge delay unchanged, and vary only the
 * post-charge delay at runtime in ONE binary to avoid code-layout changes.
 * No UART or debugger memory accesses occur during data collection. */
#define main statistics_main
#include "../main.c"
#undef main

#define PROBE_SAMPLES 4096u
#define PROBE_WARMUP 64u
#define PROBE_RAW 64u
#define PROBE_HIST_BASE 8u
#define PROBE_BINS 16u

/* Bracket each short-delay trial with a long-delay reference. */
static const uint32_t charge_delays[] = {
    2400, 600, 2400, 900, 2400, 1200, 2400
};

#define PROBE_STAGES (sizeof(charge_delays) / sizeof(charge_delays[0]))

struct delay_probe_row {
    uint32_t charge_loops;
    uint32_t samples;
    uint32_t charge_sum;
    uint32_t discharge_sum;
    uint32_t charge_min;
    uint32_t charge_max;
    uint32_t discharge_min;
    uint32_t discharge_max;
    uint32_t histogram_outside;  /* Discharge capture outside [8, 23] */
    uint16_t discharge_hist[PROBE_BINS];
};

struct delay_probe_result {
    uint32_t magic;
    uint32_t status;             /* 1=collecting, 2=complete */
    uint32_t stage;
    uint32_t discharge_loops;
    struct delay_probe_row rows[PROBE_STAGES];
    uint16_t raw[PROBE_RAW][2];   /* Last captures: charge, discharge */
};

volatile struct delay_probe_result delay_probe;

int main(void)
{
    hardware_init();

    delay_probe.magic = 0x44505332u;
    delay_probe.discharge_loops = DISCHARGE_SETTLE_LOOPS;
    delay_probe.status = 1;

    for (uint32_t stage = 0; stage < PROBE_STAGES; ++stage) {
        delay_probe.stage = stage;
        volatile struct delay_probe_row *row = &delay_probe.rows[stage];
        row->charge_loops = charge_delays[stage];
        row->charge_min = 65535u;
        row->discharge_min = 65535u;

        for (uint32_t i = 0; i < PROBE_WARMUP + PROBE_SAMPLES; ++i) {
            b_input();
            uint32_t charge = measure_phase(0);
            delay_loops(row->charge_loops);
            uint32_t discharge = measure_phase(1);
            discharge_and_hold();

            if (i < PROBE_WARMUP)
                continue;

            row->charge_sum += charge;
            row->discharge_sum += discharge;

            if (charge < row->charge_min)
                row->charge_min = charge;
            if (charge > row->charge_max)
                row->charge_max = charge;

            if (discharge < row->discharge_min)
                row->discharge_min = discharge;
            if (discharge > row->discharge_max)
                row->discharge_max = discharge;

            if (discharge >= PROBE_HIST_BASE &&
                discharge < PROBE_HIST_BASE + PROBE_BINS)
                ++row->discharge_hist[discharge - PROBE_HIST_BASE];
            else
                ++row->histogram_outside;

            uint32_t slot = row->samples % PROBE_RAW;
            delay_probe.raw[slot][0] = (uint16_t)charge;
            delay_probe.raw[slot][1] = (uint16_t)discharge;
            ++row->samples;
        }
    }

    delay_probe.stage = PROBE_STAGES;
    delay_probe.status = 2;

    /* Keep A/B low and RAM untouched until a single debugger read. */
    for (;;)
        __asm__ volatile ("nop");
}
