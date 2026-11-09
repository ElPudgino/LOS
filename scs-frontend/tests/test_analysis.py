import copy
import unittest

from scs.analysis import analyze, flow_state, resolve_mode
from tests.helpers import error, measurement, settings


class HysteresisTests(unittest.TestCase):
    def test_strict_thresholds_and_retained_state(self):
        channel = settings()["channels"][0]
        for previous in ("unknown", "flow", "no_flow"):
            for value in (9, 10, 11):
                with self.subTest(previous=previous, value=value):
                    self.assertEqual(flow_state(value, previous, channel), previous)
            self.assertEqual(flow_state(11.001, previous, channel), "flow")
            self.assertEqual(flow_state(8.999, previous, channel), "no_flow")

    def test_zero_hysteresis_equality_retains_state(self):
        channel = dict(settings()["channels"][0], hysteresis=0)
        self.assertEqual(flow_state(10, "unknown", channel), "unknown")
        self.assertEqual(flow_state(10, "flow", channel), "flow")
        self.assertEqual(flow_state(10.001, "no_flow", channel), "flow")
        self.assertEqual(flow_state(9.999, "flow", channel), "no_flow")

    def test_timeout_exact_boundary_and_inside_band_reset(self):
        config = settings(timeout=5)
        records = [measurement(0, 12), measurement(5, 10)]
        result, state = analyze(records, config, 0, 12)
        self.assertEqual(result["channels"][0]["intervals"], [
            dict(start=0, end=5, state="flow"), dict(start=5, end=12, state="unknown")])
        self.assertEqual(state["states"][0], "unknown")
        self.assertEqual(result["mode"]["intervals"], [
            dict(start=0, end=5, state="mode_1"), dict(start=5, end=10, state="unknown"),
            dict(start=10, end=12, state="no_data")])
        _, boundary = analyze([measurement(0, 12)], config, 0, 5)
        self.assertEqual(boundary["states"][0], "unknown")
        self.assertEqual(boundary["mode"], "no_data")

    def test_error_neither_resets_state_nor_refreshes_timeout(self):
        result, state = analyze([measurement(0, 12), error(4)], settings(timeout=5), 0, 7)
        channel = result["channels"][0]
        self.assertEqual(channel["values"]["all"], dict(count=1, min=12, max=12, median=12))
        self.assertEqual(channel["durations"]["flow"]["total"], 5)
        self.assertEqual(channel["durations"]["no_flow"]["total"], 0)
        self.assertEqual(state["last_valid"][0], 0)
        self.assertEqual(state["errors"][0], "TO")
        self.assertEqual(state["states"][0], "unknown")

    def test_channel_timeouts_are_independent(self):
        config = settings(timeout=10)
        config["channels"][0]["timeout"] = 2
        config["channels"][1]["timeout"] = 4
        _, state = analyze([measurement(0, 12, sensor=i) for i in (1, 2, 3)], config, 0, 5)
        self.assertEqual(state["states"], ["unknown", "unknown", "flow"])

    def test_valid_measurement_clears_sensor_error(self):
        _, state = analyze([measurement(0, 12), error(1), measurement(2, 10)], settings(), 0, 3)
        self.assertEqual(state["errors"][0], None)
        self.assertEqual(state["states"][0], "flow")
        self.assertEqual(state["last_valid"][0], 2)


class ModeTests(unittest.TestCase):
    def test_both_modes_neither_mode_and_missing_data(self):
        modes = settings()["modes"]
        cases = [(["flow", "unknown", "unknown"], [True, False, False], "mode_1"),
                 (["no_flow", "unknown", "unknown"], [True, False, False], "mode_2"),
                 (["unknown"] * 3, [True] * 3, "unknown"),
                 (["unknown"] * 3, [False] * 3, "no_data")]
        for states, available, expected in cases:
            with self.subTest(states=states, available=available):
                self.assertEqual(resolve_mode(states, available, modes), expected)
        modes[1]["conditions"] = modes[0]["conditions"][:]
        self.assertEqual(resolve_mode(["flow"] * 3, [True] * 3, modes), "unknown")
        self.assertEqual(resolve_mode(["no_flow"] * 3, [True] * 3, modes), "unknown")

    def test_ignore_missing_channels_does_not_prevent_unique_mode(self):
        modes = settings()["modes"]
        self.assertEqual(resolve_mode(["flow", "unknown", "unknown"], [True, False, False], modes), "mode_1")
        modes[1]["conditions"] = ["ignore"] * 3
        # A match and another possible match are not a unique known mode.
        self.assertEqual(resolve_mode(["unknown"] * 3, [False] * 3, modes), "no_data")
        modes[0]["conditions"] = ["ignore"] * 3
        self.assertEqual(resolve_mode(["unknown"] * 3, [False] * 3, modes), "unknown")

    def test_unknown_ignored_sensor_cannot_create_no_data(self):
        config = settings()
        records = [measurement(0, 12)]
        result, state = analyze(records, config, 0, 10)
        self.assertEqual(state["mode"], "mode_1")
        self.assertEqual(result["mode"]["summary"]["mode_1"]["share"], 1)

    def test_missing_channel_in_excluded_mode_is_not_cause_of_uncertainty(self):
        modes = settings()["modes"]
        modes[0]["conditions"] = ["flow", "ignore", "ignore"]
        modes[1]["conditions"] = ["ignore", "flow", "flow"]
        self.assertEqual(resolve_mode(["unknown", "no_flow", "unknown"], [True, True, False], modes), "unknown")

    def test_switches_do_not_bridge_unknown_or_no_data_gaps(self):
        records = [measurement(0, 12), measurement(2, 8), measurement(10, 12)]
        result, _ = analyze(records, settings(timeout=3), 0, 12)
        self.assertEqual(result["mode"]["switches"], 1)
        self.assertEqual([i["state"] for i in result["mode"]["intervals"]],
                         ["mode_1", "mode_2", "no_data", "mode_1"])


