import tempfile
import unittest
from pathlib import Path

from scs.protocol import Parser
from scs.storage import Store
from tests.helpers import settings


class ProtocolTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.store = Store(Path(self.tmp.name) / "telemetry.sqlite3")
        self.addCleanup(self.store.close)
        self.parser = Parser(self.store)

    def group(self, lines, timestamp=100):
        self.parser.feed("BEGIN", timestamp - 1)
        for line in lines:
            self.parser.feed(line, timestamp - 0.5)
        self.parser.feed("END", timestamp)

    def kinds(self):
        return [event["kind"] for event in self.store.events(1000)]

    def test_commit_only_at_end_preserves_order_raw_seq_and_timestamp(self):
        self.parser.feed("BEGIN", 10)
        self.parser.feed("3 7E 0.117", 11)
        self.parser.feed("3 7F 0.125", 11)
        self.parser.feed("2 42 0.080", 11)
        self.assertEqual(self.store.records(100), [])
        self.parser.feed("END", 12)
        records = self.store.records(12)
        self.assertEqual([r["sensor"] for r in records], [3, 3, 2])
        self.assertEqual([r["seq"] for r in records], [126, 127, 66])
        self.assertEqual([r["value_milli"] for r in records], [117, 125, 80])
        self.assertEqual([r["raw"] for r in records], ["3 7E 0.117", "3 7F 0.125", "2 42 0.080"])
        self.assertEqual([r["position"] for r in records], [0, 1, 2])
        self.assertEqual([r["time"] for r in records], [12, 12, 12])
        self.assertEqual(len({r["group_id"] for r in records}), 1)

    def test_uint32_extremes_and_zero_are_measurements(self):
        self.group(["1 00 0.000", "1 01 4294967.295"])
        self.assertEqual([r["value_milli"] for r in self.store.records(100)], [0, 0xFFFFFFFF])

    def test_sensor_errors_are_immediate_and_not_zero_measurements(self):
        self.parser.feed("BEGIN", 10)
        for index, code in enumerate(("NR", "TO", "CR", "OF")):
            self.parser.feed(f"1 ERR {code}", 11 + index / 10)
            self.assertEqual(self.store.events()[0]["detail"], code)
            self.assertEqual(self.store.events()[0]["sensor"], 1)
        self.assertEqual(self.store.records(100), [])
        self.parser.feed("END", 12)
        records = self.store.records(100)
        self.assertEqual([r["error"] for r in records], ["NR", "TO", "CR", "OF"])
        self.assertTrue(all(r["value_milli"] is None and r["seq"] is None for r in records))
        self.assertEqual(self.store.latest_sensor_errors()[1]["detail"], "OF")

    def test_bo_diagnostic_group_and_recovery(self):
        self.group(["ERR BO"])
        record, = self.store.records(100)
        self.assertIsNone(record["sensor"])
        self.assertIsNone(record["value_milli"])
        self.assertEqual(record["error"], "BO")
        self.assertIn("receiver_overflow", self.kinds())
        self.group(["2 00 12.000"], 101)
        self.assertEqual(len(self.store.records(101)), 2)

    def test_bo_cannot_commit_partial_measurements(self):
        self.group(["1 00 12.000", "ERR BO"])
        self.assertEqual(self.store.records(100), [])
        self.assertIn("receiver_overflow", self.kinds())
        self.assertIn("invalid_group", self.kinds())
        self.group(["1 01 13.000"], 101)
        self.assertEqual(len(self.store.records(101)), 1)

    def test_heartbeat_is_not_telemetry(self):
        self.parser.feed("BEAT", 1)
        self.assertEqual(self.kinds(), ["beat"])
        self.assertEqual(self.store.records(100), [])

    def test_invalid_lines_atomically_discard_group(self):
        invalid = ["1 0a 1.000", "1 A 1.000", "4 00 1.000", "1 00 -1.000",
                   "1 00 1.00", "1 00 1.0000", "1 00 4294967.296", "1 ERR XX",
                   " 1 00 1.000", "1 00 1.000 ", "BEAT", "", "1 00 1.000\r"]
        for line in invalid:
            with self.subTest(line=line):
                self.group(["1 00 12.000", line])
                self.assertEqual(self.store.records(100), [])
                self.assertEqual(self.kinds()[:2], ["invalid_group", "protocol_error"])

    def test_outside_group_and_empty_group_are_not_committed(self):
        for line in ("END", "1 00 12.000", "1 ERR TO", "ERR BO", "nonsense"):
            self.parser.feed(line, 1)
        self.group([])
        self.assertEqual(self.store.records(100), [])
        self.assertEqual(self.kinds().count("protocol_error"), 5)
        self.assertIn("invalid_group", self.kinds())
        self.assertIn("sensor_error", self.kinds())
        self.assertIn("receiver_overflow", self.kinds())

    def test_incomplete_group_expires_at_five_seconds(self):
        self.parser.feed("BEGIN", 10)
        self.parser.feed("1 00 12.000", 11)
        self.parser.expire(14.999)
        self.assertIsNotNone(self.parser.pending)
        self.parser.expire(15)
        event = self.store.events()[0]
        self.assertEqual(event["kind"], "incomplete_group")
        self.assertIn("1 00 12.000", event["detail"])
        self.assertIn("5", event["detail"])
        self.assertIsNone(self.parser.pending)
        self.parser.feed("END", 16)
        self.assertEqual(self.store.records(100), [])
        self.assertIn("incomplete_group", self.kinds())

    def test_repeated_begin_and_abort_discard_partial_data(self):
        self.parser.feed("BEGIN", 1)
        self.parser.feed("1 00 12.000", 1)
        self.parser.feed("BEGIN", 2)
        self.parser.feed("2 00 13.000", 2)
        self.parser.feed("END", 3)
        self.assertEqual([r["sensor"] for r in self.store.records(100)], [2])
        self.assertIn("incomplete_group", self.kinds())
        self.parser.feed("BEGIN", 4)
        self.parser.feed("1 01 14.000", 4)
        self.parser.abort(4, "disconnect")
        self.parser.feed("END", 4)
        self.assertEqual(len(self.store.records(100)), 1)

    def test_group_capacity_99_and_reject_100(self):
        self.group([f"1 {i:02X} 1.000" for i in range(99)])
        self.assertEqual(len(self.store.records(100)), 99)
        self.group([f"2 {i:02X} 1.000" for i in range(100)], 101)
        self.assertEqual(len(self.store.records(101)), 99)
        self.assertIn("invalid_group", self.kinds())

    def test_seq_wrap_is_contiguous_gap_duplicate_and_restart_are_diagnostic(self):
        self.group(["1 FE 12.000", "2 30 12.000", "1 FF 12.000", "1 00 12.000"])
        self.assertNotIn("seq_discontinuity", self.kinds())
        self.group(["1 ERR TO", "1 03 12.000", "2 31 12.000"], 101)
        event, = [e for e in self.store.events() if e["kind"] == "seq_discontinuity"]
        self.assertEqual(event["sensor"], 1)
        self.assertIn("00 -> 03", event["detail"])
        self.assertIn("2", event["detail"])
        self.group(["1 03 12.000", "1 00 12.000"], 102)
        self.assertEqual(self.kinds().count("seq_discontinuity"), 3)
        self.assertEqual(len(self.store.records(102)), 9)  # No synthetic gap filling.


