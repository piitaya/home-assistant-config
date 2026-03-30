import esphome.codegen as cg
from esphome.components import i2c, pn532, text_sensor, sensor, button
import esphome.config_validation as cv
from esphome.const import CONF_ID

AUTO_LOAD = ["pn532", "text_sensor", "sensor", "button"]
DEPENDENCIES = ["i2c"]

CONF_FILAMENT_TYPE = "filament_type"
CONF_FILAMENT_COLOR = "filament_color"
CONF_MIN_HOTEND_TEMP = "min_hotend_temp"
CONF_MAX_HOTEND_TEMP = "max_hotend_temp"
CONF_BED_TEMP = "bed_temp"
CONF_TRAY_UID = "tray_uid"
CONF_PRODUCTION_DATE = "production_date"
CONF_LAST_SCAN_DATE = "last_scan_date"
CONF_RESET = "reset"

bambu_nfc_ns = cg.esphome_ns.namespace("bambu_nfc")
BambuNfc = bambu_nfc_ns.class_("BambuNfc", pn532.PN532, i2c.I2CDevice)
BambuNfcResetButton = bambu_nfc_ns.class_(
    "BambuNfcResetButton", button.Button, cg.Parented.template(BambuNfc)
)

CONFIG_SCHEMA = cv.All(
    pn532.PN532_SCHEMA.extend(
        {
            cv.GenerateID(): cv.declare_id(BambuNfc),
            cv.Optional(CONF_FILAMENT_TYPE): text_sensor.text_sensor_schema(),
            cv.Optional(CONF_FILAMENT_COLOR): text_sensor.text_sensor_schema(),
            cv.Optional(CONF_TRAY_UID): text_sensor.text_sensor_schema(),
            cv.Optional(CONF_PRODUCTION_DATE): text_sensor.text_sensor_schema(),
            cv.Optional(CONF_LAST_SCAN_DATE): text_sensor.text_sensor_schema(),
            cv.Optional(CONF_MIN_HOTEND_TEMP): sensor.sensor_schema(
                unit_of_measurement="°C",
                accuracy_decimals=0,
            ),
            cv.Optional(CONF_MAX_HOTEND_TEMP): sensor.sensor_schema(
                unit_of_measurement="°C",
                accuracy_decimals=0,
            ),
            cv.Optional(CONF_BED_TEMP): sensor.sensor_schema(
                unit_of_measurement="°C",
                accuracy_decimals=0,
            ),
            cv.Optional(CONF_RESET): button.button_schema(BambuNfcResetButton),
        }
    ).extend(i2c.i2c_device_schema(0x24))
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await pn532.setup_pn532(var, config)
    await i2c.register_i2c_device(var, config)

    for key, setter in [
        (CONF_FILAMENT_TYPE, "set_filament_type_sensor"),
        (CONF_FILAMENT_COLOR, "set_filament_color_sensor"),
        (CONF_TRAY_UID, "set_tray_uid_sensor"),
        (CONF_PRODUCTION_DATE, "set_production_date_sensor"),
        (CONF_LAST_SCAN_DATE, "set_last_scan_date_sensor"),
    ]:
        if key in config:
            sens = await text_sensor.new_text_sensor(config[key])
            cg.add(getattr(var, setter)(sens))

    for key, setter in [
        (CONF_MIN_HOTEND_TEMP, "set_min_temp_sensor"),
        (CONF_MAX_HOTEND_TEMP, "set_max_temp_sensor"),
        (CONF_BED_TEMP, "set_bed_temp_sensor"),
    ]:
        if key in config:
            sens = await sensor.new_sensor(config[key])
            cg.add(getattr(var, setter)(sens))

    if CONF_RESET in config:
        btn = await button.new_button(config[CONF_RESET])
        await cg.register_parented(btn, var)
