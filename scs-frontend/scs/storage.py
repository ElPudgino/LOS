import json
import math
import sqlite3
import threading
from pathlib import Path

from .config import default_settings


class Store:
    def __init__(self, path):
        Path(path).parent.mkdir(parents=True, exist_ok=True)
        self.lock = threading.RLock()
        self.db = sqlite3.connect(str(path), check_same_thread=False)
        self.db.row_factory = sqlite3.Row
        self.db.executescript("""
            PRAGMA journal_mode=WAL;
            PRAGMA foreign_keys=ON;
            CREATE TABLE IF NOT EXISTS settings (id INTEGER PRIMARY KEY CHECK(id=1), data TEXT NOT NULL);
            CREATE TABLE IF NOT EXISTS groups (id INTEGER PRIMARY KEY, time REAL NOT NULL);
            CREATE TABLE IF NOT EXISTS records (
                id INTEGER PRIMARY KEY, group_id INTEGER NOT NULL REFERENCES groups(id),
                position INTEGER NOT NULL, sensor INTEGER, seq INTEGER,
                value_milli INTEGER, error TEXT, raw TEXT NOT NULL
            );
            CREATE INDEX IF NOT EXISTS groups_time ON groups(time);
            CREATE INDEX IF NOT EXISTS records_group ON records(group_id);
            CREATE INDEX IF NOT EXISTS records_sensor ON records(sensor,id);
            CREATE TABLE IF NOT EXISTS events (
                id INTEGER PRIMARY KEY, time REAL NOT NULL, kind TEXT NOT NULL,
                detail TEXT NOT NULL, sensor INTEGER
            );
            CREATE INDEX IF NOT EXISTS events_time ON events(time);
            CREATE INDEX IF NOT EXISTS events_sensor_kind ON events(sensor,kind,id);
        """)
        with self.db:
            self.db.execute("INSERT OR IGNORE INTO settings VALUES (1, ?)", (json.dumps(default_settings()),))

    def settings(self):
        with self.lock:
            return json.loads(self.db.execute("SELECT data FROM settings WHERE id=1").fetchone()[0])

    def save_settings(self, settings):
        with self.lock, self.db:
            self.db.execute("UPDATE settings SET data=? WHERE id=1", (json.dumps(settings),))

    def event(self, timestamp, kind, detail, sensor=None):
        with self.lock, self.db:
            self.db.execute("INSERT INTO events(time,kind,detail,sensor) VALUES (?,?,?,?)",
                            (timestamp, kind, detail, sensor))

    def save_group(self, timestamp, records):
        with self.lock, self.db:
            group_id = self.db.execute("INSERT INTO groups(time) VALUES (?)", (timestamp,)).lastrowid
            for position, record in enumerate(records):
                sensor, seq = record.get("sensor"), record.get("seq")
                if seq is not None:
                    previous = self.db.execute(
                        "SELECT seq FROM records WHERE sensor=? AND seq IS NOT NULL ORDER BY id DESC LIMIT 1",
                        (sensor,),
                    ).fetchone()
                    if previous and seq != (previous[0] + 1) % 256:
                        delta = (seq - previous[0] - 1) % 256
                        self.db.execute("INSERT INTO events(time,kind,detail,sensor) VALUES (?,?,?,?)", (
                            timestamp, "seq_discontinuity",
                            f"SEQ {previous[0]:02X} -> {seq:02X}; possible gap {delta}; "
                            "duplicate/restart/counter wrap cannot be distinguished", sensor,
                        ))
                self.db.execute(
                    "INSERT INTO records(group_id,position,sensor,seq,value_milli,error,raw) VALUES (?,?,?,?,?,?,?)",
                    (group_id, position, sensor, seq, record.get("value_milli"), record.get("error"), record["raw"]),
                )

    def records(self, end):
        # Include pre-period history: hysteresis may be determined arbitrarily far back.
        with self.lock:
            return [dict(row) for row in self.db.execute(
                "SELECT r.*,g.time FROM records r JOIN groups g ON r.group_id=g.id "
                "WHERE g.time<=? ORDER BY g.time,g.id,r.position", (end,),
            )]

    def events(self, limit=100, exclude_beat=False):
        where = "WHERE kind != 'beat'" if exclude_beat else ""
        with self.lock:
            return [dict(row) for row in self.db.execute(
                f"SELECT * FROM events {where} ORDER BY id DESC LIMIT ?", (limit,),
            )]

    def delete_history(self, start=None, end=None):
        if (start is None) != (end is None):
            raise ValueError("Both period bounds are required")
        if start is not None and (not math.isfinite(start) or not math.isfinite(end) or start >= end):
            raise ValueError("A finite period with start < end is required")
        where = "WHERE time >= ? AND time < ?" if start is not None else ""
        params = (start, end) if start is not None else ()
        with self.lock, self.db:
            records = self.db.execute(
                f"DELETE FROM records WHERE group_id IN (SELECT id FROM groups {where})", params,
            ).rowcount
            groups = self.db.execute(f"DELETE FROM groups {where}", params).rowcount
            events = self.db.execute(f"DELETE FROM events {where}", params).rowcount
            return dict(groups=groups, records=records, events=events)

    def latest_sensor_errors(self):
        with self.lock:
            result = {}
            for sensor in (1, 2, 3):
                row = self.db.execute(
                    "SELECT * FROM events WHERE sensor=? AND kind='sensor_error' ORDER BY id DESC LIMIT 1",
                    (sensor,),
                ).fetchone()
                if row:
                    result[sensor] = dict(row)
            return result

    def close(self):
        with self.lock:
            self.db.close()
