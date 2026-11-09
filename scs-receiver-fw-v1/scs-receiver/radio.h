#pragma once

#include <Arduino.h>
#include <SPI.h>

// CC1101, 26 MHz crystal: same on-air profile as scs-transmitter.
namespace Radio {
const uint8_t CS_PIN = 10, GDO0_PIN = 2, PAYLOAD_SIZE = 32;

const uint8_t SIDLE = 0x36, SFRX = 0x3a, SRX = 0x34;

inline bool ready() {
  const uint32_t start = millis();
  while (digitalRead(MISO)) {
    if (uint32_t(millis() - start) >= 100UL) return false;
  }

  return true;
}

inline bool select() {
  digitalWrite(CS_PIN, LOW);
  if (ready()) return true;

  digitalWrite(CS_PIN, HIGH);
  return false;
}

inline bool strobe(uint8_t command) {
  if (!select()) return false;

  SPI.transfer(command);
  digitalWrite(CS_PIN, HIGH);
  return true;
}

inline bool writeReg(uint8_t address, uint8_t value) {
  if (!select()) return false;

  SPI.transfer(address);
  SPI.transfer(value);
  digitalWrite(CS_PIN, HIGH);
  return true;
}

inline bool readReg(uint8_t address, uint8_t &value) {
  if (!select()) return false;

  SPI.transfer(address | 0x80); // Single-byte configuration read.
  value = SPI.transfer(0);
  digitalWrite(CS_PIN, HIGH);
  return true;
}

inline bool readStatus(uint8_t address, uint8_t &value) {
  if (!select()) return false;

  SPI.transfer(address | 0xc0); // Status registers require burst-read bit.
  value = SPI.transfer(0);
  digitalWrite(CS_PIN, HIGH);
  return true;
}

inline bool restart() {
  // SFRX is only legal in IDLE or RX overflow.
  return strobe(SIDLE) && strobe(SFRX) && strobe(SRX);
}

inline bool begin() {
  digitalWrite(CS_PIN, HIGH);
  pinMode(CS_PIN, OUTPUT);
  pinMode(GDO0_PIN, INPUT);

  // TI SWRS061I, section 19.1.2: GPIO levels must be established before
  // toggling CSn. Disable SPI even on retries so it cannot override SCLK/SI.
  SPI.endTransaction();
  SPI.end();
  digitalWrite(SCK, HIGH);
  pinMode(SCK, OUTPUT);
  digitalWrite(MOSI, LOW);
  pinMode(MOSI, OUTPUT);
  pinMode(MISO, INPUT);

  digitalWrite(CS_PIN, LOW);
  delayMicroseconds(10);
  digitalWrite(CS_PIN, HIGH);
  delayMicroseconds(40);
  if (!select()) return false;

  // CSn stays low while SPI takes over the pins and issues SRES.
  SPI.begin();
  SPI.beginTransaction(SPISettings(4000000, MSBFIRST, SPI_MODE0));
  SPI.transfer(0x30); // SRES
  const bool reset = ready();
  digitalWrite(CS_PIN, HIGH);
  if (!reset) return false;
  delay(1);

  static const uint8_t config[][2] = {
    {0x02, 0x06}, {0x03, 0x07}, {0x04, 0xd3}, {0x05, 0x91},
    {0x06, PAYLOAD_SIZE}, {0x07, 0x04}, {0x08, 0x05},
    {0x0a, 0x00}, {0x0b, 0x06}, {0x0c, 0x00},
    {0x0d, 0x10}, {0x0e, 0xb0}, {0x0f, 0x71},
    {0x10, 0x87}, {0x11, 0x83}, {0x12, 0x02},
    {0x13, 0x02}, {0x14, 0xf8}, {0x15, 0x47},
    // RXOFF_MODE=IDLE: FIFO is stable after a completed packet.
    {0x17, 0x30}, {0x18, 0x18}, {0x19, 0x16}, {0x1a, 0x1c},
    {0x1b, 0xc7}, {0x1c, 0x00}, {0x1d, 0xb2},
    {0x21, 0x56}, {0x22, 0x10}, {0x23, 0xe9}, {0x24, 0x2a},
    {0x25, 0x00}, {0x26, 0x1f}, {0x29, 0x59},
    {0x2c, 0x81}, {0x2d, 0x35}, {0x2e, 0x09}
  };

  for (uint8_t i = 0; i < sizeof(config) / sizeof(config[0]); ++i) {
    if (!writeReg(config[i][0], config[i][1])) return false;
  }

  // MISO low alone is not proof that SPI writes reached the radio.
  uint8_t value;
  for (uint8_t i = 0; i < sizeof(config) / sizeof(config[0]); ++i) {
    if (!readReg(config[i][0], value) || value != config[i][1]) return false;
  }

  return restart();
}

// Returns false only on an SPI readiness failure; length=0 means no valid packet.
inline bool poll(uint8_t *data, uint8_t &length) {
  static bool receiving = false;
  static uint32_t started = 0;

  length = 0;
  if (digitalRead(GDO0_PIN)) {
    if (!receiving) {
      receiving = true;
      started = millis();
    }
    if (uint32_t(millis() - started) < 200UL) return true;

    receiving = false;
    return restart(); // Stuck/incomplete packet, bounded recovery.
  }

  receiving = false;

  uint8_t count;
  if (!readStatus(0x3b, count)) return false; // RXBYTES
  if (count & 0x80) return restart();
  if (!count) return true;

  // A packet can start between the first GDO check and RXBYTES read.
  if (digitalRead(GDO0_PIN)) return true;
  if (!strobe(SIDLE)) return false;

  // Read again after IDLE, so a status-register transition cannot cause
  // us to read past the FIFO or mistake a partial packet for a full one.
  if (!readStatus(0x3b, count)) return false;
  if ((count & 0x80) || count < 4 || count > PAYLOAD_SIZE + 3) return restart();

  if (!select()) return false;

  SPI.transfer(0xff); // RXFIFO burst
  const uint8_t size = SPI.transfer(0);
  if (!size || size > PAYLOAD_SIZE || count != size + 3) {
    digitalWrite(CS_PIN, HIGH);
    return restart();
  }

  for (uint8_t i = 0; i < size; ++i) data[i] = SPI.transfer(0);
  SPI.transfer(0); // RSSI
  const uint8_t lqi = SPI.transfer(0);
  digitalWrite(CS_PIN, HIGH);

  if (!restart()) return false;
  if (lqi & 0x80) length = size; // CRC_OK, not merely LQI.

  return true;
}

} // namespace Radio
