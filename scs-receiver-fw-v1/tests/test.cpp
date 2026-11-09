#include <assert.h>
#include <iostream>
#include <string>
#include "../scs-receiver/burst.h"

std::string record(uint8_t sensor, uint8_t status = 0, uint8_t seq = 0,
                   uint32_t value = 0) {
  const uint8_t bytes[] = {uint8_t((status << 4) | sensor), seq,
    uint8_t(value >> 24), uint8_t(value >> 16), uint8_t(value >> 8), uint8_t(value)};
  return std::string(reinterpret_cast<const char *>(bytes), sizeof(bytes));
}

void packet(Burst &burst, const std::string &data, uint32_t now) {
  assert(data.size() <= 32);
  burst.append(reinterpret_cast<const uint8_t *>(data.data()), data.size(), now);
}

void append(Burst &burst, const std::string &data, uint32_t now) {
  assert(data.size() % 6 == 0);
  for (size_t offset = 0; offset < data.size(); offset += 30)
    packet(burst, data.substr(offset, 30), now);
}

int main() {
  static_assert(BURST_BUFFER_SIZE == 594, "Test the default capacity");
  Burst burst;
  MockSerial out;

  burst.flushIfExpired(10000, out);
  burst.append(nullptr, 0, 0);
  burst.flushIfExpired(10000, out);

  assert(out.data.empty());

  // All sensors, boundary values and SEQ padding/case, across physical packets.
  packet(burst, record(1, 0, 0, 0) + record(2, 0, 0x0a, 1) +
                record(3, 0, 0xff, 7), 100);
  packet(burst, record(3, 0, 0xff, 999) + record(2, 0, 0x0a, 1000) +
                record(1, 0, 0, UINT32_MAX), 1099);
  burst.flushIfExpired(2098, out);

  assert(out.data.empty());

  burst.flushIfExpired(2099, out);

  const std::string measurements = "BEGIN\n1 00 0.000\n2 0A 0.001\n3 FF 0.007\n"
    "3 FF 0.999\n2 0A 1.000\n1 00 4294967.295\nEND\n";
  assert(out.data == measurements);

  burst.flushIfExpired(3000, out);

  assert(out.data == measurements);
  out.data.clear();

  // Every status for every sensor; reserved fields deliberately nonzero.
  const char *errors[] = {"", "NR", "TO", "CR", "OF"};
  std::string records, expected = "BEGIN\n";
  for (uint8_t sensor = 1; sensor <= 3; ++sensor) {
    for (uint8_t status = 1; status <= 4; ++status) {
      records += record(sensor, status, 0xff, UINT32_MAX);
      expected += std::to_string(sensor) + " ERR " + errors[status] + "\n";
    }
  }

  append(burst, records, 3000);
  burst.flushIfExpired(4000, out);

  assert(out.data == expected + "END\n");
  out.data.clear();

  // Every allowed RF payload length, preserving order.
  for (unsigned count = 1; count <= 5; ++count) {
    std::string payload, text;
    for (unsigned i = 0; i < count; ++i) {
      payload += record(1, 0, i, i);
      text += "1 0" + std::to_string(i) + " 0.00" + std::to_string(i) + "\n";
    }

    packet(burst, payload, 0);
    burst.flushIfExpired(1000, out);

    assert(out.data == "BEGIN\n" + text + "END\n");
    out.data.clear();
  }

  // Atomic rejection: no new group, no data or timeout change in active group.
  std::string invalid[] = {
    std::string(1, '\1'), std::string(2, '\1'), std::string(3, '\1'),
    std::string(4, '\1'), std::string(5, '\1'), std::string(7, '\1'),
    std::string(31, '\1'), std::string(32, '\1'),
    record(0), record(4), record(15), record(1, 5), record(1, 15),
    record(1) + record(2, 5), record(0) + record(3),
    record(1) + record(2) + record(3) + record(1) + record(15)
  };

  for (const std::string &bad : invalid) {
    packet(burst, bad, 100);
    burst.flushIfExpired(1100, out);

    assert(out.data.empty());

    packet(burst, record(3, 0, 0x0a, 125), 2000);
    packet(burst, bad, 2999);
    burst.append(nullptr, 0, 2999);
    burst.flushIfExpired(3000, out);

    assert(out.data == "BEGIN\n3 0A 0.125\nEND\n");
    out.data.clear();
  }

  // Matched TX maximum: three sensors, 32 measurements and one TO each.
  records.clear();
  expected = "BEGIN\n";
  for (uint8_t sensor = 1; sensor <= 3; ++sensor) {
    for (unsigned i = 0; i < 32; ++i) {
      records += record(sensor, 0, 0xff, UINT32_MAX);
      expected += std::to_string(sensor) + " FF 4294967.295\n";
    }

    records += record(sensor, 2);
    expected += std::to_string(sensor) + " ERR TO\n";
  }

  assert(records.size() == 594);

  append(burst, records, 4000);
  burst.flushIfExpired(5000, out);

  assert(out.data == expected + "END\n");
  out.data.clear();

  append(burst, records, 7000);
  packet(burst, record(1), 7001); // The 100th record overflows.
  packet(burst, record(2), 7900); // Still extends the overflow timeout.
  burst.flushIfExpired(8001, out);

  assert(out.data.empty());

  packet(burst, record(3), 8800);
  packet(burst, record(1) + record(0), 9799); // Invalid does not extend it.
  burst.flushIfExpired(9799, out);

  assert(out.data.empty());

  burst.flushIfExpired(9800, out);

  assert(out.data == "BEGIN\nERR BO\nEND\n");

  burst.flushIfExpired(9999, out);

  assert(out.data == "BEGIN\nERR BO\nEND\n");

  packet(burst, record(1, 1), 10000);
  burst.flushIfExpired(11000, out);

  assert(out.data == "BEGIN\nERR BO\nEND\nBEGIN\n1 ERR NR\nEND\n");
  out.data.clear();

  // Unsigned elapsed time across millis() wraparound.
  packet(burst, record(2), UINT32_MAX - 499);
  burst.flushIfExpired(499, out);

  assert(out.data.empty());

  burst.flushIfExpired(500, out);

  assert(out.data == "BEGIN\n2 00 0.000\nEND\n");
  out.data.clear();

  // The main loop expires before appending at the exact boundary.
  packet(burst, record(1, 1), 5000);
  burst.flushIfExpired(6000, out);
  packet(burst, record(2, 2), 6000);
  burst.flushIfExpired(6999, out);

  assert(out.data == "BEGIN\n1 ERR NR\nEND\n");

  burst.flushIfExpired(7000, out);

  assert(out.data == "BEGIN\n1 ERR NR\nEND\nBEGIN\n2 ERR TO\nEND\n");

  std::cout << "burst tests passed\n";
}
