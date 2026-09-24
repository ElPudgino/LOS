// UART -> CC1101 transmitter.
// Every byte received from UART is sent as one CRC-protected radio packet.
// Pins: CS=10, MOSI=11, MISO=12, SCK=13, GDO0=2, VCC=3.3V.
#include <SmartRC_CC1101.h>

#define FREQ_MHZ 433.92
#define GDO0_PIN 2
#define MAX_PACKET_BYTES 32

SmartRC_CC1101 radio;

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

  // Do not start TX with an empty FIFO.
  radio.setSidle();
  radio.SpiStrobe(0x3B);            // SFTX
}

void loop() {
  if (Serial.available() <= 0) return;

  // Передаём доступные сейчас UART-байты одним пакетом. Это сохраняет порядок
  // символов и не теряет их на ожиданиях GDO0 между отдельными байтами.
  byte packet[MAX_PACKET_BYTES];
  byte length = 0;
  while (Serial.available() > 0 && length < sizeof(packet)) {
    packet[length++] = (byte)Serial.read();
  }
  radio.SendData(packet, length);
}
