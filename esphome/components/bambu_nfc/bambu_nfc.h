#pragma once

#include "esphome/core/component.h"
#include "esphome/core/automation.h"
#include "esphome/components/pn532/pn532.h"
#include "esphome/components/i2c/i2c.h"
#include "esphome/components/text_sensor/text_sensor.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/button/button.h"

#include "bambu_colors.h"

namespace esphome {
namespace bambu_nfc {

class BambuNfc;

class BambuNfcResetButton : public button::Button, public Parented<BambuNfc> {
 protected:
  void press_action() override;
};

class BambuSuccessTrigger : public Trigger<> {};
class BambuErrorTrigger : public Trigger<> {};

class BambuNfc : public pn532::PN532, public i2c::I2CDevice {
 public:
  void dump_config() override;
  void loop() override;
  void clear_sensors();

  void set_filament_type_sensor(text_sensor::TextSensor *s) { filament_type_sensor_ = s; }
  void set_filament_color_sensor(text_sensor::TextSensor *s) { filament_color_sensor_ = s; }
  void set_filament_color_name_sensor(text_sensor::TextSensor *s) { filament_color_name_sensor_ = s; }
  void set_tray_uid_sensor(text_sensor::TextSensor *s) { tray_uid_sensor_ = s; }
  void set_tray_info_idx_sensor(text_sensor::TextSensor *s) { tray_info_idx_sensor_ = s; }
  void set_production_date_sensor(text_sensor::TextSensor *s) { production_date_sensor_ = s; }
  void set_last_scan_date_sensor(text_sensor::TextSensor *s) { last_scan_date_sensor_ = s; }

  void set_min_temp_sensor(sensor::Sensor *s) { min_temp_sensor_ = s; }
  void set_max_temp_sensor(sensor::Sensor *s) { max_temp_sensor_ = s; }
  void set_bed_temp_sensor(sensor::Sensor *s) { bed_temp_sensor_ = s; }
  void set_spool_weight_sensor(sensor::Sensor *s) { spool_weight_sensor_ = s; }
  void set_filament_diameter_sensor(sensor::Sensor *s) { filament_diameter_sensor_ = s; }
  void set_drying_temp_sensor(sensor::Sensor *s) { drying_temp_sensor_ = s; }
  void set_drying_time_sensor(sensor::Sensor *s) { drying_time_sensor_ = s; }
  void set_nozzle_diameter_sensor(sensor::Sensor *s) { nozzle_diameter_sensor_ = s; }
  void set_spool_width_sensor(sensor::Sensor *s) { spool_width_sensor_ = s; }
  void set_filament_length_sensor(sensor::Sensor *s) { filament_length_sensor_ = s; }

  void register_bambu_success_trigger(BambuSuccessTrigger *t) { success_triggers_.push_back(t); }
  void register_bambu_error_trigger(BambuErrorTrigger *t) { error_triggers_.push_back(t); }

 protected:
  bool is_read_ready() override;
  bool write_data(const std::vector<uint8_t> &data) override;
  bool read_data(std::vector<uint8_t> &data, uint8_t len) override;
  bool read_response(uint8_t command, std::vector<uint8_t> &data) override;
  uint8_t read_response_length_();

  bool read_bambu_data_(nfc::NfcTagUid &uid);
  bool derive_bambu_keys_(const uint8_t *uid, size_t uid_len, uint8_t *keys_out);

  text_sensor::TextSensor *filament_type_sensor_{nullptr};
  text_sensor::TextSensor *filament_color_sensor_{nullptr};
  text_sensor::TextSensor *filament_color_name_sensor_{nullptr};
  text_sensor::TextSensor *tray_uid_sensor_{nullptr};
  text_sensor::TextSensor *tray_info_idx_sensor_{nullptr};
  text_sensor::TextSensor *production_date_sensor_{nullptr};
  text_sensor::TextSensor *last_scan_date_sensor_{nullptr};

  sensor::Sensor *min_temp_sensor_{nullptr};
  sensor::Sensor *max_temp_sensor_{nullptr};
  sensor::Sensor *bed_temp_sensor_{nullptr};
  sensor::Sensor *spool_weight_sensor_{nullptr};
  sensor::Sensor *filament_diameter_sensor_{nullptr};
  sensor::Sensor *drying_temp_sensor_{nullptr};
  sensor::Sensor *drying_time_sensor_{nullptr};
  sensor::Sensor *nozzle_diameter_sensor_{nullptr};
  sensor::Sensor *spool_width_sensor_{nullptr};
  sensor::Sensor *filament_length_sensor_{nullptr};

  std::vector<BambuSuccessTrigger *> success_triggers_;
  std::vector<BambuErrorTrigger *> error_triggers_;
};

}  // namespace bambu_nfc
}  // namespace esphome
