#include "registers.h"
#include "statistics.h"

#ifndef BLOCK_STANDBY
#define BLOCK_STANDBY 1
#endif
#if BLOCK_STANDBY != 0 && BLOCK_STANDBY != 1
#error "BLOCK_STANDBY must be 0 or 1"
#endif

#ifndef AWU_WINDOW
#define AWU_WINDOW 29u
#endif
#if AWU_WINDOW < 0 || AWU_WINDOW > 63
#error "AWU_WINDOW must be 0..63"
#endif

#if BLOCK_STANDBY
extern void standby_wfe(void);
#endif

static uint32_t discharge_window[WINDOW_BLOCKS];

#ifndef BUFFER_CAPACITY
#define BUFFER_CAPACITY 32
#endif
#if BUFFER_CAPACITY < 1 || BUFFER_CAPACITY > 256
#error "BUFFER_CAPACITY must be 1..256; the linker also checks RAM space"
#endif

/* Store thousandths and sequence numbers separately to avoid padding.
 * CRC is computed only when sending a record. */
static uint32_t result_buffer[BUFFER_CAPACITY];
static uint8_t result_sequences[BUFFER_CAPACITY];
static uint8_t next_sequence;
static uint32_t result_head;
static uint32_t result_count;

static void result_push(uint32_t value_milli)
{
    uint32_t index = result_head + result_count;
    if (index >= BUFFER_CAPACITY)
        index -= BUFFER_CAPACITY;

    result_buffer[index] = value_milli;
    result_sequences[index] = next_sequence++;

    if (result_count == BUFFER_CAPACITY) {
        if (++result_head == BUFFER_CAPACITY)
            result_head = 0;
    } else {
        ++result_count;
    }
}

#ifndef SAMPLES
#define SAMPLES 16
#endif
/* Max D is SAMPLES * 65535 / 2; thousandths must fit in uint32_t. */
#if SAMPLES < 1 || SAMPLES > 131
#error "SAMPLES must be 1..131"
#endif

/* SOP8, full TIM2 remap: CH1=PC1 (B, pin 5), CH3=PD6 (A, pin 1).
 * PA1 shares pin 1: leave it floating; keep the external oscillator off. */
#define A_PIN (1u << 6)
#define B_PIN (1u << 1)
#define A_CFG_SHIFT 24u
#define B_CFG_SHIFT 4u

#define SELECT_PIN (1u << 4)        /* PC4, SOP8 pin 7 */
#define SELECT_CFG_SHIFT 16u

#define GPIO_PULL    0x8u           /* Input pull-up/down; OUTDR selects direction */
#define GPIO_OUT_PP  0x3u           /* Push-pull output, 30 MHz setting */
#define GPIO_AF_PP   0xbu           /* Alternate function push-pull */
#define GPIO_FLOAT   0x4u

#define START_DELAY_TICKS 8u

#ifndef CHARGE_SETTLE_LOOPS
#define CHARGE_SETTLE_LOOPS 64u
#endif

#ifndef DISCHARGE_SETTLE_LOOPS
#define DISCHARGE_SETTLE_LOOPS 0u
#endif
#if CHARGE_SETTLE_LOOPS < 0 || DISCHARGE_SETTLE_LOOPS < 0
#error "Delay loop counts must be nonnegative"
#endif

/* Keep a common implementation for the firmware and diagnostic sweep.
 * A loop iteration is not one CPU cycle; zero skips the loop body. */
__attribute__((noinline, noclone)) static void delay_loops(uint32_t loops)
{
    for (uint32_t i = 0; i < loops; ++i)
        __asm__ volatile ("nop");

    __asm__ volatile ("" ::: "memory");
}

static void clock_init(void)
{
    /* HSI on; preserve the factory oscillator trim. */
    RCC_CTLR |= 1u;
    while (!(RCC_CTLR & (1u << 1))) {
    }

    RCC_CFGR0 &= ~3u;              /* SYSCLK = HSI */
    while (RCC_CFGR0 & (3u << 2)) {
    }

    RCC_CFGR0 &= ~(0xfu << 4);     /* HCLK = SYSCLK / 1 = nominal 24 MHz */
    FLASH_ACTLR &= ~3u;            /* Zero wait states at 24 MHz */
}

