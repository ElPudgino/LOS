#include "burst.h"
#include "radio.h"

Burst burst;
bool radioReady = false;
uint32_t lastRadioAttempt = 0;
uint32_t lastHeartbeat = 0;

void setup() {
  Serial.begin(115200);
  lastHeartbeat = millis();

  radioReady = Radio::begin();
  lastRadioAttempt = millis();
}

void loop() {
  // Expire the previous burst before accepting a new packet at the boundary.
  const uint32_t nowHeartbeat = millis();
  burst.flushIfExpired(nowHeartbeat, Serial);
  if (uint32_t(nowHeartbeat - lastHeartbeat) >= 1000UL) {
    lastHeartbeat = nowHeartbeat;
    Serial.print(F("BEAT\n"));
  }

  if (!radioReady) {
    if (uint32_t(millis() - lastRadioAttempt) >= 1000UL) {
      radioReady = Radio::begin();
      lastRadioAttempt = millis();
    }
    return;
  }

  uint8_t packet[Radio::PAYLOAD_SIZE];
  uint8_t length;
  if (!Radio::poll(packet, length)) {
    radioReady = false;
    lastRadioAttempt = millis();
    return;
  }

  const uint32_t now = millis();
  burst.flushIfExpired(now, Serial);
  if (length) burst.append(packet, length, now);
}
