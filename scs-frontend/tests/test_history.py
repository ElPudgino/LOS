import json
import sqlite3
import tempfile
import threading
import unittest
from pathlib import Path
from unittest.mock import patch

from fastapi.testclient import TestClient

from scs.app import create_app
from scs.protocol import Parser
from tests.helpers import error, measurement, settings


class HistoryDeletionTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.path = Path(self.tmp.name) / "history.sqlite3"
        forbidden = patch("scs.app.Collector.start", side_effect=AssertionError("real serial forbidden"))
        forbidden.start()
        self.addCleanup(forbidden.stop)
        self.app = create_app(self.path, collect=False)
        self.client = TestClient(self.app)
        self.client.__enter__()
        self.addCleanup(self.client.__exit__, None, None, None)
        self.store = self.app.state.store
        self.config = settings(timeout=1000)
        self.config["channels"][0]["name"] = "Сохранить название"
        self.config["port"] = "/test/no-hardware"
        self.store.save_settings(self.config)
        for seq, (timestamp, value) in enumerate([(100, 12), (200, 8), (300, 10)]):
            self.store.save_group(timestamp, [measurement(timestamp, value, sensor=1, seq=seq),
                                              measurement(timestamp, value, sensor=2, seq=seq),
                                              error(timestamp, "NR", sensor=3)])
            self.store.event(timestamp, "sensor_error", "NR", 3)
            self.store.event(timestamp, "beat", "BEAT")

    def delete(self, body):
        return self.client.request("DELETE", "/api/history", json=body)

    def snapshot(self):
        return self.store.records(float("inf")), self.store.events(1000), self.store.settings()

    def test_period_is_half_open_and_deletes_whole_groups_and_events(self):
        response = self.delete(dict(scope="period", start=200, end=300, confirm=True))
        self.assertEqual(response.status_code, 200)
        self.assertEqual(response.json(), {"deleted": {"groups": 1, "records": 3, "events": 2}})
        remaining = self.store.records(float("inf"))
        self.assertEqual([r["time"] for r in remaining], [100] * 3 + [300] * 3)
        self.assertEqual([r["position"] for r in remaining], [0, 1, 2] * 2)
        self.assertEqual([e["time"] for e in self.store.events()], [300, 300, 100, 100])
        self.assertEqual(self.store.settings(), self.config)
        self.assertEqual(self.store.db.execute("PRAGMA foreign_key_check").fetchall(), [])

    def test_fractional_boundaries_apply_to_events_own_time(self):
        self.store.event(199.999, "test", "before")
        self.store.event(299.999, "test", "inside")
        self.store.event(300, "test", "end")
        response = self.delete(dict(scope="period", start=199.999, end=300, confirm=True))
        self.assertEqual(response.json()["deleted"], {"groups": 1, "records": 3, "events": 4})
        self.assertIn("end", [e["detail"] for e in self.store.events()])
        self.assertNotIn("inside", [e["detail"] for e in self.store.events()])

    def test_all_deletes_history_but_preserves_settings_and_live_connection_status(self):
        self.app.state.collector.status.update(connected=True, last_beat=301)
        response = self.delete(dict(scope="all", confirm=True))
        self.assertEqual(response.status_code, 200)
        self.assertEqual(response.json(), {"deleted": {"groups": 3, "records": 9, "events": 6}})
        self.assertEqual(self.store.records(float("inf")), [])
        self.assertEqual(self.store.events(), [])
        self.assertEqual(self.store.latest_sensor_errors(), {})
        self.assertEqual(self.client.get("/api/settings").json(), self.config)
        current = self.client.get("/api/current").json()
        self.assertEqual([c["value"] for c in current["channels"]], [None] * 3)
        self.assertEqual([c["error"] for c in current["channels"]], [None] * 3)
        self.assertEqual([c["state"] for c in current["channels"]], ["unknown"] * 3)
        self.assertTrue(current["connection"]["connected"])
        self.assertEqual(current["connection"]["last_beat"], 301)
        analysis = self.client.get("/api/analysis", params={"start": 0, "end": 400}).json()
        self.assertEqual(analysis["channels"][0]["values"]["all"]["count"], 0)
        self.assertEqual(self.delete(dict(scope="all", confirm=True)).json()["deleted"],
                         {"groups": 0, "records": 0, "events": 0})

    def test_current_and_analysis_recompute_from_remaining_history(self):
        with patch("scs.app.time.time", return_value=301):
            self.assertEqual(self.client.get("/api/current").json()["channels"][0]["state"], "no_flow")
            self.delete(dict(scope="period", start=200, end=300, confirm=True))
            current = self.client.get("/api/current").json()
        self.assertEqual(current["channels"][0]["state"], "flow")
        self.assertEqual(current["channels"][0]["last_valid"], 300)
        result = self.client.get("/api/analysis", params={"start": 0, "end": 400}).json()
        channel = result["channels"][0]
        self.assertEqual([m["time"] for m in channel["measurements"]], [100, 300])
        self.assertEqual(channel["values"]["all"]["count"], 2)
        self.assertEqual(channel["durations"]["no_flow"]["total"], 0)

    def test_empty_period_changes_nothing(self):
        before = self.snapshot()
        response = self.delete(dict(scope="period", start=400, end=500, confirm=True))
        self.assertEqual(response.json()["deleted"], {"groups": 0, "records": 0, "events": 0})
        self.assertEqual(self.snapshot(), before)

    def test_invalid_or_unconfirmed_requests_leave_history_unchanged(self):
        before = self.snapshot()
        invalid = [None, {}, {"scope": "all"}, {"scope": "all", "confirm": False},
                   {"scope": "all", "confirm": "true"}, {"scope": "all", "confirm": 1},
                   {"scope": "all", "confirm": True, "start": 0},
                   {"scope": "all", "confirm": True, "end": 100},
                   {"scope": "period", "confirm": True},
                   {"scope": "period", "confirm": True, "start": 0},
                   {"scope": "period", "confirm": True, "end": 100},
                   {"scope": "period", "confirm": True, "start": 100, "end": 100},
                   {"scope": "period", "confirm": True, "start": 200, "end": 100},
                   {"scope": "period", "confirm": True, "start": "bad", "end": 100},
                   {"scope": "unknown", "confirm": True},
                   {"scope": "all", "confirm": True, "unexpected": 1}]
        for body in invalid:
            with self.subTest(body=body):
                self.assertEqual(self.delete(body).status_code, 422)
                self.assertEqual(self.snapshot(), before)
        for field in ("start", "end"):
            for value in ("NaN", "Infinity", "-Infinity", "1e999"):
                body = dict(scope="period", confirm=True, start=0, end=100)
                body[field] = "NONFINITE"
                raw = json.dumps(body).replace('"NONFINITE"', value)
                with self.subTest(field=field, value=value):
                    r = self.client.request("DELETE", "/api/history", content=raw,
                                            headers={"Content-Type": "application/json"})
                    self.assertEqual(r.status_code, 422)
                    self.assertEqual(self.snapshot(), before)

    def test_failure_rolls_back_all_deletions(self):
        before = self.snapshot()
        self.store.db.execute("""CREATE TRIGGER reject_delete BEFORE DELETE ON events
                                BEGIN SELECT RAISE(ABORT, 'test failure'); END""")
        with self.assertRaises(sqlite3.IntegrityError):
            self.store.delete_history(200, 300)
        self.assertEqual(self.snapshot(), before)
        client = TestClient(self.app, raise_server_exceptions=False)
        try:
            response = client.request("DELETE", "/api/history", json=dict(scope="all", confirm=True))
            self.assertEqual(response.status_code, 500)
            self.assertEqual(self.snapshot(), before)
        finally:
            client.close()
        self.store.db.execute("DROP TRIGGER reject_delete")

    def test_storage_rejects_invalid_bounds_without_deletion(self):
        before = self.snapshot()
        for bounds in ((1, None), (None, 2), (2, 1), (1, 1), (float("nan"), 2), (1, float("inf"))):
            with self.subTest(bounds=bounds), self.assertRaises(ValueError):
                self.store.delete_history(*bounds)
            self.assertEqual(self.snapshot(), before)

    def test_new_groups_continue_after_deletion_and_survive_restart(self):
        self.delete(dict(scope="all", confirm=True))
        parser = Parser(self.store)
        for line in ("BEAT", "BEGIN", "1 03 10.000", "END"):
            parser.feed(line, 400)
        with patch("scs.app.time.time", return_value=401):
            current = self.client.get("/api/current").json()
        self.assertEqual(current["channels"][0]["value"], 10)
        self.assertEqual(current["channels"][0]["state"], "unknown")
        self.assertFalse(any(e["kind"] == "seq_discontinuity" for e in self.store.events()))
        with TestClient(create_app(self.path, collect=False)) as restarted:
            self.assertEqual(restarted.get("/api/settings").json(), self.config)
            data = restarted.get("/api/analysis", params={"start": 0, "end": 500}).json()
            self.assertEqual([m["time"] for m in data["channels"][0]["measurements"]], [400])

    def test_collector_write_waits_for_deletion_lock_and_is_not_lost(self):
        entered = threading.Event()

        def write_group():
            entered.set()
            self.store.save_group(400, [measurement(400, 13, seq=3)])

        with self.store.lock:
            writer = threading.Thread(target=write_group)
            writer.start()
            self.assertTrue(entered.wait(2))
            self.store.delete_history()
        writer.join(timeout=2)
        self.assertFalse(writer.is_alive())
        self.assertEqual([r["time"] for r in self.store.records(float("inf"))], [400])
