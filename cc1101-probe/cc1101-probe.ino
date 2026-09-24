// CC1101 probe v8 - per datasheet SBAS216.
// 6-bit address space 0x00-0x3F. Header = R/W | B | A5-A0.
// Read sequence: send header (chip returns status byte, discard),
//                send dummy   (chip returns register data).
//   Config regs 0x00-0x2E: read header = 0x80|a
//   Status regs 0x30-0x3D: read header = 0xC0|a  (B=1 selects status)
//   SRES command strobe    : write byte 0x30
// PARTNUM reset = 0x00 (table 29.3), VERSION = 0x04 on this old revision.
// Dumps all modem config registers at power-on reset (after SRES).
// CS=10, MOSI=11, MISO=12, SCK=13.
#include <SPI.h>
#define CC_CS 10

void f(uint8_t b) { SPI.transfer(b); }
void sel(int v) { digitalWrite(CC_CS, v); }
// read using a full header byte; discards the status byte, returns data byte
uint8_t rdhdr(uint8_t hdr) {
  sel(LOW); SPI.transfer(hdr); uint8_t v = SPI.transfer(0); sel(HIGH); return v;
}
uint8_t rd_cfg(uint8_t a) { return rdhdr(0x80 | a); }
uint8_t rd_st (uint8_t a) { return rdhdr(0xC0 | a); }
void sres() { sel(LOW); f(0x30); sel(HIGH); delay(5); }
void ph(uint8_t v) { Serial.print(v >> 4, HEX); Serial.print(v & 15, HEX); }

void setup() {
  Serial.begin(9600);
  pinMode(CC_CS, OUTPUT); sel(HIGH); SPI.begin(); delay(300);

  sres();

  // status registers (B=1)
  Serial.print(F("PARTNUM=")); ph(rd_st(0x30));
  Serial.print(F(" VERSION=")); ph(rd_st(0x31));
  Serial.print(F(" MARCSTATE=")); ph(rd_st(0x35));
  Serial.println();
  bool ok = (rd_st(0x30) == 0x00 && rd_st(0x31) == 0x04);
  Serial.println(ok ? F("=> CC1101 OK") : F("=> ID MISMATCH"));

  // config registers at power-on reset: 0x04..0x2E
  Serial.print(F("CFG "));
  for (uint8_t a = 0x04; a <= 0x2E; a++) {
    ph(rd_cfg(a));
    Serial.print(' ');
  }
  Serial.println();

  Serial.println(F("=== DONE ==="));
  while (1) delay(1000);
}
void loop() {}
