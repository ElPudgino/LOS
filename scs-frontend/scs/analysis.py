from itertools import groupby
from statistics import median


def value_stats(values):
    return {"count": len(values), "min": min(values) if values else None,
            "max": max(values) if values else None, "median": median(values) if values else None}


def duration_stats(intervals, state):
    values = [i["end"] - i["start"] for i in intervals if i["state"] == state]
    return {**value_stats(values), "total": sum(values)}


def flow_state(value, previous, channel):
    if value > channel["threshold"] + channel["hysteresis"] / 2:
        return "flow"
    if value < channel["threshold"] - channel["hysteresis"] / 2:
        return "no_flow"
    return previous


def resolve_mode(states, available, modes):
    outcomes = []
    missing = False
    for mode in modes:
        uncertain = False
        mismatch = False
        mode_missing = False
        for index, condition in enumerate(mode["conditions"]):
            if condition == "ignore":
                continue
            if states[index] == "unknown":
                uncertain = True
                mode_missing |= not available[index]
            elif states[index] != condition:
                mismatch = True
        outcomes.append(False if mismatch else None if uncertain else True)
        if not mismatch:
            missing |= mode_missing
    if outcomes == [True, False]:
        return "mode_1"
    if outcomes == [False, True]:
        return "mode_2"
    return "no_data" if missing and None in outcomes else "unknown"


def append_interval(intervals, start, end, state):
    if end <= start:
        return
    if intervals and intervals[-1]["state"] == state and intervals[-1]["end"] == start:
        intervals[-1]["end"] = end
    else:
        intervals.append(dict(start=start, end=end, state=state))


def analyze(records, settings, start, end):
    channels = [dict(id=i + 1, **channel, measurements=[], intervals=[])
                for i, channel in enumerate(settings["channels"])]
    states = ["unknown"] * 3
    last_valid = [None] * 3
    latest = [None] * 3
    errors = [None] * 3
    mode_intervals = []
    cursor = min(start, records[0]["time"]) if records else start

    def expire_at(timestamp):
        for i, channel in enumerate(channels):
            if last_valid[i] is not None and timestamp >= last_valid[i] + channel["timeout"]:
                states[i] = "unknown"

    def advance(target):
        nonlocal cursor
        boundaries = sorted({last_valid[i] + c["timeout"] for i, c in enumerate(channels)
                             if last_valid[i] is not None and cursor < last_valid[i] + c["timeout"] < target})
        for boundary in [*boundaries, target]:
            available = [last_valid[i] is not None and cursor < last_valid[i] + c["timeout"]
                         for i, c in enumerate(channels)]
            for i, channel in enumerate(channels):
                append_interval(channel["intervals"], max(start, cursor), boundary, states[i])
            append_interval(mode_intervals, max(start, cursor), boundary,
                            resolve_mode(states, available, settings["modes"]))
            cursor = boundary
            expire_at(boundary)

    for timestamp, batch in groupby(records, key=lambda r: r["time"]):
        advance(timestamp)
        for record in batch:
            if record["sensor"] is None:
                continue
            i = record["sensor"] - 1
            if record["value_milli"] is None:
                errors[i] = record["error"]
                continue
            value = record["value_milli"] / 1000
            states[i] = flow_state(value, states[i], channels[i])
            last_valid[i], latest[i], errors[i] = timestamp, record, None
            if start <= timestamp < end:
                channels[i]["measurements"].append(dict(time=timestamp, seq=record["seq"], value=value, state=states[i]))
    advance(end)
    for i, channel in enumerate(channels):
        measurements = channel["measurements"]
        channel["values"] = {
            key: value_stats([m["value"] for m in measurements if key == "all" or m["state"] == key])
            for key in ("all", "flow", "no_flow")
        }
        channel["durations"] = {key: duration_stats(channel["intervals"], key)
                                for key in ("flow", "no_flow", "unknown")}
    summary = {}
    for key in ("mode_1", "mode_2", "unknown", "no_data"):
        stats = duration_stats(mode_intervals, key)
        summary[key] = {**stats, "duration": stats["total"], "share": stats["total"] / (end - start)}
    # Only direct transitions between known modes count as switches; gaps are not inferred.
    switches = sum(a["state"] in ("mode_1", "mode_2") and b["state"] in ("mode_1", "mode_2")
                   and a["state"] != b["state"] for a, b in zip(mode_intervals, mode_intervals[1:]))
    return dict(start=start, end=end, channels=channels, approximate=True,
                mode=dict(intervals=mode_intervals, summary=summary, switches=switches)), dict(
                    states=states, latest=latest, last_valid=last_valid, errors=errors,
                    mode=resolve_mode(states, [last_valid[i] is not None and end < last_valid[i] + c["timeout"]
                                               for i, c in enumerate(channels)], settings["modes"]))
