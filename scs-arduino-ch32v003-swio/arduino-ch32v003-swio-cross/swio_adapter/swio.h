#ifndef SWIO_H
#define SWIO_H
#include <avr/io.h>
#include <util/delay.h>

#if !defined(__AVR_ATmega328P__) || F_CPU != 16000000UL
#error "SWIO requires an ATmega328P running at 16 MHz (Arduino Uno/Nano)."
#endif

// SWIO on PB0
#define SWIO_DDR  DDRB
#define SWIO_PORT PORTB
#define SWIO_PIN  PINB
#define SWIO_BIT  0

#ifdef __cplusplus
extern "C" {
#endif

// Call read/write with interrupts disabled; init uses long delays.
void swio_init(void);
void swio_write_reg(uint8_t addr, uint32_t val);
uint32_t swio_read_reg(uint8_t addr);

#ifdef __cplusplus
}
#endif

#endif