class StatisticsTests(unittest.TestCase):
    def test_hand_calculated_values_durations_medians_and_modes(self):
        # Flow: [0,5), [9,16); no flow: [5,9), [16,20).
        # Retained band readings belong to their previous state.
        records = [measurement(t, value, seq=i) for i, (t, value) in enumerate(
            [(0, 12), (2, 10), (5, 8), (9, 13), (12, 11), (16, 7)])]
        result, _ = analyze(records, settings(), 0, 20)
        channel = result["channels"][0]
        self.assertEqual(channel["values"], {
            "all": dict(count=6, min=7, max=13, median=10.5),
            "flow": dict(count=4, min=10, max=13, median=11.5),
            "no_flow": dict(count=2, min=7, max=8, median=7.5)})
        self.assertEqual(channel["durations"]["flow"], dict(count=2, total=12, min=5, max=7, median=6))
        self.assertEqual(channel["durations"]["no_flow"], dict(count=2, total=8, min=4, max=4, median=4))
        self.assertEqual(channel["durations"]["unknown"], dict(count=0, total=0, min=None, max=None, median=None))
        summary = result["mode"]["summary"]
        self.assertEqual(summary["mode_1"]["duration"], 12)
        self.assertEqual(summary["mode_1"]["share"], 0.6)
        self.assertEqual(summary["mode_1"]["median"], 6)
        self.assertEqual(summary["mode_2"]["duration"], 8)
        self.assertEqual(summary["mode_2"]["share"], 0.4)
        self.assertEqual(result["mode"]["switches"], 3)
        self.assertTrue(result["approximate"])

    def test_period_clips_intervals_and_excludes_end_measurements(self):
        records = [measurement(0, 12), measurement(5, 8), measurement(9, 13),
                   measurement(12, 11), measurement(16, 7)]
        result, _ = analyze(records, settings(), 3, 16)
        channel = result["channels"][0]
        self.assertEqual(channel["intervals"], [dict(start=3, end=5, state="flow"),
            dict(start=5, end=9, state="no_flow"), dict(start=9, end=16, state="flow")])
        self.assertEqual([m["time"] for m in channel["measurements"]], [5, 9, 12])
        self.assertEqual(channel["values"]["all"], dict(count=3, min=8, max=13, median=11))
        self.assertEqual(channel["durations"]["flow"], dict(count=2, total=9, min=2, max=7, median=4.5))
        self.assertEqual(sum(s["duration"] for s in result["mode"]["summary"].values()), 13)

    def test_history_carry_in_requires_more_than_last_preperiod_measurement(self):
        records = [measurement(0, 12), measurement(20, 10), measurement(40, 10), measurement(60, 10)]
        result, _ = analyze(records, settings(timeout=30), 50, 70)
        channel = result["channels"][0]
        self.assertEqual(channel["intervals"], [dict(start=50, end=70, state="flow")])
        self.assertEqual(channel["values"]["all"]["count"], 1)
        self.assertEqual(channel["durations"]["flow"]["total"], 20)

    def test_carry_in_expiry_inside_period_and_before_period(self):
        records = [measurement(0, 12)]
        result, _ = analyze(records, settings(timeout=10), 5, 15)
        self.assertEqual(result["channels"][0]["intervals"], [
            dict(start=5, end=10, state="flow"), dict(start=10, end=15, state="unknown")])
        result, _ = analyze(records, settings(timeout=10), 20, 30)
        self.assertEqual(result["channels"][0]["intervals"], [dict(start=20, end=30, state="unknown")])
        self.assertEqual(result["mode"]["summary"]["no_data"]["share"], 1)

    def test_current_settings_recompute_history_without_mutating_inputs(self):
        records = [measurement(0, 12), measurement(5, 10), measurement(10, 8)]
        original = copy.deepcopy(records)
        config = settings()
        first, _ = analyze(records, config, 0, 15)
        config["channels"][0]["threshold"] = 20
        second, _ = analyze(records, config, 0, 15)
        self.assertEqual(first["channels"][0]["durations"]["flow"]["total"], 10)
        self.assertEqual(second["channels"][0]["durations"]["no_flow"]["total"], 15)
        self.assertEqual(first["channels"][0]["values"]["all"], second["channels"][0]["values"]["all"])
        self.assertEqual(records, original)
        self.assertNotIn("intervals", config["channels"][0])

    def test_same_timestamp_preserves_order_without_zero_duration_intervals(self):
        records = [measurement(1, 12), measurement(1, 8), measurement(1, 10)]
        result, state = analyze(records, settings(), 0, 2)
        self.assertEqual([m["state"] for m in result["channels"][0]["measurements"]], ["flow", "no_flow", "no_flow"])
        self.assertEqual(state["states"][0], "no_flow")
        self.assertEqual(result["channels"][0]["durations"]["flow"]["count"], 0)
        self.assertEqual(result["mode"]["switches"], 0)

    def test_empty_period_has_null_value_statistics_and_no_false_no_flow(self):
        result, state = analyze([], settings(), 1, 11)
        for channel in result["channels"]:
            self.assertEqual(channel["values"]["all"], dict(count=0, min=None, max=None, median=None))
            self.assertEqual(channel["durations"]["unknown"]["total"], 10)
            self.assertEqual(channel["durations"]["no_flow"]["total"], 0)
        self.assertEqual(state["mode"], "no_data")
        self.assertEqual(result["mode"]["summary"]["no_data"]["share"], 1)
