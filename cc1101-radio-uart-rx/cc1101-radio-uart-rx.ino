// CC1101 receiver -> UART.
// Writes every byte received in a valid CRC-protected radio packet to UART.
// Pins: CS=10, MOSI=11, MISO=12, SCK=13, GDO0=2, VCC=3.3V.
#include <SmartRC_CC1101.h>

#define FREQ_MHZ 433.92
#define GDO0_PIN 2

SmartRC_CC1101 radio;
byte rxbuf[61];  // CC1101 RX FIFO is 64 bytes; two are appended status bytes.

void setup() {
  Serial.begin(9600);

  radio.setSpiPin(13, 12, 11, 10);  // sck, miso, mosi, ss
  radio.setGDO0(GDO0_PIN);
  radio.Init();
  if (!radio.getCC1101()) {
    while (1) delay(100);
  }

  radio.setMHZ(FREQ_MHZ);
  radio.setModulation(0);           // 2-FSK
  radio.setDRate(4.8);              // 4.8 kBaud
  radio.SpiWriteReg(0x08, 0x05);    // normal FIFO packets, CRC, variable length
  radio.SpiWriteReg(0x02, 0x06);    // GDO0: sync -> end of packet
  radio.SpiWriteReg(0x07, 0x04);    // append RSSI/LQI status; same setting as link test
  radio.SetRx();
}

void loop() {
  // ReceiveData() resets RX when FIFO is empty. Only call it when GDO0 marks
  // a complete packet, otherwise a preamble/sync can be interrupted.
  static bool packetInProgress = false;

  if (digitalRead(GDO0_PIN)) {
    packetInProgress = true;
    return;
  }
  if (!packetInProgress) return;

  packetInProgress = false;
  byte length = radio.ReceiveData(rxbuf);
  if (length > 0) {
    Serial.write(rxbuf, length);
  }
}