class PersistenceTests(unittest.TestCase):
    def test_reopen_preserves_settings_raw_history_events_and_seq_baseline(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "nested" / "telemetry.sqlite3"
            store = Store(path)
            try:
                config = settings()
                config["channels"][0]["name"] = "Насос"
                store.save_settings(config)
                parser = Parser(store)
                for line in ("BEGIN", "1 FF 0.007", "END"):
                    parser.feed(line, 10)
                store.event(11, "test", "persist me", 2)
            finally:
                store.close()
            store = Store(path)
            try:
                self.assertEqual(store.settings(), config)
                self.assertEqual(store.records(9), [])
                record, = store.records(10)
                self.assertEqual((record["raw"], record["seq"], record["value_milli"]), ("1 FF 0.007", 255, 7))
                self.assertEqual(store.events(1)[0]["detail"], "persist me")
                parser = Parser(store)
                for line in ("BEGIN", "1 00 0.008", "END"):
                    parser.feed(line, 12)
                self.assertNotIn("seq_discontinuity", [e["kind"] for e in store.events()])
                self.assertEqual(len(store.records(12)), 2)
            finally:
                store.close()

    def test_settings_return_is_detached_and_events_newest_first(self):
        store = Store(":memory:")
        self.addCleanup(store.close)
        first = store.settings()
        first["channels"][0]["name"] = "not saved"
        self.assertNotEqual(store.settings(), first)
        for i in range(3):
            store.event(i, "test", str(i))
        self.assertEqual([e["detail"] for e in store.events(2)], ["2", "1"])
