from typing import Literal

from pydantic import BaseModel, ConfigDict, Field


class StrictModel(BaseModel):
    model_config = ConfigDict(extra="forbid", allow_inf_nan=False)


class Channel(StrictModel):
    name: str = Field(min_length=1, max_length=100)
    role: str = Field(default="", max_length=200)
    threshold: float = 10
    hysteresis: float = Field(default=2, ge=0)
    timeout: float = Field(default=60, gt=0)


class Mode(StrictModel):
    name: str = Field(min_length=1, max_length=100)
    conditions: list[Literal["flow", "no_flow", "ignore"]] = Field(min_length=3, max_length=3)


class Settings(StrictModel):
    port: str = Field(default="/dev/ttyACM0", min_length=1, max_length=300)
    network_access: bool = False
    channels: list[Channel] = Field(min_length=3, max_length=3)
    modes: list[Mode] = Field(min_length=2, max_length=2)


def default_settings():
    return Settings(
        channels=[Channel(name=f"Датчик {i}") for i in range(1, 4)],
        modes=[Mode(name=f"Режим {i}", conditions=["ignore"] * 3) for i in range(1, 3)],
    ).model_dump()
