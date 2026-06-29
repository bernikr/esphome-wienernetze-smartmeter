# ruff: noqa: ANN002, ANN003, ANN204, RUF067
import esphome.config_validation as cv

DEPENDENCIES = ["uart"]
CODEOWNERS = ["@bernikr"]
AUTO_LOAD = ["text_sensor", "sensor"]
MULTI_CONF = True


class Deprecated(cv.Schema):
    def __init__(self, *_, **__):
        super().__init__({})

    def __call__(self, *_, **__):
        msg = "The 'im150' component/platform was renamed and is deprecated.\nPlease use 'wienernetze' instead."
        raise cv.Invalid(msg)


CONFIG_SCHEMA = Deprecated()
