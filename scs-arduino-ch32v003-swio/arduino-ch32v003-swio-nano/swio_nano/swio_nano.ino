// CH32V003 SWIO debug interface for Arduino Nano (ATmega328P, 16 MHz).
//
// Wiring:
//   D8 (PB0) - SWIO signal of the CH32V003 (1k series resistor advised)
//   D9 (PB1) - switches power to the target
//
// Protocol over Serial (115200 8N1):
//   '!'  - sent once on startup, means "ready"
//   '?'  - test, response: '+'
//   'p'  - target power on,  response: '+'
//   'P'  - target power off, response: '+'
//   'w' <reg:1b> <val:4b, LSB-first> - write debug register, response: '+'
//   'r' <reg:1b>              - read debug register, response: 4 bytes
//
// A DTR reset (stock Nano bootloader) re-initializes the adapter.

#include <avr/io.h>

// SWIO on PB0 (D8).
#define SWIO_DDR  DDRB
#define SWIO_PORT PORTB
#define SWIO_PIN  PINB
#define SWIO_BIT  0

// Target power on PB1 (D9).
#define TARGET_POWER_PORT PORTB
#define TARGET_POWER_DDR  DDRB
#define TARGET_POWER_BIT  1

// Protocol characters.
#define PROTOCOL_START     '!'
#define PROTOCOL_ACK       '+'
#define PROTOCOL_TEST      '?'
#define PROTOCOL_POWER_ON  'p'
#define PROTOCOL_POWER_OFF 'P'
#define PROTOCOL_WRITE_REG 'w'
#define PROTOCOL_READ_REG  'r'

// SWIO timing.
// T = 1 / 8 MHz
// 0 = T-4T low, T-16T high
// 1 = 6T-64T low, T-16T high
//
// Note: bitwise ops take up 2 clock cycles (1T).
// nop are 1 cycle (0.5T).

static inline void swioSendOne() {
    /* 0.0T */ SWIO_DDR |= _BV(SWIO_BIT);
    /* 1.0T */ SWIO_PORT &= ~_BV(SWIO_BIT);
    /* 2.0T */ asm("nop"); asm("nop");
    /* 3.0T */ SWIO_PORT |= _BV(SWIO_BIT);
    /* 4.0T */ SWIO_DDR &= ~_BV(SWIO_BIT);
}

static inline void swioSendZero() {
    /* 0.0T */ SWIO_DDR |= _BV(SWIO_BIT);
    /* 1.0T */ SWIO_PORT &= ~_BV(SWIO_BIT);
    /* 2.0T */ asm("nop"); asm("nop");
    /* 3.0T */ asm("nop"); asm("nop");
    /* 4.0T */ asm("nop"); asm("nop");
    /* 5.0T */ asm("nop"); asm("nop");
    /* 6.0T */ asm("nop"); asm("nop");
    /* 7.0T */ asm("nop"); asm("nop");
    /* 8.0T */ SWIO_PORT |= _BV(SWIO_BIT);
    /* 9.0T */ SWIO_DDR &= ~_BV(SWIO_BIT);
}

static inline char swioRecvBit() {
    char x;
    /* 0.0T */ SWIO_DDR |= _BV(SWIO_BIT);
    /* 1.0T */ SWIO_PORT &= ~_BV(SWIO_BIT);
    /* 2.0T */ SWIO_PORT |= _BV(SWIO_BIT); // Precharge when the line is floating.
    /* 3.0T */ SWIO_DDR &= ~_BV(SWIO_BIT);
    /* 4.0T */ asm("nop"); asm("nop");
    /* 5.0T */ asm("nop"); asm("nop");
    /* 6.0T */ x = SWIO_PIN;

    // Wait for the line to come back up if it's down.
    while (!(SWIO_PIN & _BV(SWIO_BIT)))
        ;

    return x & _BV(SWIO_BIT);
}

// Blocking read of one byte, matching the original fread() semantics.
// Serial.read() alone returns 0xFF when the RX buffer is empty, which would
// corrupt the register address / value if the argument bytes arrive after the
// command byte.
static uint8_t readByte() {
    while (Serial.available() == 0) {
    }
    return static_cast<uint8_t>(Serial.read());
}

static void targetPower(int x) {
    if (x)
        TARGET_POWER_PORT |= _BV(TARGET_POWER_BIT);
    else
        TARGET_POWER_PORT &= ~_BV(TARGET_POWER_BIT);
}

// Send the 7-bit register address, MSB first.
static void swioSendAddr(uint8_t addr) {
    char i;

    // Start bit.
    swioSendOne();

    for (i = 0; i < 7; i++) {
        if (addr & 0x40)
            swioSendOne();
        else
            swioSendZero();

        addr <<= 1;
    }
}

// Write a register.
void swioWriteReg(uint8_t addr, uint32_t val) {
    char i;

    // UART RX and Timer0 interrupts must not stretch the short SWIO pulses.
    noInterrupts();

    swioSendAddr(addr);

    // Read/write select bit.
    swioSendOne();

    // Send the word.
    for (i = 0; i < 32; i++) {
        if (val & 0x80000000)
            swioSendOne();
        else
            swioSendZero();

        val <<= 1;
    }

    // Stop bit.
    delayMicroseconds(10);

    interrupts();
}

// Read a register.
uint32_t swioReadReg(uint8_t addr) {
    char i;
    uint32_t x = 0;

    // UART RX and Timer0 interrupts must not stretch the short SWIO pulses.
    noInterrupts();

    swioSendAddr(addr);

    // Read/write select bit.
    swioSendZero();

    // Receive the word.
    for (i = 0; i < 32; i++) {
        x <<= 1;

        if (swioRecvBit())
            x |= 1;
    }

    // Stop bit.
    delayMicroseconds(10);

    interrupts();

    return x;
}

void swioInit() {
    SWIO_PORT |= _BV(SWIO_BIT);
    SWIO_DDR |= _BV(SWIO_BIT);
    delay(5);
    SWIO_PORT &= ~_BV(SWIO_BIT);
    delay(20);
    SWIO_PORT |= _BV(SWIO_BIT);
    SWIO_DDR &= ~_BV(SWIO_BIT);
}

void setup() {
    Serial.begin(115200);

    // Make the target reset pin an output.
    TARGET_POWER_DDR |= _BV(TARGET_POWER_BIT);

    swioInit();

    Serial.write(PROTOCOL_START);
}

void loop() {
    while (Serial.available()) {
        switch (Serial.read()) {
            case PROTOCOL_TEST:
                Serial.write(PROTOCOL_ACK);
                break;
            case PROTOCOL_POWER_ON:
                targetPower(1);
                Serial.write(PROTOCOL_ACK);
                break;
            case PROTOCOL_POWER_OFF:
                targetPower(0);
                Serial.write(PROTOCOL_ACK);
                break;
            case PROTOCOL_WRITE_REG: {
                const uint8_t reg = readByte();
                uint32_t val = 0;
                for (int i = 0; i < 4; i++)
                    val |= (uint32_t)readByte() << (8 * i);
                swioWriteReg(reg, val);
                Serial.write(PROTOCOL_ACK);
                break;
            }
            case PROTOCOL_READ_REG: {
                const uint8_t reg = readByte();
                const uint32_t val = swioReadReg(reg);
                for (int i = 0; i < 4; i++)
                    Serial.write((val >> (8 * i)) & 0xff);
                break;
            }
            default:
                break;
        }
    }
}
