import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import number
from esphome.const import ENTITY_CATEGORY_CONFIG, UNIT_AMPERE

from . import CONF_DELTA_WALLBOX_ID, DeltaWallbox, delta_wallbox_ns

DEPENDENCIES = ["delta_wallbox"]

CONF_MIN_VALUE = "min_value"
CONF_MAX_VALUE = "max_value"
CONF_MAX_CURRENT = "max_current"

DeltaWallboxMaxCurrentNumber = delta_wallbox_ns.class_(
    "DeltaWallboxMaxCurrentNumber", number.Number
)


def validate_current_range(config):
    if config[CONF_MIN_VALUE] > config[CONF_MAX_VALUE]:
        raise cv.Invalid("min_value must be less than or equal to max_value")
    return config

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(CONF_DELTA_WALLBOX_ID): cv.use_id(DeltaWallbox),
        cv.Optional(CONF_MAX_CURRENT): cv.All(
            number.number_schema(
                DeltaWallboxMaxCurrentNumber,
                unit_of_measurement=UNIT_AMPERE,
                entity_category=ENTITY_CATEGORY_CONFIG,
            ).extend(
                {
                    cv.Optional(CONF_MIN_VALUE, default=0): cv.int_range(min=0, max=255),
                    cv.Optional(CONF_MAX_VALUE, default=255): cv.int_range(min=0, max=255),
                }
            ),
            validate_current_range,
        ),
    }
)


async def to_code(config):
    parent = await cg.get_variable(config[CONF_DELTA_WALLBOX_ID])

    if max_current_config := config.get(CONF_MAX_CURRENT):
        number_var = await number.new_number(
            max_current_config,
            min_value=max_current_config[CONF_MIN_VALUE],
            max_value=max_current_config[CONF_MAX_VALUE],
            step=1,
        )
        await cg.register_parented(number_var, parent)
        cg.add(parent.set_max_current_number(number_var))
