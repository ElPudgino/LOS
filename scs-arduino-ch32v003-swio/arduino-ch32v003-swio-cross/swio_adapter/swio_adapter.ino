#include <Arduino.h>
#include <util/atomic.h>
#include "swio.h"

static const uint8_t TARGET_POWER_PIN = 9;
static const unsigned long UART_BAUD = 115200;

// Preserve the original blocking protocol: wait for every argument byte.
static uint8_t readByte() {
    while (Serial.available() == 0) {
    }
    return static_cast<uint8_t>(Serial.read());
}

void setup() {
    digitalWrite(TARGET_POWER_PIN, LOW);
    pinMode(TARGET_POWER_PIN, OUTPUT);
    Serial.begin(UART_BAUD);
    swio_init();
    Serial.write('!');
}

void loop() {
    if (Serial.available() == 0) {
        return;
    }

    switch (Serial.read()) {
        case '?':
            Serial.write('+');
            break;
        case 'p':
            digitalWrite(TARGET_POWER_PIN, HIGH);
            Serial.write('+');
            break;
        case 'P':
            digitalWrite(TARGET_POWER_PIN, LOW);
            Serial.write('+');
            break;
        case 'w': {
            const uint8_t addr = readByte();
            uint32_t value = 0;
            for (uint8_t i = 0; i < 4; ++i) {
                value |= static_cast<uint32_t>(readByte()) << (8 * i);
            }
            // Timer0 and UART interrupts must not stretch SWIO pulses.
            ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
                swio_write_reg(addr, value);
            }
            Serial.write('+');
            break;
        }
        case 'r': {
            const uint8_t addr = readByte();
            uint32_t value;
            ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
                value = swio_read_reg(addr);
            }
            for (uint8_t i = 0; i < 4; ++i) {
                Serial.write(static_cast<uint8_t>(value >> (8 * i)));
            }
            break;
        }
    }
}
