// CC1101 link test - receiver.
// Listens for 4-byte packets from cc1101-link-tx:
//   [0xA5, seq, 0x5A, seq^0x5A]
// Prints every valid packet with RSSI/LQI; prints a line if nothing
// received for 5 s.
// Wiring: CS=10, MOSI=11, MISO=12, SCK=13, GDO0=2, VCC=3.3V.
#include <SmartRC_CC1101.h>

// Должно совпадать с cc1101-link-tx
#define FREQ_MHZ 433.92

SmartRC_CC1101 radio;

byte rxbuf[32];
unsigned total = 0, bad = 0;
uint32_t last_rx;

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
  radio.SetRx();
  last_rx = millis();
  Serial.println(F("RX ready"));
}

void hex(byte v) { Serial.print(v >> 4, HEX); Serial.print(v & 15, HEX); }

void dbg() {
  // FREQEST: signed, LSB = F_OSC/2^16 ~= 397 Hz
  int16_t fe = (int8_t)radio.SpiReadStatus(0x32);
  Serial.print(F("DBG freqest="));
  Serial.print((long)fe * 397 / 1000);
  Serial.print(F("k marc="));
  byte m = radio.SpiReadStatus(0x35);
  hex(m);
  Serial.print(F(" rxfifo="));
  hex(radio.SpiReadStatus(0x3B));
  Serial.print(F(" rssi="));
  Serial.print(radio.getRssi());
  Serial.print(F(" pkt="));
  hex(radio.SpiReadStatus(0x38));  // PKTSTATUS: SFD/PQT/CCA
  Serial.print(F(" gdo0="));
  Serial.print(digitalRead(2));
  Serial.println();
}

void loop() {
  // ReceiveData() при пустом FIFO делает SFRX+SRX. Не вызывать его постоянно:
  // это сбрасывает приём между preamble и sync. Ждём конец пакета по GDO0.
  static bool packet_in_progress = false;
  byte n = 0;
  if (digitalRead(2)) {
    packet_in_progress = true;
  } else if (packet_in_progress) {
    packet_in_progress = false;
    n = radio.ReceiveData(rxbuf);
  }
  if (n > 0) {
    last_rx = millis();
    bool ok = (n >= 4 && rxbuf[0] == 0xA5 && rxbuf[2] == 0x5A &&
               rxbuf[3] == (byte)(rxbuf[1] ^ 0x5A));
    if (ok) {
      total++;
      Serial.print(F("RX #"));
      Serial.print(rxbuf[1]);
      Serial.print(F(" rssi="));
      Serial.print(radio.getRssi());
      Serial.print(F(" dBm lqi="));
      Serial.println(radio.getLqi());
    } else {
      bad++;
      Serial.print(F("RX bad packet ("));
      Serial.print(n);
      Serial.println(F(" bytes)"));
    }
  }
  if (millis() - last_rx > 5000) {
    last_rx = millis();
    Serial.print(F("RX silence, total="));
    Serial.print(total);
    Serial.print(F(" bad="));
    Serial.println(bad);
  }
  static uint32_t last_dbg = 0;
  if (millis() - last_dbg > 500) {
    last_dbg = millis();
    dbg();
  }
}
