import re


MEASUREMENT = re.compile(r"([123]) ([0-9A-F]{2}) ([0-9]+)\.([0-9]{3})")
SENSOR_ERROR = re.compile(r"([123]) ERR (NR|TO|CR|OF)")


class Parser:
    """Only END commits measurements. Diagnostic errors are visible immediately."""

    def __init__(self, store):
        self.store = store
        self.pending = None
        self.started = None
        self.invalid = False

    def abort(self, timestamp, reason):
        if self.pending is not None:
            raw = "\n".join(record["raw"] for record in self.pending)
            self.store.event(timestamp, "incomplete_group", f"{reason}\n{raw}")
        self.pending = None
        self.started = None
        self.invalid = False

    def expire(self, timestamp):
        if self.started is not None and timestamp - self.started >= 5:
            self.abort(timestamp, "No END within 5 seconds")

    def feed(self, line, timestamp):
        self.expire(timestamp)
        if line == "BEGIN":
            self.abort(timestamp, "Repeated BEGIN without END")
            self.pending, self.started = [], timestamp
            return
        if line == "END":
            if self.pending and len(self.pending) > 1 and any(r.get("error") == "BO" for r in self.pending):
                self.invalid = True  # BO represents a discarded group, never partial telemetry.
            if self.pending is None:
                self.store.event(timestamp, "protocol_error", "END without BEGIN")
            elif not self.invalid and self.pending:
                self.store.save_group(timestamp, self.pending)
            else:
                raw = "\n".join(record["raw"] for record in self.pending)
                self.store.event(timestamp, "invalid_group", f"Empty or invalid group\n{raw}")
            self.pending, self.started, self.invalid = None, None, False
            return
        if line == "BEAT" and self.pending is None:
            self.store.event(timestamp, "beat", "BEAT")
            return
        record = None
        match = MEASUREMENT.fullmatch(line)
        if match:
            sensor, seq, integer, fraction = match.groups()
            value = int(integer) * 1000 + int(fraction)
            if value <= 0xFFFFFFFF:
                record = dict(sensor=int(sensor), seq=int(seq, 16), value_milli=value, raw=line)
        match = SENSOR_ERROR.fullmatch(line)
        if match:
            sensor, error = match.groups()
            record = dict(sensor=int(sensor), error=error, raw=line)
            self.store.event(timestamp, "sensor_error", error, int(sensor))
        elif line == "ERR BO":
            record = dict(error="BO", raw=line)
            self.store.event(timestamp, "receiver_overflow", "ERR BO: group lost")
        if self.pending is None or record is None:
            self.store.event(timestamp, "protocol_error", line[:500])
            if self.pending is not None:
                self.invalid = True
        elif len(self.pending) >= 99:
            self.invalid = True
            self.store.event(timestamp, "protocol_error", "Group exceeds 99 records")
        else:
            self.pending.append(record)
