import os
import tempfile
import time
import unittest
from pathlib import Path

from scs.collector import Collector
from scs.storage import Store
from tests.helpers import settings

try:
    import pty
except ImportError:
    pty = None


@unittest.skipUnless(pty is not None and os.name == "posix", "requires POSIX pseudoterminals")
class CollectorPtyTests(unittest.TestCase):
    """Real pyserial over private PTYs; never opens any hardware device."""

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.store = Store(Path(self.tmp.name) / "telemetry.sqlite3")
        self.addCleanup(self.store.close)
        self.master, self.slave = pty.openpty()
        self.addCleanup(os.close, self.slave)
        self.addCleanup(self.close_master)
        config = settings()
        config["port"] = os.ttyname(self.slave)
        self.store.save_settings(config)
        self.collector = Collector(self.store)
        self.collector.start()
        self.addCleanup(self.stop_collector)
        self.wait_for(lambda: self.collector.status["connected"], "initial PTY connection")

    def close_master(self):
        if self.master is not None:
            os.close(self.master)
            self.master = None

    def stop_collector(self):
        self.collector.stop()
        self.assertFalse(self.collector.thread.is_alive(), "collector failed to stop in bounded time")

    def wait_for(self, predicate, description, timeout=4):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            if predicate():
                return
            time.sleep(0.01)
        self.fail(f"Timed out waiting for {description}; status={self.collector.status!r}")

    def has_event(self, kind):
        return any(event["kind"] == kind for event in self.store.events(1000))

    def test_fragmented_lines_and_multiple_lines_per_read_commit_without_browser(self):
        os.write(self.master, b"BE")
        os.write(self.master, b"AT\nBEGIN\n3 7E 0.")
        os.write(self.master, b"117\n3 7F 0.125\n2 ERR TO\nEND\n")
        self.wait_for(lambda: len(self.store.records(float("inf"))) == 3, "complete group")
        records = self.store.records(float("inf"))
        self.assertEqual([r["raw"] for r in records], ["3 7E 0.117", "3 7F 0.125", "2 ERR TO"])
        self.assertEqual(len({r["time"] for r in records}), 1)
        self.assertIsNotNone(self.collector.status["last_beat"])
        self.assertTrue(self.has_event("beat"))
        self.assertTrue(self.has_event("sensor_error"))

    def test_non_ascii_and_overlong_lines_abort_then_recover(self):
        os.write(self.master, b"BEGIN\n1 00 12.000\n\xff\nEND\n")
        self.wait_for(lambda: self.has_event("incomplete_group"), "non-ASCII abort")
        self.assertEqual(self.store.records(float("inf")), [])
        os.write(self.master, b"BEGIN\n" + b"x" * 513 + b"\nEND\n")
        self.wait_for(lambda: any("Line too long" in e["detail"]
                                  for e in self.store.events()), "long-line rejection")
        os.write(self.master, b"BEGIN\n1 00 13.000\nEND\n")
        self.wait_for(lambda: len(self.store.records(float("inf"))) == 1, "recovery group")
        self.assertEqual(self.store.records(float("inf"))[0]["value_milli"], 13000)

    def test_disconnect_discards_partial_group_and_port_change_reconnects(self):
        os.write(self.master, b"BEGIN\n1 00 12.000\n1 ERR NR\n")
        self.wait_for(lambda: self.has_event("sensor_error"), "partial group consumed")
        self.close_master()
        self.wait_for(lambda: self.has_event("serial_disconnected"), "PTY disconnection")
        self.assertTrue(self.has_event("incomplete_group"))
        self.assertEqual(self.store.records(float("inf")), [])
        master, slave = pty.openpty()
        self.addCleanup(os.close, master)
        self.addCleanup(os.close, slave)
        self.addCleanup(self.collector.stop)  # Stop before closing replacement PTY.
        config = self.store.settings()
        config["port"] = os.ttyname(slave)
        self.store.save_settings(config)
        self.wait_for(lambda: self.collector.status["connected"]
                      and self.collector.status["port"] == config["port"], "replacement PTY connection")
        os.write(master, b"BEGIN\n1 01 10.000\nEND\n")
        self.wait_for(lambda: len(self.store.records(float("inf"))) == 1, "post-reconnect group")
        self.assertEqual(self.store.records(float("inf"))[0]["value_milli"], 10000)
        self.assertIsNone(self.collector.status["error"])
