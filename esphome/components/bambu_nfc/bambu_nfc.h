#pragma once

#include "esphome/core/component.h"
#include "esphome/components/pn532/pn532.h"
#include "esphome/components/i2c/i2c.h"
#include "esphome/components/text_sensor/text_sensor.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/button/button.h"

namespace esphome {
namespace bambu_nfc {

class BambuNfc;

class BambuNfcResetButton : public button::Button, public Parented<BambuNfc> {
 protected:
  void press_action() override;
};

class BambuNfc : public pn532::PN532, public i2c::I2CDevice {
 public:
  void dump_config() override;
  void loop() override;
  void clear_sensors();

  void set_filament_type_sensor(text_sensor::TextSensor *s) { filament_type_sensor_ = s; }
  void set_filament_color_sensor(text_sensor::TextSensor *s) { filament_color_sensor_ = s; }
  void set_tray_uid_sensor(text_sensor::TextSensor *s) { tray_uid_sensor_ = s; }
  void set_production_date_sensor(text_sensor::TextSensor *s) { production_date_sensor_ = s; }
  void set_last_scan_date_sensor(text_sensor::TextSensor *s) { last_scan_date_sensor_ = s; }
  void set_min_temp_sensor(sensor::Sensor *s) { min_temp_sensor_ = s; }
  void set_max_temp_sensor(sensor::Sensor *s) { max_temp_sensor_ = s; }
  void set_bed_temp_sensor(sensor::Sensor *s) { bed_temp_sensor_ = s; }

 protected:
  // I2C transport (from PN532I2C)
  bool is_read_ready() override;
  bool write_data(const std::vector<uint8_t> &data) override;
  bool read_data(std::vector<uint8_t> &data, uint8_t len) override;
  bool read_response(uint8_t command, std::vector<uint8_t> &data) override;
  uint8_t read_response_length_();

  // Bambu-specific
  void read_bambu_data_(nfc::NfcTagUid &uid);
  bool derive_bambu_keys_(const uint8_t *uid, size_t uid_len, uint8_t *keys_out);

  // Sensors
  text_sensor::TextSensor *filament_type_sensor_{nullptr};
  text_sensor::TextSensor *filament_color_sensor_{nullptr};
  text_sensor::TextSensor *tray_uid_sensor_{nullptr};
  text_sensor::TextSensor *production_date_sensor_{nullptr};
  text_sensor::TextSensor *last_scan_date_sensor_{nullptr};
  sensor::Sensor *min_temp_sensor_{nullptr};
  sensor::Sensor *max_temp_sensor_{nullptr};
  sensor::Sensor *bed_temp_sensor_{nullptr};
};

}  // namespace bambu_nfc
}  // namespace esphome
