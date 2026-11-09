from scs.config import default_settings


def settings(timeout=60):
    result = default_settings()
    for channel in result["channels"]:
        channel.update(threshold=10, hysteresis=2, timeout=timeout)
    result["modes"][0]["conditions"] = ["flow", "ignore", "ignore"]
    result["modes"][1]["conditions"] = ["no_flow", "ignore", "ignore"]
    return result


def measurement(timestamp, value, sensor=1, seq=0):
    return dict(time=timestamp, sensor=sensor, seq=seq,
                value_milli=round(value * 1000), error=None,
                raw=f"{sensor} {seq:02X} {value:.3f}")


def error(timestamp, code="TO", sensor=1):
    return dict(time=timestamp, sensor=sensor, seq=None,
                value_milli=None, error=code, raw=f"{sensor} ERR {code}")