#if BLOCK_STANDBY
static uint32_t standby_init(void)
{
    /* USER[2]=1 means resume, not reset, on this part (RM option table).
     * Do not change option bytes automatically on an unexpected board. */
    if (!(FLASH_USER & (1u << 2)))
        return 0;

    RCC_APB1PCENR |= 1u << 28;     /* PWR */
    (void)RCC_APB1PCENR;
    RCC_RSTSCKR |= 1u;            /* LSI on */
    while (!(RCC_RSTSCKR & (1u << 1))) {
    }

    PWR_AWUCSR &= ~AWU_ENABLE;
    PWR_AWUPSC = (PWR_AWUPSC & ~15u) | 7u; /* LSI / 64 */
    PWR_AWUWR = (PWR_AWUWR & ~63u) | AWU_WINDOW;

    /* Event-only EXTI9 falling edge, as in WCH Standby_Mode example.
     * No PFIC IRQ and no change to the startup's global interrupt mask. */
    EXTI_INTENR &= ~AWU_EVENT;
    EXTI_RTENR &= ~AWU_EVENT;
    EXTI_FTENR |= AWU_EVENT;
    EXTI_EVENR |= AWU_EVENT;
    EXTI_INTFR = AWU_EVENT;       /* W1C */

    return 1;
}

static void standby_between_blocks(void)
{
    const uint32_t saved_sctlr = PFIC_SCTLR;

    PWR_AWUCSR &= ~AWU_ENABLE;
    EXTI_INTFR = AWU_EVENT;
    PWR_CTLR |= PWR_PDDS;

    /* Set and consume a known event, then wait for a fresh AWU event.
     * Unlike the SDK wrapper, never unconditionally enable interrupts. */
    PFIC_SCTLR = saved_sctlr | SLEEPDEEP | WFITOWFE | SETEVENT;
    PFIC_SCTLR = (saved_sctlr & ~SETEVENT) | SLEEPDEEP | WFITOWFE;
    standby_wfe();
    PFIC_SCTLR |= WFITOWFE;
    PWR_AWUCSR |= AWU_ENABLE;

    /* The synthetic event may have consumed a real PC4 event too. Its
     * sticky interrupt flag survives: do not sleep again if it is pending.
     * An edge after this check supplies the final WFE's wake event. */
    if (!(EXTI_INTFR & REQUEST_EVENT))
        standby_wfe();

    PFIC_SCTLR = saved_sctlr;
    PWR_AWUCSR &= ~AWU_ENABLE;
    EXTI_INTFR = AWU_EVENT;
    PWR_CTLR &= ~PWR_PDDS;
    clock_init();                /* Wake clock is HSI; retain 24 MHz timing. */
}
#endif

static uint32_t uart_init_if_selected(void)
{
    /* PC4 pull-up: open = UART, grounded = keep SWIO untouched. */
    GPIOC_BSHR = SELECT_PIN;
    GPIOC_CFGLR = (GPIOC_CFGLR & ~(0xfu << SELECT_CFG_SHIFT))
                 | (GPIO_PULL << SELECT_CFG_SHIFT);
    delay_loops(2400u);           /* Let the selector pull-up settle */
    if (!(GPIOC_INDR & SELECT_PIN))
        return 0;

    RCC_APB2PCENR |= 1u << 14;     /* USART1 */
    (void)RCC_APB2PCENR;
    USART_CTLR1 = 0;
    USART_CTLR2 = 0;               /* One stop bit, no clock output */
    USART_CTLR3 = 0;               /* No flow control, DMA or special modes */
    USART_BRR = 2500u;             /* 24 MHz / 9600 */

    /* PD1, PD4 and PD5 share SOP8 pin 8: only PD5 may drive it. */
    GPIOD_CFGLR = (GPIOD_CFGLR & ~((0xfu << 4) | (0xfu << 16)))
                 | (GPIO_FLOAT << 4) | (GPIO_FLOAT << 16);

    /* SWCFG=100 disables SWIO; USART1_RM[21,2]=00 selects TX on PD5.
     * Preserve TIM2 full remap and all unrelated fields. */
    AFIO_PCFR1 = (AFIO_PCFR1 & ~((7u << 24) | (1u << 21) | (1u << 2)))
                 | (4u << 24);

    USART_CTLR1 = USART_UE | USART_TE; /* TX only, 8 bits, no parity */
    GPIOD_BSHR = 1u << 5;
    GPIOD_CFGLR = (GPIOD_CFGLR & ~(0xfu << 20)) | (GPIO_AF_PP << 20);

    return 1;
}

