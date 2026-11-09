import argparse
import math
import os
import time
from contextlib import asynccontextmanager
from pathlib import Path
from typing import Literal

import uvicorn
from pydantic import Field, model_validator
from fastapi import FastAPI, HTTPException, Query
from fastapi.staticfiles import StaticFiles


from fastapi.responses import FileResponse, JSONResponse
from fastapi.exceptions import RequestValidationError

from .analysis import analyze
from .collector import Collector
from .config import Settings, StrictModel
from .storage import Store
from .thresholds import recommend_threshold


class NoCacheStaticFiles(StaticFiles):
    """The interface changes without a redeploy; always revalidate in the browser."""

    async def get_response(self, path, scope):
        response = await super().get_response(path, scope)
        response.headers["Cache-Control"] = "no-cache"
        return response


DEFAULT_DATABASE = os.environ.get("SCS_DATABASE", "data/telemetry.sqlite3")


class DeleteHistoryRequest(StrictModel):
    scope: Literal["all", "period"]
    confirm: bool = Field(strict=True)
    start: float | None = None
    end: float | None = None

    @model_validator(mode="after")
    def validate_deletion(self):
        if not self.confirm:
            raise ValueError("Explicit confirmation is required to delete history")
        if self.scope == "all":
            if self.start is not None or self.end is not None:
                raise ValueError("Do not specify bounds when deleting all history")
        elif self.start is None or self.end is None or self.start >= self.end:
            raise ValueError("A finite period with start < end is required")
        return self


def create_app(database=DEFAULT_DATABASE, collect=True):
    @asynccontextmanager
    async def lifespan(app):
        store = Store(database)
        collector = Collector(store)
        app.state.store, app.state.collector = store, collector
        if collect:
            collector.start()
        try:
            yield
        finally:
            if collect:
                collector.stop()
            store.close()

    app = FastAPI(title="SCS: telemetry", lifespan=lifespan)

    @app.exception_handler(RequestValidationError)
    async def validation_error(request, exc):
        # Do not echo invalid NaN/Infinity inputs: they cannot be encoded as JSON.
        errors = [{key: error[key] for key in ("loc", "msg", "type")} for error in exc.errors()]
        return JSONResponse(status_code=422, content={"detail": errors})

    @app.get("/api/settings")
    def settings():
        return app.state.store.settings()

    @app.put("/api/settings")
    def save_settings(settings: Settings):
        app.state.store.save_settings(settings.model_dump())
        app.state.store.event(time.time(), "settings_changed", "Settings changed; history is recalculated")
        return settings

    @app.get("/api/analysis")
    def analysis(start: float, end: float):
        if not math.isfinite(start) or not math.isfinite(end) or start >= end or end > time.time() + 1:
            raise HTTPException(422, "A finite period with start < end, not in the future, is required")
        store = app.state.store
        result, _ = analyze(store.records(end), store.settings(), start, end)
        return result

    @app.get("/api/thresholds")
    def thresholds(start: float, end: float):
        if not math.isfinite(start) or not math.isfinite(end) or start >= end or end > time.time() + 1:
            raise HTTPException(422, "A finite period with start < end, not in the future, is required")
        store = app.state.store
        values = {sensor: [] for sensor in (1, 2, 3)}
        for record in store.records(end):
            if start <= record["time"] < end and record["sensor"] in values and record["value_milli"] is not None:
                values[record["sensor"]].append(record["value_milli"] / 1000)
        channels = store.settings()["channels"]
        return dict(start=start, end=end, channels=[
            dict(id=sensor, name=channel["name"], **recommend_threshold(values[sensor]))
            for sensor, channel in zip((1, 2, 3), channels)])

    @app.get("/api/current")
    def current():
        store, now = app.state.store, time.time()
        settings = store.settings()
        _, state = analyze(store.records(now), settings, now - 1, now)
        diagnostics = store.latest_sensor_errors()
        channels = []
        for i, channel in enumerate(settings["channels"]):
            latest = state["latest"][i]
            error = diagnostics.get(i + 1)
            error = (error["detail"] if error and (latest is None or error["time"] > latest["time"])
                     else state["errors"][i])
            channels.append(dict(id=i + 1, name=channel["name"],
                                 value=latest["value_milli"] / 1000 if latest else None,
                                 seq=latest["seq"] if latest else None,
                                 last_valid=state["last_valid"][i], state=state["states"][i], error=error,
                                 available=(state["last_valid"][i] is not None
                                            and now < state["last_valid"][i] + channel["timeout"])))
        return dict(time=now, channels=channels, mode=state["mode"], connection=dict(app.state.collector.status))

    @app.get("/api/events")
    def events(limit: int = Query(default=100, ge=1, le=1000)):
        return app.state.store.events(limit, exclude_beat=True)

    @app.delete("/api/history")
    def delete_history(request: DeleteHistoryRequest):
        return {"deleted": app.state.store.delete_history(request.start, request.end)}

    static = Path(__file__).resolve().parent / "static"

    @app.get("/", include_in_schema=False)
    def index():
        return FileResponse(static / "index.html")

    app.mount("/static", NoCacheStaticFiles(directory=static), name="static")
    return app


def main():
    parser = argparse.ArgumentParser(
        description="Collect and analyze SCS telemetry. Access is local by default.",
        epilog="Example: python -m scs.app --database data/telemetry.sqlite3 --port 8000. See README.md.",
    )
    parser.add_argument("--database", default=DEFAULT_DATABASE, help="SQLite file (default: SCS_DATABASE or data/telemetry.sqlite3)")
    parser.add_argument("--port", type=int, default=8000, help="HTTP port (default: 8000)")
    args = parser.parse_args()
    if not 1 <= args.port <= 65535:
        parser.error("--port must be between 1 and 65535")
    store = Store(args.database)
    network = store.settings()["network_access"]
    store.close()
    uvicorn.run(create_app(args.database), host="0.0.0.0" if network else "127.0.0.1", port=args.port)


if __name__ == "__main__":
    main()
