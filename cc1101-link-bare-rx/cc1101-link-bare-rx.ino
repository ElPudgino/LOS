// Minimal CC1101 packet receiver. No SmartRC_CC1101 library is used.
// Arduino Nano wiring: CS=10, MOSI=11, MISO=12, SCK=13, GDO0=2.
// CC1101 is a 3.3 V part: level-shift Nano outputs CS/MOSI/SCK to 3.3 V.
// This fixed profile must match cc1101-link-bare-tx exactly.
#include <SPI.h>

const byte CS_PIN = 10;
const byte GDO0_PIN = 2;
const byte SRES = 0x30, SRX = 0x34, SIDLE = 0x36, SFRX = 0x3A;
const byte RXFIFO = 0x3F;

bool selectChip() {
  digitalWrite(CS_PIN, LOW);
  unsigned long start = millis();
  while (digitalRead(MISO)) {
    if (millis() - start > 100) {
      digitalWrite(CS_PIN, HIGH);
      return false;
    }
  }
  return true;
}

void writeReg(byte address, byte value) {
  if (!selectChip()) return;
  SPI.transfer(address); SPI.transfer(value);
  digitalWrite(CS_PIN, HIGH);
}

void strobe(byte command) {
  if (!selectChip()) return;
  SPI.transfer(command);
  digitalWrite(CS_PIN, HIGH);
}

byte readStatus(byte address) {
  // Bit 7=read; bit 6 selects burst/status space. RXBYTES is 0x3B -> 0xFB.
  if (!selectChip()) return 0;
  SPI.transfer(0xC0 | address);
  byte value = SPI.transfer(0);
  digitalWrite(CS_PIN, HIGH);
  return value;
}

byte readRxFifoByte() {
  if (!selectChip()) return 0;
  SPI.transfer(0xBF);               // RXFIFO single-read header
  byte value = SPI.transfer(0);
  digitalWrite(CS_PIN, HIGH);
  return value;
}

void readRxFifo(byte *data, byte length) {
  if (!selectChip()) return;
  SPI.transfer(0xFF);               // RXFIFO burst-read header
  for (byte i = 0; i < length; ++i) data[i] = SPI.transfer(0);
  digitalWrite(CS_PIN, HIGH);
}

void resetChip() {
  digitalWrite(CS_PIN, LOW);  delayMicroseconds(10);
  digitalWrite(CS_PIN, HIGH); delayMicroseconds(40);
  if (!selectChip()) return;
  SPI.transfer(SRES);
  while (digitalRead(MISO)) {}      // reset complete when MISO becomes low
  digitalWrite(CS_PIN, HIGH);
  delay(1);
}

// The explicit register set is the shared 433.92 MHz / 2-FSK / 4.8-kBaud
// profile. It avoids relying on reset values or SmartRC_CC1101 defaults.
const byte CONFIG[][2] = {
  {0x02,0x06}, {0x03,0x07},         // GDO0: sync->EOP; FIFO threshold
  {0x04,0xD3}, {0x05,0x91},         // 16-bit sync D391
  {0x06,0x00}, {0x07,0x04}, {0x08,0x05}, // variable FIFO, RSSI/LQI, CRC
  {0x0A,0x00}, {0x0B,0x06}, {0x0C,0x00},
  {0x0D,0x10}, {0x0E,0xB0}, {0x0F,0x71}, // FREQ=0x10B071 -> 433.92 MHz
  {0x10,0x87}, {0x11,0x83},         // 4.8 kBaud
  {0x12,0x02},                      // 2-FSK, no Manchester, 16-bit sync
  {0x13,0x02}, {0x14,0xF8}, {0x15,0x47}, // preamble/spacing/deviation
  {0x17,0x30}, {0x18,0x18},         // state machine + automatic calibration
  {0x19,0x16}, {0x1A,0x1C},         // frequency-offset and bit-sync loops
  {0x1B,0xC7}, {0x1C,0x00}, {0x1D,0xB2}, // AGC
  {0x21,0x56}, {0x22,0x10},
  {0x23,0xE9}, {0x24,0x2A}, {0x25,0x00}, {0x26,0x1F}, // PLL calibration
  {0x29,0x59}, {0x2C,0x81}, {0x2D,0x35}, {0x2E,0x09}
};

void configureRadio() {
  for (byte i = 0; i < sizeof(CONFIG) / sizeof(CONFIG[0]); ++i)
    writeReg(CONFIG[i][0], CONFIG[i][1]);
}

void enterRx() {
  // SFRX removes stale bytes/overflow. It is valid in IDLE or RX.
  strobe(SIDLE);
  strobe(SFRX);
  strobe(SRX);
}

void setup() {
  Serial.begin(9600);
  pinMode(CS_PIN, OUTPUT); digitalWrite(CS_PIN, HIGH);
  pinMode(GDO0_PIN, INPUT);
  SPI.begin();
  SPI.beginTransaction(SPISettings(4000000, MSBFIRST, SPI_MODE0));
  resetChip();
  configureRadio();
  enterRx();
  Serial.println(F("bare RX ready"));
}

void loop() {
  static bool packetInProgress = false;
  static byte packet[61];           // 64 FIFO bytes minus length/RSSI/LQI

  // GDO0 is HIGH only while a packet is being received. Do not clear/read FIFO
  // while it is HIGH: doing so would interrupt preamble/sync/CRC processing.
  if (digitalRead(GDO0_PIN)) {
    packetInProgress = true;
    return;
  }
  if (!packetInProgress) return;
  packetInProgress = false;

  byte available = readStatus(0x3B); // RXBYTES; bit7 signals FIFO overflow
  if (available & 0x80) {
    Serial.println(F("RX FIFO overflow"));
    enterRx();
    return;
  }

  byte length = readRxFifoByte();   // variable-length packet prefix
  if (length != 4) {
    Serial.println(F("unexpected length"));
    enterRx();
    return;
  }

  readRxFifo(packet, length);
  byte rssi = readRxFifoByte();     // APPEND_STATUS adds RSSI and LQI/CRC_OK
  byte lqi = readRxFifoByte();
  if (!(lqi & 0x80)) {
    Serial.println(F("bad CRC"));
  } else if (packet[0] == 0xA5 && packet[2] == 0x5A &&
             packet[3] == (byte)(packet[1] ^ 0x5A)) {
    Serial.print(F("RX #")); Serial.print(packet[1]);
    Serial.print(F(" rssi_raw=0x")); Serial.print(rssi, HEX);
    Serial.print(F(" lqi=")); Serial.println(lqi & 0x7F);
  } else {
    Serial.println(F("bad payload"));
  }

  // MCSM1 returns to IDLE after a packet; re-enter RX for the next one.
  strobe(SRX);
}
