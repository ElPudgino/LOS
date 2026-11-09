import logging
import threading
import time

import serial

from .protocol import Parser

log = logging.getLogger(__name__)


class Collector:
    def __init__(self, store):
        self.store = store
        self.stop_event = threading.Event()
        self.thread = threading.Thread(target=self.run, name="serial-collector", daemon=True)
        self.status = dict(connected=False, port=None, last_beat=None, error=None)

    def start(self):
        self.thread.start()

    def stop(self):
        self.stop_event.set()
        self.thread.join(timeout=5)

    def run(self):
        parser = Parser(self.store)
        while not self.stop_event.is_set():
            port = self.store.settings()["port"]
            self.status.update(port=port)
            try:
                with serial.Serial(port, baudrate=115200, bytesize=8, parity="N", stopbits=1,
                                   timeout=0.25, exclusive=True) as connection:
                    self.status.update(connected=True, error=None)
                    self.store.event(time.time(), "serial_connected", port)
                    buffer = bytearray()
                    while not self.stop_event.is_set() and self.store.settings()["port"] == port:
                        chunk = connection.read(connection.in_waiting or 1)
                        now = time.time()
                        parser.expire(now)
                        for byte in chunk:
                            if byte == 10:
                                try:
                                    line = buffer.decode("ascii")
                                    parser.feed(line, now)
                                    if line == "BEAT" and parser.pending is None:
                                        self.status["last_beat"] = now
                                except UnicodeDecodeError:
                                    parser.abort(now, "Non-ASCII data")
                                    self.store.event(now, "protocol_error", "Non-ASCII data: " + buffer.hex())
                                buffer.clear()
                            else:
                                buffer.append(byte)
                                if len(buffer) > 512:
                                    parser.abort(now, "Line too long")
                                    self.store.event(now, "protocol_error", "Line too long")
                                    buffer.clear()
                    if buffer:
                        self.store.event(time.time(), "incomplete_line", buffer.hex())
            except (serial.SerialException, OSError) as exc:
                message = str(exc)
                # Avoid one identical event per retry while a port is absent.
                if self.status["error"] != message:
                    self.store.event(time.time(), "serial_error", message)
                    log.warning("Serial %s: %s", port, message)
                self.status["error"] = message
            finally:
                parser.abort(time.time(), "Port closed before END")
                if self.status["connected"]:
                    self.store.event(time.time(), "serial_disconnected", port)
                self.status["connected"] = False
            self.stop_event.wait(2)
