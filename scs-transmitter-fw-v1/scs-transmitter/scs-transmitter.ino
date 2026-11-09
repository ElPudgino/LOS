#include <Arduino.h>
#include <string.h>

#include "config.h"
#include "radio.h"

// Physical poll order: D4, D5, D6; sensor numbers are binary, not ASCII.
static const uint8_t SELECT_PINS[] = {4, 5, 6};
static const uint8_t SENSOR_NUMBERS[] = {3, 2, 1};

enum Status : uint8_t {
  OK = 0, NO_RESPONSE = 1, TIMEOUT = 2, PROTOCOL_ERROR = 3, OVERFLOW = 4
};

struct Channel {
  uint8_t data[CHANNEL_CAPACITY][RECORD_SIZE];
  uint8_t count;
  Status status;
};

static Channel channels[3];
static bool radioReady;

static uint8_t crc8(const uint8_t *data, uint8_t length) {
  uint8_t crc = 0;
  while (length--) {
    crc ^= *data++;
    for (uint8_t bit = 0; bit < 8; ++bit)
      crc = (crc & 0x80) ? (crc << 1) ^ 0x07 : crc << 1;
  }

  return crc;
}

static void discardAvailable() {
  // Snapshot: even a continuous stream must not keep us here forever.
  int count = Serial.available();
  while (count-- > 0) Serial.read();
}

static void drainLine() {
  const unsigned long start = millis();
  unsigned long last = start;

  while (millis() - start < DRAIN_LIMIT_MS) {
    if (Serial.available()) {
      Serial.read();
      last = millis();
    } else if (millis() - last >= FRAME_GAP_MS) {
      return;
    }
  }
}

static void receiveChannel(Channel &ch) {
  ch.count = 0;
  uint8_t record[RECORD_SIZE];
  uint8_t length = 0;
  bool received = false;
  const unsigned long start = millis();
  unsigned long last = start;

  for (;;) {
    // The overall deadline wins if both deadline and frame gap have elapsed.
    if (millis() - start >= TIMEOUT_MS) {
      ch.status = !received ? NO_RESPONSE : length ? PROTOCOL_ERROR : TIMEOUT;
      break;
    }

    if (Serial.available()) {
      const uint8_t value = Serial.read();
      received = true;
      last = millis();

      if (ch.count == CHANNEL_CAPACITY) {
        ch.status = OVERFLOW; // Byte 193, without evicting any old record.
        break;
      }

      record[length++] = value;
      if (length == RECORD_SIZE) {
        if (crc8(record, RECORD_SIZE - 1) != record[RECORD_SIZE - 1]) {
          ch.status = PROTOCOL_ERROR;
          break;
        }

        memcpy(ch.data[ch.count++], record, RECORD_SIZE);
        length = 0;
      }
    } else if (received && millis() - last >= FRAME_GAP_MS) {
      ch.status = length ? PROTOCOL_ERROR : OK;
      break;
    }
  }

  if (ch.status == PROTOCOL_ERROR || ch.status == OVERFLOW) ch.count = 0;

  // First terminal error wins; draining never reclassifies the response.
  if (received && ch.status != OK) drainLine();
}

// Whole six-byte radio records; at most five per physical packet.
static uint8_t packet[Radio::PAYLOAD_SIZE];
static uint8_t packetCount;
static bool transmissionOk;

static void flushPacket() {
  if (!packetCount) return;

  if (transmissionOk && !Radio::send(packet, packetCount)) {
    transmissionOk = false; // No retries/duplicates without an ACK protocol.
    radioReady = false;
  }

  packetCount = 0;
}

static void emitRecord(Status status, uint8_t number, const uint8_t *record) {
  if (!transmissionOk) return;
  if (packetCount + RECORD_SIZE > Radio::PAYLOAD_SIZE) flushPacket();
  if (!transmissionOk) return;

  packet[packetCount++] = (static_cast<uint8_t>(status) << 4) | number;
  // Input bytes 0..4 already contain seq and the big-endian value.
  for (uint8_t i = 0; i < RECORD_SIZE - 1; ++i)
    packet[packetCount++] = record ? record[i] : 0;
}

static void sendChannel(const Channel &ch, uint8_t number) {
  for (uint8_t i = 0; i < ch.count && transmissionOk; ++i)
    emitRecord(OK, number, ch.data[i]);

  if (ch.status != OK) emitRecord(ch.status, number, nullptr);
}

void setup() {
  // Set the output latch first: never request data during pin setup.
  for (uint8_t i = 0; i < 3; ++i) {
    digitalWrite(SELECT_PINS[i], HIGH);
    pinMode(SELECT_PINS[i], OUTPUT);
  }

  Serial.begin(9600); // RX D0 only; no debug text on sensor UART.
  radioReady = Radio::begin();
}

void loop() {
  // Also provides the initial sensor boot pause. No catch-up polls.
  delay(POLL_INTERVAL_MS);

  for (uint8_t i = 0; i < 3; ++i) {
    discardAvailable();
    digitalWrite(SELECT_PINS[i], LOW);
    delay(PULSE_MS); // AVR RX interrupt buffers bytes during the pulse.
    digitalWrite(SELECT_PINS[i], HIGH);
    receiveChannel(channels[i]);
  }

  // Send only after all sensor responses have been collected.
  if (!radioReady) radioReady = Radio::begin();
  transmissionOk = radioReady;
  packetCount = 0;

  for (uint8_t i = 0; i < 3; ++i) sendChannel(channels[i], SENSOR_NUMBERS[i]);
  flushPacket();
}