static void uart_write_byte(uint8_t byte)
{
    while (!(USART_STATR & USART_TXE)) {
    }

    USART_DATAR = byte;
}

/* CRC-8/ATM: poly=0x07, init=0, no reflection or final XOR. */
static uint8_t crc8_update(uint8_t crc, uint8_t byte)
{
    crc ^= byte;
    for (uint32_t bit = 0; bit < 8; ++bit)
        crc = (uint8_t)((crc << 1) ^ ((crc & 0x80u) ? 0x07u : 0u));

    return crc;
}

static void request_init(void)
{
    /* RM 6.4, 7.3.2.2: EXTI4 uses a two-bit port field, PC = 10.
     * INTENR latches INTFR even though PFIC IRQs and mstatus.MIE stay off;
     * EVENR independently supplies a wake event, including in Standby. */
    AFIO_EXTICR = (AFIO_EXTICR & ~(3u << 8)) | (2u << 8);
    EXTI_RTENR &= ~REQUEST_EVENT;
    EXTI_FTENR |= REQUEST_EVENT;
    EXTI_INTFR = REQUEST_EVENT;
    EXTI_INTENR |= REQUEST_EVENT;
    EXTI_EVENR |= REQUEST_EVENT;
}

static void uart_send_if_requested(void)
{
    if (!(EXTI_INTFR & REQUEST_EVENT))
        return;

    if (result_count) {
        uint32_t index = result_head;
        for (uint32_t n = 0; n < result_count; ++n) {
            uint8_t sequence = result_sequences[index];
            uint8_t crc = crc8_update(0, sequence);
            uart_write_byte(sequence);

            uint32_t value_milli = result_buffer[index];
            for (uint32_t i = 0; i < 4; ++i) {
                uint8_t byte = (uint8_t)(value_milli >> 24);
                uart_write_byte(byte);
                crc = crc8_update(crc, byte);
                value_milli <<= 8;
            }

            uart_write_byte(crc);
            if (++index == BUFFER_CAPACITY)
                index = 0;
        }

        while (!(USART_STATR & USART_TC)) {
        }

        /* Only discard the batch once the last stop bit has left UART. */
        result_head = index;
        result_count = 0;
    }

    /* W1C: discard this request and any further edges during transmission.
     * A held-low input is not another request; a new falling edge is needed. */
    EXTI_INTFR = REQUEST_EVENT;
}

static void b_input(void)
{
    GPIOC_CFGLR = (GPIOC_CFGLR & ~(0xfu << B_CFG_SHIFT))
                 | (GPIO_FLOAT << B_CFG_SHIFT);
}

static void a_low(void)
{
    TIM_CHCTLR2 = OC3_FORCE_LOW;
}

static void discharge_and_hold(void)
{
    a_low();
    GPIOC_BSHR = B_PIN << 16;      /* Latch low BEFORE enabling the driver */
    GPIOC_CFGLR = (GPIOC_CFGLR & ~(0xfu << B_CFG_SHIFT))
                 | (GPIO_OUT_PP << B_CFG_SHIFT);
#if DISCHARGE_SETTLE_LOOPS > 0
    delay_loops(DISCHARGE_SETTLE_LOOPS);
#endif
}

