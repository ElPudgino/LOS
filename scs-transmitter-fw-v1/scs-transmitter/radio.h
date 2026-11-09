#pragma once

#include <Arduino.h>
#include <SPI.h>

// On-air profile: 26 MHz crystal, 433.92 MHz, 2-FSK,
// 4.8 kbaud, sync D391, CRC.
namespace Radio {
const uint8_t CS_PIN = 10;
const uint8_t GDO0_PIN = 2;
const uint8_t PAYLOAD_SIZE = 32;

const uint8_t SIDLE = 0x36, SFTX = 0x3b;

inline bool ready() {
  const unsigned long start = millis();
  while (digitalRead(MISO)) {
    if (millis() - start >= 100UL) return false;
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

  SPI.transfer(address | 0x80); // Single-byte read (configuration or PATABLE).
  value = SPI.transfer(0);
  digitalWrite(CS_PIN, HIGH);
  return true;
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
    {0x06, 0x3d}, {0x07, 0x04}, {0x08, 0x05},
    {0x0a, 0x00}, {0x0b, 0x06}, {0x0c, 0x00},
    {0x0d, 0x10}, {0x0e, 0xb0}, {0x0f, 0x71},
    {0x10, 0x87}, {0x11, 0x83}, {0x12, 0x02},
    {0x13, 0x02}, {0x14, 0xf8}, {0x15, 0x47},
    {0x17, 0x30}, {0x18, 0x18}, {0x19, 0x16}, {0x1a, 0x1c},
    {0x1b, 0xc7}, {0x1c, 0x00}, {0x1d, 0xb2},
    {0x21, 0x56}, {0x22, 0x10}, {0x23, 0xe9}, {0x24, 0x2a},
    {0x25, 0x00}, {0x26, 0x1f}, {0x29, 0x59},
    {0x2c, 0x81}, {0x2d, 0x35}, {0x2e, 0x09}
  };

  for (uint8_t i = 0; i < sizeof(config) / sizeof(config[0]); ++i) {
    if (!writeReg(config[i][0], config[i][1])) return false;
  }
  if (!writeReg(0x3e, 0xc0)) return false; // PATABLE[0], +10 dBm

  // MISO low alone is not proof that SPI writes reached the radio.
  uint8_t value;
  for (uint8_t i = 0; i < sizeof(config) / sizeof(config[0]); ++i) {
    if (!readReg(config[i][0], value) || value != config[i][1]) return false;
  }
  if (!readReg(0x3e, value) || value != 0xc0) return false;

  return strobe(SIDLE) && strobe(SFTX);
}

inline bool waitGdo(uint8_t level) {
  const unsigned long start = millis();
  while (digitalRead(GDO0_PIN) != level) {
    if (millis() - start >= 200UL) return false;
  }

  return true;
}

inline bool send(const uint8_t *data, uint8_t length) {
  if (!length || length > PAYLOAD_SIZE) return false;
  if (!strobe(SIDLE) || !strobe(SFTX)) return false;
  if (!select()) return false;

  SPI.transfer(0x7f); // TXFIFO burst: length followed by payload
  SPI.transfer(length);
  for (uint8_t i = 0; i < length; ++i) SPI.transfer(data[i]);
  digitalWrite(CS_PIN, HIGH);

  const bool sent = strobe(0x35) && waitGdo(HIGH) && waitGdo(LOW);

  // Force IDLE before flushing, including when GDO0 never went low.
  const bool idle = strobe(SIDLE);
  const bool flushed = strobe(SFTX);

  return sent && idle && flushed;
}

} // namespace Radio
