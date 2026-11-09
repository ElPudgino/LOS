import copy
import json
import io
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

from fastapi.testclient import TestClient

from scs.app import create_app, main
from scs.config import Settings, default_settings
from scs.protocol import Parser
from tests.helpers import settings


class ApiTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.database = Path(self.tmp.name) / "telemetry.sqlite3"
        # No test may accidentally open the real receiver, even with bad settings.
        self.start = patch("scs.app.Collector.start", side_effect=AssertionError("real collector forbidden"))
        self.start.start()
        self.addCleanup(self.start.stop)
        self.app = create_app(self.database, collect=False)
        self.client = TestClient(self.app)
        self.client.__enter__()
        self.addCleanup(self.client.__exit__, None, None, None)

    def group(self, lines, timestamp=100):
        parser = Parser(self.app.state.store)
        for line in ["BEGIN", *lines, "END"]:
            parser.feed(line, timestamp)

    def test_index_and_local_static_assets_are_served(self):
        response = self.client.get("/")
        self.assertEqual(response.status_code, 200)
        self.assertIn("text/html", response.headers["content-type"])
        self.assertIn("/static/app.js", response.text)
        self.assertIn("/static/style.css", response.text)
        for path, media_type in (("/static/app.js", "javascript"), ("/static/style.css", "text/css")):
            with self.subTest(path=path):
                response = self.client.get(path)
                self.assertEqual(response.status_code, 200)
                self.assertIn(media_type, response.headers["content-type"])
                self.assertEqual(response.headers["cache-control"], "no-cache")
                self.assertTrue(response.content)
                self.assertNotIn("<!doctype html", response.text.lower())

    def test_defaults_and_current_empty_data(self):
        response = self.client.get("/api/settings")
        self.assertEqual(response.status_code, 200)
        self.assertEqual(response.json(), default_settings())
        config = response.json()
        self.assertEqual(config["port"], "/dev/ttyACM0")
        self.assertFalse(config["network_access"])
        self.assertEqual([c["timeout"] for c in config["channels"]], [60] * 3)
        current = self.client.get("/api/current").json()
        self.assertFalse(current["connection"]["connected"])
        self.assertEqual([c["state"] for c in current["channels"]], ["unknown"] * 3)
        self.assertEqual([c["value"] for c in current["channels"]], [None] * 3)

    def test_valid_settings_roundtrip_and_event(self):
        config = settings(timeout=12.5)
        config.update(port="/test/not-a-real-device", network_access=True)
        config["channels"][0].update(name="Возврат", role="Назначение", threshold=-1.5, hysteresis=0)
        response = self.client.put("/api/settings", json=config)
        self.assertEqual(response.status_code, 200)
        self.assertEqual(response.json(), config)
        self.assertEqual(self.client.get("/api/settings").json(), config)
        self.assertEqual(self.client.get("/api/events").json()[0]["kind"], "settings_changed")

    def test_settings_validation_rejects_invalid_shapes_ranges_and_extras_atomically(self):
        baseline = self.client.get("/api/settings").json()
        invalid = []
        for field, value in (("channels", baseline["channels"][:2]),
                             ("channels", baseline["channels"] * 2),
                             ("modes", baseline["modes"][:1]), ("modes", baseline["modes"] * 2),
                             ("port", ""), ("unexpected", 1)):
            config = copy.deepcopy(baseline)
            config[field] = value
            invalid.append(config)
        for field, value in (("hysteresis", -0.1), ("timeout", 0), ("timeout", -1),
                             ("name", ""), ("name", "x" * 101), ("role", "x" * 201),
                             ("threshold", "invalid"), ("extra", 0)):
            config = copy.deepcopy(baseline)
            config["channels"][0][field] = value
            invalid.append(config)
        for conditions in (["flow"] * 2, ["flow"] * 4, ["on", "ignore", "ignore"]):
            config = copy.deepcopy(baseline)
            config["modes"][0]["conditions"] = conditions
            invalid.append(config)
        config = copy.deepcopy(baseline)
        config["modes"][0]["name"] = ""
        invalid.append(config)
        for config in invalid:
            with self.subTest(config=config):
                self.assertEqual(self.client.put("/api/settings", json=config).status_code, 422)
                self.assertEqual(self.client.get("/api/settings").json(), baseline)
        self.assertEqual(self.client.get("/api/events").json(), [])

    def test_nonfinite_settings_rejected(self):
        # Overflowing exponents are valid JSON numbers, not just NaN extensions.
        client = TestClient(self.app, raise_server_exceptions=False)
        self.addCleanup(client.close)
        for field in ("threshold", "hysteresis", "timeout"):
            for token in ("1e999", "-1e999", "NaN"):
                config = settings()
                config["channels"][0][field] = "__NONFINITE__"
                payload = json.dumps(config).replace('"__NONFINITE__"', token)
                with self.subTest(field=field, token=token):
                    response = client.put("/api/settings", content=payload,
                                          headers={"content-type": "application/json"})
                    self.assertEqual(response.status_code, 422)

    def test_analysis_period_validation(self):
        for params in ({}, {"start": 1}, {"end": 2}, {"start": 2, "end": 2},
                       {"start": 3, "end": 2}, {"start": "nan", "end": 2},
                       {"start": 0, "end": "inf"}, {"start": "-inf", "end": 2},
                       {"start": 0, "end": "nan"}, {"start": "bad", "end": 2},
                       {"start": 0, "end": 2000}):
            with self.subTest(params=params), patch("scs.app.time.time", return_value=1000):
                self.assertEqual(self.client.get("/api/analysis", params=params).status_code, 422)
        response = self.client.get("/api/analysis", params={"start": 0, "end": 10})
        self.assertEqual(response.status_code, 200)
        self.assertTrue(response.json()["approximate"])

    def test_events_limit_validation_and_order(self):
        for i in range(3):
            self.app.state.store.event(i, "test", str(i))
        response = self.client.get("/api/events", params={"limit": 2})
        self.assertEqual(response.status_code, 200)
        self.assertEqual([e["detail"] for e in response.json()], ["2", "1"])
        for limit in (0, -1, 1001, "bad", 1.5):
            with self.subTest(limit=limit):
                self.assertEqual(self.client.get("/api/events", params={"limit": limit}).status_code, 422)

    def test_events_exclude_beats_before_applying_limit(self):
        store = self.app.state.store
        store.event(1, "serial_error", "test error")
        for timestamp in range(2, 105):
            store.event(timestamp, "beat", "BEAT")
        events = self.client.get("/api/events", params={"limit": 1}).json()
        self.assertEqual([event["kind"] for event in events], ["serial_error"])
        self.assertEqual(store.events(1)[0]["kind"], "beat")

    def test_current_immediate_incomplete_error_timeout_and_recovery(self):
        self.client.put("/api/settings", json=settings(timeout=5))
        self.group(["1 00 12.000"])
        parser = Parser(self.app.state.store)
        parser.feed("BEGIN", 101)
        parser.feed("1 ERR TO", 101)
        with patch("scs.app.time.time", return_value=102):
            current = self.client.get("/api/current").json()
        channel = current["channels"][0]
        self.assertEqual((channel["value"], channel["seq"], channel["last_valid"], channel["state"], channel["error"]),
                         (12, 0, 100, "flow", "TO"))
        self.assertEqual(current["mode"], "mode_1")
        self.assertTrue(current["channels"][0]["available"])
        with patch("scs.app.time.time", return_value=105):
            current = self.client.get("/api/current").json()
        self.assertEqual(current["channels"][0]["state"], "unknown")
        self.assertFalse(current["channels"][0]["available"])
        self.assertEqual(current["channels"][0]["value"], 12)
        self.assertEqual(current["mode"], "no_data")
        parser.abort(105, "disconnect")
        self.group(["1 01 10.000"], 106)
        with patch("scs.app.time.time", return_value=107):
            channel = self.client.get("/api/current").json()["channels"][0]
        self.assertEqual(channel["state"], "unknown")
        self.assertTrue(channel["available"])
        self.assertEqual(channel["value"], 10)
        self.assertIsNone(channel["error"])

    def test_api_recalculates_using_all_preperiod_history_and_current_settings(self):
        self.client.put("/api/settings", json=settings(timeout=30))
        for timestamp, line in ((0, "1 00 12.000"), (20, "1 01 10.000"),
                                (40, "1 02 10.000"), (60, "1 03 10.000")):
            self.group([line], timestamp)
        params = dict(start=50, end=70)
        first = self.client.get("/api/analysis", params=params).json()
        self.assertEqual(first["channels"][0]["durations"]["flow"]["total"], 20)
        self.assertEqual(first["channels"][0]["values"]["all"]["count"], 1)
        raw_before = self.app.state.store.records(70)
        config = settings(timeout=30)
        config["channels"][0]["threshold"] = 20
        self.client.put("/api/settings", json=config)
        second = self.client.get("/api/analysis", params=params).json()
        self.assertEqual(second["channels"][0]["durations"]["no_flow"]["total"], 20)
        self.assertEqual(self.app.state.store.records(70), raw_before)

    def test_server_is_local_by_default_and_network_access_is_explicit(self):
        for network, host in ((False, "127.0.0.1"), (True, "0.0.0.0")):
            config = settings()
            config["network_access"] = network
            self.app.state.store.save_settings(config)
            with self.subTest(network=network), patch("sys.argv", ["scs", "--database", str(self.database)]), \
                    patch("scs.app.uvicorn.run") as run:
                main()
                self.assertEqual(run.call_args.kwargs["host"], host)
                self.assertEqual(run.call_args.kwargs["port"], 8000)

    def test_server_rejects_invalid_http_ports_without_starting(self):
        for port in ("0", "65536", "-1"):
            with self.subTest(port=port), patch("sys.argv", ["scs", "--port", port]), \
                    patch("sys.stderr", new_callable=io.StringIO), patch("scs.app.uvicorn.run") as run:
                with self.assertRaises(SystemExit) as raised:
                    main()
                self.assertEqual(raised.exception.code, 2)
                run.assert_not_called()

    def test_application_restart_keeps_settings_history_and_diagnostics(self):
        config = settings()
        self.client.put("/api/settings", json=config)
        self.group(["1 FF 12.000", "2 ERR CR"])
        # An independent app/lifespan opens the existing SQLite file, never serial.
        with TestClient(create_app(self.database, collect=False)) as restarted:
            self.assertEqual(restarted.get("/api/settings").json(), config)
            result = restarted.get("/api/analysis", params={"start": 99, "end": 110}).json()
            self.assertEqual(result["channels"][0]["values"]["all"]["count"], 1)
            self.assertEqual(restarted.get("/api/events").json()[0]["kind"], "sensor_error")
            with patch("scs.app.time.time", return_value=101):
                self.assertEqual(restarted.get("/api/current").json()["channels"][1]["error"], "CR")


class ConfigTests(unittest.TestCase):
    def test_valid_defaults_and_each_default_is_independent(self):
        first = default_settings()
        self.assertEqual(Settings.model_validate(first).model_dump(), first)
        first["modes"][0]["conditions"][0] = "flow"
        first["channels"][0]["threshold"] = 99
        second = default_settings()
        self.assertEqual(second["modes"][0]["conditions"], ["ignore"] * 3)
        self.assertEqual(second["channels"][0]["threshold"], 10)