static void hardware_init(void)
{
    clock_init();

    RCC_APB2PCENR |= (1u << 0) | (1u << 4) | (1u << 5); /* AFIO + GPIOC + GPIOD */
    RCC_APB1PCENR |= 1u;                    /* TIM2 */
    (void)RCC_APB2PCENR;
    (void)RCC_APB1PCENR;

    RCC_APB1PRSTR |= 1u;
    RCC_APB1PRSTR &= ~1u;
    AFIO_PCFR1 = (AFIO_PCFR1 & ~(3u << 8)) | (3u << 8); /* Full TIM2 remap */

    TIM_CTLR1 = 0;
    TIM_CTLR2 = 0;
    TIM_SMCFGR = 0;                /* Internal clock, no slave mode */
    TIM_DMAINTENR = 0;             /* No interrupts or DMA */
    TIM_CHCTLR2 = 0;
    TIM_PSC = 0;
    TIM_ATRLR = 0xffffu;
    TIM_CHCTLR1 = CC1_DIRECT_TI1;
    TIM_CHCTLR2 = OC3_FORCE_LOW;
    TIM_CCER = CC3E;               /* Active-high channel 3 output */

    GPIOD_BSHR = A_PIN << 16;
    GPIOC_BSHR = B_PIN << 16;
    GPIOC_CFGLR = (GPIOC_CFGLR & ~(0xfu << B_CFG_SHIFT))
                 | (GPIO_OUT_PP << B_CFG_SHIFT);
    uint32_t config = GPIOD_CFGLR & ~(0xfu << A_CFG_SHIFT);
    config |= GPIO_AF_PP << A_CFG_SHIFT;
    GPIOD_CFGLR = config;

    discharge_and_hold();
}

/* falling=0: start charging and capture B rising.
 * falling=1: start discharging through R and capture B falling.
 * B MUST remain an input between these phases. */
static uint32_t measure_phase(uint32_t falling)
{
    /* Reinitialize the output-compare state before each charge; force-low
     * + UG + CNT=0 was not sufficient in hardware tests.
     * Do not reset between charge/discharge: A must remain high there. */
    if (!falling) {
        RCC_APB1PRSTR |= 1u;
        RCC_APB1PRSTR &= ~1u;
        TIM_PSC = 0;
        TIM_ATRLR = 0xffffu;
        TIM_CHCTLR1 = CC1_DIRECT_TI1;
    }

    TIM_CTLR1 = 0;
    const uint16_t output_enable = CC3E;
    TIM_CCER = output_enable;      /* Capture off while reconfiguring */
    TIM_CHCTLR2 = falling ? OC3_FORCE_HIGH : OC3_FORCE_LOW;
    const uint16_t polarity = falling ? CC1P : 0;
    TIM_CCER = output_enable | polarity;
    TIM_SWEVGR = UG;               /* Reset prescaler phase */
    TIM_CNT = 0;                  /* Explicit phase origin; verified on hardware */
    TIM_INTFR = 0;                 /* Includes clearing the UG update flag */
    TIM_CH3CVR = START_DELAY_TICKS;

    /* CC3S=00, OC3PE=0: immediate compare value, no preload. */
    TIM_CHCTLR2 = falling ? OC3_LOW_ON_MATCH : OC3_HIGH_ON_MATCH;
    TIM_CCER = output_enable | polarity | CC1E;

    TIM_CTLR1 = CEN;

    while (!(TIM_INTFR & CC1IF)) {
    }

    TIM_CTLR1 = 0;
    TIM_CCER = output_enable | polarity;

    return TIM_CH1CVR & 0xffffu;
}

int main(void)
{
    hardware_init();

    const uint32_t uart_enabled = uart_init_if_selected();
    if (uart_enabled)
        request_init();

#if BLOCK_STANDBY
    const uint32_t standby_ready = standby_init();
#endif

    uint32_t window_used = 0;

    for (;;) {
        if (uart_enabled)
            uart_send_if_requested();

        uint32_t discharged = 0;
        for (uint32_t i = 0; i < SAMPLES; ++i) {
            b_input();
            /* Preserve charge capture; only discharge feeds the statistic. */
            (void)measure_phase(0);

            /* A stays high, B stays floating. Fully charge before discharge. */
            delay_loops(CHARGE_SETTLE_LOOPS);
            discharged += measure_phase(1);
            discharge_and_hold();
        }

        discharge_window[window_used++] = discharged;
        if (window_used == WINDOW_BLOCKS) {
            result_push(deviation_milli(deviation_total(discharge_window)));
            window_used = 0;
        }

        if (uart_enabled)
            uart_send_if_requested();

#if BLOCK_STANDBY
        /* A/B are already held low and any UART transmission is complete. */
        if (standby_ready)
            standby_between_blocks();
#endif
    }
}
