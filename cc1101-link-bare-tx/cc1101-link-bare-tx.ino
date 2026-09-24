// Minimal CC1101 packet transmitter. No SmartRC_CC1101 library is used.
// Arduino Nano wiring: CS=10, MOSI=11, MISO=12, SCK=13, GDO0=2.
// CC1101 is a 3.3 V part: level-shift Nano outputs CS/MOSI/SCK to 3.3 V.
//
// Fixed profile, shared with cc1101-link-bare-rx:
// 433.92 MHz, 2-FSK, 4.8 kBaud, 16-bit sync D391, CRC, variable FIFO packets.
#include <SPI.h>

const byte CS_PIN = 10;
const byte GDO0_PIN = 2;
const byte TXFIFO = 0x3F;
const byte SRES = 0x30, STX = 0x35, SIDLE = 0x36, SFTX = 0x3B;

// Wait for MISO=LOW after CS. CC1101 uses HIGH to signal that it is busy.
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
  SPI.transfer(address);       // write, single register
  SPI.transfer(value);
  digitalWrite(CS_PIN, HIGH);
}

void strobe(byte command) {
  if (!selectChip()) return;
  SPI.transfer(command);
  digitalWrite(CS_PIN, HIGH);
}

void resetChip() {
  // Required reset sequence from the datasheet before SRES.
  digitalWrite(CS_PIN, LOW);  delayMicroseconds(10);
  digitalWrite(CS_PIN, HIGH); delayMicroseconds(40);
  if (!selectChip()) return;
  SPI.transfer(SRES);
  while (digitalRead(MISO)) {}  // SRES has completed when MISO goes low.
  digitalWrite(CS_PIN, HIGH);
  delay(1);
}

// Values below are the complete, fixed profile needed by this exact link.
// Keeping them here, rather than inheriting library reset values, makes TX/RX
// deterministic and prevents the library's asynchronous-serial PKTCTRL0 mode.
const byte CONFIG[][2] = {
  {0x02, 0x06},                   // IOCFG0: GDO0 high from sync through EOP.
  {0x03, 0x07},                   // FIFOTHR.
  {0x04, 0xD3}, {0x05, 0x91},     // SYNC1/SYNC0 = D391.
  {0x06, 0x00},                   // PKTLEN: first FIFO byte is payload length.
  {0x07, 0x04},                   // PKTCTRL1: append RSSI/LQI, no address check.
  {0x08, 0x05},                   // PKTCTRL0: FIFO packets, CRC, variable length.
  {0x0A, 0x00}, {0x0B, 0x06}, {0x0C, 0x00}, // channel/IF/no manual offset.
  {0x0D, 0x10}, {0x0E, 0xB0}, {0x0F, 0x71}, // 433.92 MHz, 26 MHz crystal.
  {0x10, 0x87}, {0x11, 0x83},     // MDMCFG4/3: 4.8 kBaud.
  {0x12, 0x02},                   // MDMCFG2: 2-FSK, no Manchester, 16-bit sync.
  {0x13, 0x02}, {0x14, 0xF8},     // preamble/channel-spacing fields.
  {0x15, 0x47},                   // DEVIATN.
  {0x17, 0x30}, {0x18, 0x18},     // IDLE after packet; auto-calibrate on RX/TX.
  {0x19, 0x16}, {0x1A, 0x1C},     // frequency-offset and bit-sync loops.
  {0x1B, 0xC7}, {0x1C, 0x00}, {0x1D, 0xB2}, // AGC.
  {0x21, 0x56}, {0x22, 0x10},     // front-end settings.
  {0x23, 0xE9}, {0x24, 0x2A}, {0x25, 0x00}, {0x26, 0x1F}, // synth calibration.
  {0x29, 0x59}, {0x2C, 0x81}, {0x2D, 0x35}, {0x2E, 0x09}  // test settings.
};

void configureRadio() {
  for (byte i = 0; i < sizeof(CONFIG) / sizeof(CONFIG[0]); ++i)
    writeReg(CONFIG[i][0], CONFIG[i][1]);

  // In FSK, FREND0 selects PATABLE index 0. 0xC0 is the +10 dBm 433-MHz entry.
  if (selectChip()) {
    SPI.transfer(0x7E);            // PATABLE write + burst header.
    SPI.transfer(0xC0);            // PATABLE[0].
    digitalWrite(CS_PIN, HIGH);
  }
}

bool waitGdo(bool wanted) {
  unsigned long start = millis();
  while (digitalRead(GDO0_PIN) != wanted)
    if (millis() - start > 100) return false;
  return true;
}

bool sendPacket(const byte *payload, byte length) {
  // Always recover from an earlier incomplete TX before loading a new packet.
  strobe(SIDLE);
  strobe(SFTX);

  writeReg(TXFIFO, length);        // variable-length FIFO: length prefix
  if (!selectChip()) return false;
  SPI.transfer(0x7F);              // TXFIFO write + burst header
  for (byte i = 0; i < length; ++i) SPI.transfer(payload[i]);
  digitalWrite(CS_PIN, HIGH);

  strobe(STX);                     // hardware adds preamble, sync and CRC
  bool complete = waitGdo(HIGH) && waitGdo(LOW);
  strobe(SFTX);
  return complete;
}

void setup() {
  Serial.begin(9600);
  pinMode(CS_PIN, OUTPUT); digitalWrite(CS_PIN, HIGH);
  pinMode(GDO0_PIN, INPUT);
  SPI.begin();
  SPI.beginTransaction(SPISettings(4000000, MSBFIRST, SPI_MODE0));
  resetChip();
  configureRadio();
  strobe(SIDLE); strobe(SFTX);     // never issue STX with an empty FIFO
  Serial.println(F("bare TX ready"));
}

void loop() {
  static byte sequence = 0;
  byte packet[] = {0xA5, sequence, 0x5A, (byte)(sequence ^ 0x5A)};
  if (sendPacket(packet, sizeof(packet))) {
    Serial.print(F("TX #")); Serial.println(sequence);
  } else {
    Serial.println(F("TX timeout"));
  }
  ++sequence;
  delay(1000);
}
