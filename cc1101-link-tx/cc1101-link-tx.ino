// CC1101 link test - transmitter.
// Sends a 4-byte packet every second:
//   [0xA5, seq, 0x5A, seq^0x5A]
// Same pins and FREQ_MHZ as cc1101-link-rx.
// Wiring: CS=10, MOSI=11, MISO=12, SCK=13, GDO0=2, VCC=3.3V.
#include <SmartRC_CC1101.h>

// Подогнать под модули: 433.92 (433 МГц) или, например, 868.3
#define FREQ_MHZ     433.92
#define TX_PERIOD_MS 1000

SmartRC_CC1101 radio;

void setup() {
  Serial.begin(9600);
  radio.setSpiPin(13, 12, 11, 10);  // sck, miso, mosi, ss
  radio.setGDO0(2);
  radio.Init();
  if (!radio.getCC1101()) {
    Serial.println(F("ERROR: CC1101 not found"));
    while (1) delay(100);
  }
  radio.setMHZ(FREQ_MHZ);
  radio.setModulation(0);  // 0 = 2-FSK; иначе библиотека по умолчанию выберет ASK/OOK
  radio.setDRate(4.8);     // одинаковая явная скорость на обеих сторонах
  radio.SpiWriteReg(0x08, 0x05);  // normal FIFO packets, CRC, variable length
  radio.SpiWriteReg(0x02, 0x06);  // GDO0: HIGH от sync до конца пакета
  // Не вызывать SetTx(): он даёт STX при пустом FIFO и вызывает TX underflow.
  radio.setSidle();
  radio.SpiStrobe(0x3B);   // SFTX: очистить возможный флаг/содержимое TX FIFO
  Serial.println(F("TX ready"));
}

void loop() {
  static byte seq = 0;
  byte pkt[4];
  pkt[0] = 0xA5;         // magic head
  pkt[1] = seq;
  pkt[2] = 0x5A;         // tail marker
  pkt[3] = pkt[1] ^ 0x5A;

  radio.SendData(pkt, sizeof(pkt));
  Serial.print(F("TX #"));
  Serial.println(seq);
  seq++;
  delay(TX_PERIOD_MS);
}
