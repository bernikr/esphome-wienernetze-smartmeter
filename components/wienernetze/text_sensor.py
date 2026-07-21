# ruff:file-ignore[missing-type-function-argument, missing-return-type-undocumented-public-function]

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import text_sensor

from . import CONF_WIENERNETZE_ID, WIENERNETZE_COMPONENT_SCHEMA

T_SENSORS = [
    "active_energy_pos",
    "active_energy_neg",
    "reactive_energy_pos",
    "reactive_energy_neg",
]

CONFIG_SCHEMA = WIENERNETZE_COMPONENT_SCHEMA.extend(
    {cv.Optional(t): text_sensor.text_sensor_schema() for t in T_SENSORS},
)


async def to_code(config):
    paren = await cg.get_variable(config[CONF_WIENERNETZE_ID])

    for t in T_SENSORS:
        if t in config:
            conf = config[t]
            sens = await text_sensor.new_text_sensor(conf)
            cg.add(getattr(paren, f"set_{t}_raw")(sens))
