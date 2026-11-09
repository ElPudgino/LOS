import random
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

from fastapi.testclient import TestClient

from scs.app import create_app
from scs.thresholds import recommend_threshold
from tests.helpers import measurement, settings


class ThresholdAlgorithmTests(unittest.TestCase):
    def test_bimodal_distribution_gives_band_inside_gap(self):
        random.seed(7)
        values = [0.0] * 60 + [10 + random.uniform(-0.5, 0.5) for _ in range(60)]
        result = recommend_threshold(values)
        threshold = result["threshold"]
        self.assertIsNotNone(threshold)
        self.assertIsNone(result["reason"])
        # Both clusters stay outside the hysteresis band.
        self.assertGreater(threshold["turn_on"], 9)
        self.assertLess(threshold["turn_off"], 1)
        self.assertEqual(threshold["turn_on"], round(threshold["t"] + threshold["h"] / 2, 3))
        self.assertEqual(threshold["turn_off"], round(threshold["t"] - threshold["h"] / 2, 3))
        self.assertEqual(len(result["bins"]), 50)
        self.assertEqual(sum(bin["count"] for bin in result["bins"]), len(values))

    def test_narrow_interior_cluster_is_detected(self):
        values = [1.0] * 60 + [10.0] * 60
        result = recommend_threshold(values)
        threshold = result["threshold"]
        self.assertIsNotNone(threshold, result["reason"])
        self.assertGreater(threshold["turn_off"], 1)
        self.assertLess(threshold["turn_on"], 10)

    def test_single_narrow_interior_cluster_has_no_recommendation(self):
        result = recommend_threshold([1.0] * 120 + [10.0])
        self.assertIsNone(result["threshold"])

    def test_zero_rest_cluster_still_turns_off(self):
        # "Нет потока" is exactly 0: turn_off must stay strictly above it.
        random.seed(3)
        values = [0.0] * 40 + [5 + random.uniform(-0.2, 0.2) for _ in range(80)]
        threshold = recommend_threshold(values)["threshold"]
        self.assertIsNotNone(threshold)
        self.assertGreater(threshold["turn_off"], 0)
        self.assertLess(threshold["turn_on"], 5)

    def test_insufficient_samples(self):
        result = recommend_threshold([0.0] * 19)
        self.assertIsNone(result["threshold"])
        self.assertIn("Мало измерений", result["reason"])
        self.assertIsNone(result["bins"])

    def test_unimodal_distribution_has_no_recommendation(self):
        random.seed(1)
        values = [10 + random.uniform(-1, 1) for _ in range(100)]
        result = recommend_threshold(values)
        self.assertIsNone(result["threshold"])
        self.assertIsNotNone(result["bins"])
        self.assertEqual(result["count"], 100)

    def test_all_zero_values(self):
        result = recommend_threshold([0.0] * 40)
        self.assertIsNone(result["threshold"])
        self.assertEqual(result["reason"], "Все значения не больше нуля.")


class ThresholdApiTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.database = Path(self.tmp.name) / "telemetry.sqlite3"
        self.start = patch("scs.app.Collector.start", side_effect=AssertionError("real collector forbidden"))
        self.start.start()
        self.addCleanup(self.start.stop)
        self.app = create_app(self.database, collect=False)
        self.client = TestClient(self.app)
        self.client.__enter__()
        self.addCleanup(self.client.__exit__, None, None, None)
        self.app.state.store.save_settings(settings())

    def test_period_validation_matches_analysis(self):
        for params in ({}, {"start": 1}, {"end": 2}, {"start": 2, "end": 2},
                       {"start": 3, "end": 2}, {"start": "nan", "end": 2},
                       {"start": 0, "end": "inf"}, {"start": 0, "end": "bad"}):
            with self.subTest(params=params), patch("scs.app.time.time", return_value=1000):
                self.assertEqual(self.client.get("/api/thresholds", params=params).status_code, 422)

    def test_empty_period_returns_no_recommendation(self):
        response = self.client.get("/api/thresholds", params={"start": 0, "end": 100})
        self.assertEqual(response.status_code, 200)
        data = response.json()
        self.assertEqual((data["start"], data["end"]), (0, 100))
        self.assertEqual([channel["id"] for channel in data["channels"]], [1, 2, 3])
        self.assertEqual([channel["count"] for channel in data["channels"]], [0, 0, 0])
        self.assertEqual([channel["threshold"] for channel in data["channels"]], [None, None, None])
        self.assertEqual([channel["bins"] for channel in data["channels"]], [None, None, None])

    def test_bimodal_history_is_recommended_and_period_filtered(self):
        random.seed(11)
        store = self.app.state.store
        for timestamp in range(100, 160):
            records = []
            for sensor in (1, 2, 3):
                value = 0.0 if (timestamp + sensor) % 3 else 10 * sensor + random.uniform(-0.5, 0.5)
                records.append(measurement(timestamp, value, sensor=sensor, seq=(timestamp + sensor) % 256))
            store.save_group(timestamp, records)
        data = self.client.get("/api/thresholds", params={"start": 100, "end": 160}).json()
        channels = {channel["id"]: channel for channel in data["channels"]}
        for sensor in (1, 2, 3):
            with self.subTest(sensor=sensor):
                self.assertEqual(channels[sensor]["count"], 60)
                threshold = channels[sensor]["threshold"]
                self.assertIsNotNone(threshold, channels[sensor]["reason"])
                self.assertGreater(threshold["turn_on"], channels[sensor]["median"] * 0.8)
                self.assertLess(threshold["turn_off"], channels[sensor]["max"] * 0.05)
        # Out-of-period measurements are excluded.
        data = self.client.get("/api/thresholds", params={"start": 120, "end": 160}).json()
        self.assertEqual(data["channels"][0]["count"], 40)


if __name__ == "__main__":
    unittest.main()
