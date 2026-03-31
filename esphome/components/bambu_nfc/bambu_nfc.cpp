#include "bambu_nfc.h"
#include "esphome/core/log.h"
#include "esphome/core/hal.h"

#include <mbedtls/md.h>

#include <cstring>
#include <ctime>
#include <memory>

namespace esphome {
namespace bambu_nfc {

static const char *const TAG = "bambu_nfc";

static const uint8_t BAMBU_MASTER_KEY[] = {
    0x9a, 0x75, 0x9c, 0xf2, 0xc4, 0xf7, 0xca, 0xff,
    0x22, 0x2c, 0xb9, 0x76, 0x9b, 0x41, 0xbc, 0x96};

static const uint8_t BAMBU_HKDF_INFO[] = {'R', 'F', 'I', 'D', '-', 'A', '\0'};

// ---- I2C Transport (from PN532I2C) ----

bool BambuNfc::is_read_ready() {
  uint8_t ready;
  if (!this->read_bytes_raw(&ready, 1))
    return false;
  return ready == 0x01;
}

bool BambuNfc::write_data(const std::vector<uint8_t> &data) {
  return this->write(data.data(), data.size()) == i2c::ERROR_OK;
}

bool BambuNfc::read_data(std::vector<uint8_t> &data, uint8_t len) {
  delay(1);
  if (this->read_ready_(true) != pn532::PN532ReadReady::READY)
    return false;
  data.resize(len + 1);
  this->read_bytes_raw(data.data(), len + 1);
  return true;
}

bool BambuNfc::read_response(uint8_t command, std::vector<uint8_t> &data) {
  uint8_t len = this->read_response_length_();
  if (len == 0)
    return false;

  if (!this->read_data(data, 6 + len + 2))
    return false;

  if (data[1] != 0x00 || data[2] != 0x00 || data[3] != 0xFF)
    return false;

  bool valid_header = (static_cast<uint8_t>(data[4] + data[5]) == 0 &&
                       data[6] == 0xD5 &&
                       data[7] == command + 1);
  if (!valid_header)
    return false;

  data.erase(data.begin(), data.begin() + 6);

  uint8_t checksum = 0;
  for (int i = 0; i < len + 1; i++)
    checksum += data[i];
  checksum = ~checksum + 1;

  if (data[len + 1] != checksum)
    return false;
  if (data[len + 2] != 0x00)
    return false;

  data.erase(data.begin(), data.begin() + 2);
  data.erase(data.end() - 2, data.end());
  return true;
}

uint8_t BambuNfc::read_response_length_() {
  std::vector<uint8_t> data;
  if (!this->read_data(data, 6))
    return 0;

  if (data[1] != 0x00 || data[2] != 0x00 || data[3] != 0xFF)
    return 0;

  bool valid_header = (static_cast<uint8_t>(data[4] + data[5]) == 0 && data[6] == 0xD5);
  if (!valid_header)
    return 0;

  this->send_nack_();

  uint8_t full_len = data[4];
  uint8_t len = full_len - 1;
  if (full_len == 0)
    len = 0;
  return len;
}

void BambuNfc::dump_config() {
  PN532::dump_config();
  LOG_I2C_DEVICE(this);
}

// ---- HKDF Key Derivation ----

// Manual HKDF-SHA256 implementation (mbedtls_hkdf not enabled by default in ESP-IDF)
static bool hkdf_sha256(const uint8_t *salt, size_t salt_len,
                        const uint8_t *ikm, size_t ikm_len,
                        const uint8_t *info, size_t info_len,
                        uint8_t *okm, size_t okm_len) {
  const mbedtls_md_info_t *md = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
  if (md == nullptr)
    return false;

  // Extract: PRK = HMAC-SHA256(salt, IKM)
  uint8_t prk[32];
  if (mbedtls_md_hmac(md, salt, salt_len, ikm, ikm_len, prk) != 0)
    return false;

  // Expand: T(i) = HMAC-SHA256(PRK, T(i-1) || info || i)
  uint8_t t[32];
  size_t t_len = 0;
  size_t offset = 0;
  uint8_t counter = 1;

  while (offset < okm_len) {
    mbedtls_md_context_t ctx;
    mbedtls_md_init(&ctx);
    if (mbedtls_md_setup(&ctx, md, 1) != 0) {
      mbedtls_md_free(&ctx);
      return false;
    }
    mbedtls_md_hmac_starts(&ctx, prk, 32);
    if (t_len > 0)
      mbedtls_md_hmac_update(&ctx, t, t_len);
    mbedtls_md_hmac_update(&ctx, info, info_len);
    mbedtls_md_hmac_update(&ctx, &counter, 1);
    mbedtls_md_hmac_finish(&ctx, t);
    mbedtls_md_free(&ctx);

    t_len = 32;
    size_t copy_len = (okm_len - offset < 32) ? okm_len - offset : 32;
    memcpy(okm + offset, t, copy_len);
    offset += copy_len;
    counter++;
  }
  return true;
}

bool BambuNfc::derive_bambu_keys_(const uint8_t *uid, size_t uid_len, uint8_t *keys_out) {
  if (!hkdf_sha256(BAMBU_MASTER_KEY, sizeof(BAMBU_MASTER_KEY),
                   uid, uid_len,
                   BAMBU_HKDF_INFO, sizeof(BAMBU_HKDF_INFO),
                   keys_out, 96)) {
    ESP_LOGE(TAG, "HKDF key derivation failed");
    return false;
  }
  return true;
}

// ---- Reset Button ----

void BambuNfcResetButton::press_action() {
  this->parent_->clear_sensors();
}

void BambuNfc::clear_sensors() {
  text_sensor::TextSensor *text_sensors[] = {
      filament_type_sensor_, filament_color_sensor_, filament_color_name_sensor_,
      tray_uid_sensor_, tray_info_idx_sensor_, production_date_sensor_, last_scan_date_sensor_};
  for (auto *s : text_sensors)
    if (s) s->publish_state("");

  sensor::Sensor *num_sensors[] = {
      min_temp_sensor_, max_temp_sensor_, bed_temp_sensor_, spool_weight_sensor_,
      filament_diameter_sensor_, drying_temp_sensor_, drying_time_sensor_,
      nozzle_diameter_sensor_, spool_width_sensor_, filament_length_sensor_,
      material_density_sensor_};
  for (auto *s : num_sensors)
    if (s) s->publish_state(NAN);

  this->current_uid_ = {};
  ESP_LOGD(TAG, "Sensors cleared");
}

// ---- Loop Override ----

void BambuNfc::loop() {
  if (!this->requested_read_)
    return;

  auto ready = this->read_ready_(false);
  if (ready == pn532::WOULDBLOCK)
    return;

  bool success = false;
  std::vector<uint8_t> read;

  if (ready == pn532::READY) {
    success = this->read_response(pn532::PN532_COMMAND_INLISTPASSIVETARGET, read);
  } else {
    this->send_ack_();
  }

  this->requested_read_ = false;

  if (!success) {
    if (!this->current_uid_.empty()) {
      auto tag = make_unique<nfc::NfcTag>(this->current_uid_);
      for (auto *trigger : this->triggers_ontagremoved_)
        trigger->process(tag);
    }
    this->current_uid_ = {};
    this->turn_off_rf_();
    return;
  }

  uint8_t num_targets = read[0];
  if (num_targets != 1) {
    if (!this->current_uid_.empty()) {
      auto tag = make_unique<nfc::NfcTag>(this->current_uid_);
      for (auto *trigger : this->triggers_ontagremoved_)
        trigger->process(tag);
    }
    this->current_uid_ = {};
    this->turn_off_rf_();
    return;
  }

  uint8_t nfcid_length = read[5];
  if (nfcid_length > nfc::NFC_UID_MAX_LENGTH || read.size() < 6U + nfcid_length)
    return;

  nfc::NfcTagUid nfcid(read.begin() + 6, read.begin() + 6 + nfcid_length);

  for (auto *bin_sens : this->binary_sensors_)
    bin_sens->process(nfcid);

  if (nfcid.size() == this->current_uid_.size()) {
    bool same_uid = true;
    for (size_t i = 0; i < nfcid.size(); i++)
      same_uid &= nfcid[i] == this->current_uid_[i];
    if (same_uid)
      return;
  }

  this->current_uid_ = nfcid;

  if (this->next_task_ == READ) {
    char uid_buf[nfc::FORMAT_UID_BUFFER_SIZE];
    ESP_LOGD(TAG, "Found tag '%s'", nfc::format_uid_to(uid_buf, nfcid));

    // Read Bambu data and fire appropriate trigger
    bool success = this->read_bambu_data_(nfcid);
    if (success) {
      for (auto *trigger : this->success_triggers_)
        trigger->trigger();
    } else {
      for (auto *trigger : this->error_triggers_)
        trigger->trigger();
    }

    // Fire standard on_tag triggers
    auto tag = make_unique<nfc::NfcTag>(nfcid, nfc::MIFARE_CLASSIC);
    for (auto *trigger : this->triggers_ontag_)
      trigger->process(tag);
  }

  this->read_mode();
  this->turn_off_rf_();
}

// ---- Bambu Data Reading ----

static std::string trim_string(const std::vector<uint8_t> &data) {
  std::string s(data.begin(), data.end());
  size_t end = s.find_last_not_of(std::string("\0 ", 2));
  if (end == std::string::npos)
    return "";
  return s.substr(0, end + 1);
}

static void publish_text(text_sensor::TextSensor *s, const std::string &val) {
  if (s != nullptr) s->publish_state(val);
}

static void publish_num(sensor::Sensor *s, float val) {
  if (s != nullptr) s->publish_state(val);
}

static std::string format_hex(const std::vector<uint8_t> &data, size_t max_len = 16) {
  char buf[33];
  size_t len = data.size() > max_len ? max_len : data.size();
  for (size_t i = 0; i < len; i++)
    snprintf(buf + i * 2, 3, "%02X", data[i]);
  buf[len * 2] = '\0';
  return std::string(buf);
}

static float read_float_le(const std::vector<uint8_t> &data, size_t offset) {
  float val;
  memcpy(&val, &data[offset], 4);
  return val;
}

static uint16_t read_uint16_le(const std::vector<uint8_t> &data, size_t offset) {
  return data[offset] | (data[offset + 1] << 8);
}

bool BambuNfc::read_bambu_data_(nfc::NfcTagUid &uid) {
  if (uid.size() != 4) {
    ESP_LOGW(TAG, "Non-4-byte UID, not a Bambu tag");
    return false;
  }

  uint8_t keys[96];
  if (!this->derive_bambu_keys_(uid.data(), uid.size(), keys)) {
    ESP_LOGE(TAG, "Failed to derive Bambu keys");
    return false;
  }

  ESP_LOGD(TAG, "Bambu keys derived, reading tag data...");

  // ---- Phase 1: Read all blocks into local storage ----
  std::vector<uint8_t> b1, b2, b4, b5, b6, b8, b9, b10, b12, b14;

  // Sector 0 (keys[0..5]): blocks 1, 2
  if (!this->auth_mifare_classic_block_(uid, 1, nfc::MIFARE_CMD_AUTH_A, &keys[0])) {
    ESP_LOGE(TAG, "Auth failed sector 0 - not a Bambu tag?");
    return false;
  }
  if (!this->read_mifare_classic_block_(1, b1) || !this->read_mifare_classic_block_(2, b2)) {
    ESP_LOGE(TAG, "Read failed sector 0");
    return false;
  }

  // Sector 1 (keys[6..11]): blocks 4, 5, 6
  if (!this->auth_mifare_classic_block_(uid, 4, nfc::MIFARE_CMD_AUTH_A, &keys[6])) {
    ESP_LOGE(TAG, "Auth failed sector 1");
    return false;
  }
  if (!this->read_mifare_classic_block_(4, b4) || !this->read_mifare_classic_block_(5, b5) ||
      !this->read_mifare_classic_block_(6, b6)) {
    ESP_LOGE(TAG, "Read failed sector 1");
    return false;
  }

  // Sector 2 (keys[12..17]): blocks 8, 9, 10
  if (!this->auth_mifare_classic_block_(uid, 8, nfc::MIFARE_CMD_AUTH_A, &keys[12])) {
    ESP_LOGE(TAG, "Auth failed sector 2");
    return false;
  }
  if (!this->read_mifare_classic_block_(8, b8) || !this->read_mifare_classic_block_(9, b9) ||
      !this->read_mifare_classic_block_(10, b10)) {
    ESP_LOGE(TAG, "Read failed sector 2");
    return false;
  }

  // Sector 3 (keys[18..23]): blocks 12, 14
  if (!this->auth_mifare_classic_block_(uid, 12, nfc::MIFARE_CMD_AUTH_A, &keys[18])) {
    ESP_LOGE(TAG, "Auth failed sector 3");
    return false;
  }
  if (!this->read_mifare_classic_block_(12, b12) || !this->read_mifare_classic_block_(14, b14)) {
    ESP_LOGE(TAG, "Read failed sector 3");
    return false;
  }

  // ---- Phase 2: All reads OK — parse and publish atomically ----
  ESP_LOGD(TAG, "All sectors read successfully, publishing...");

  // Block 1: Tray Info Index
  std::string tray_info = trim_string(b1);
  ESP_LOGD(TAG, "Tray info idx: %s", tray_info.c_str());
  publish_text(tray_info_idx_sensor_, tray_info);

  // Block 2: Filament type
  ESP_LOGD(TAG, "Filament type: %s", trim_string(b2).c_str());

  // Block 4: Detailed filament type
  std::string detailed_type = trim_string(b4);
  ESP_LOGD(TAG, "Detailed type: %s", detailed_type.c_str());
  publish_text(filament_type_sensor_, detailed_type);

  // Material density
  float density = find_bambu_density(detailed_type.c_str());
  ESP_LOGD(TAG, "Material density: %.2f g/cm3", density);
  publish_num(material_density_sensor_, density);

  // Block 5: Color (4B) + Weight (2B) + pad (2B) + Diameter (4B)
  if (b5.size() >= 12) {
    char color_hex[8];
    snprintf(color_hex, sizeof(color_hex), "#%02X%02X%02X", b5[0], b5[1], b5[2]);
    ESP_LOGD(TAG, "Color: %s", color_hex);
    publish_text(filament_color_sensor_, std::string(color_hex));

    // Color name lookup (strip # from hex for lookup)
    const char *color_name = find_bambu_color_name(detailed_type.c_str(), color_hex + 1);
    if (color_name) {
      ESP_LOGD(TAG, "Color name: %s", color_name);
      publish_text(filament_color_name_sensor_, std::string(color_name));
    } else {
      ESP_LOGD(TAG, "No color match for %s / %s", detailed_type.c_str(), color_hex + 1);
      publish_text(filament_color_name_sensor_, std::string(color_hex));
    }

    uint16_t weight = read_uint16_le(b5, 4);
    ESP_LOGD(TAG, "Spool weight: %d g", weight);
    publish_num(spool_weight_sensor_, weight);

    float diameter = read_float_le(b5, 8);
    ESP_LOGD(TAG, "Filament diameter: %.2f mm", diameter);
    publish_num(filament_diameter_sensor_, diameter);
  }

  // Block 6: Drying temp/time + Bed temp + Hotend min/max
  if (b6.size() >= 12) {
    uint16_t drying_temp = read_uint16_le(b6, 0);
    uint16_t drying_time = read_uint16_le(b6, 2);
    uint16_t bed_temp = read_uint16_le(b6, 6);
    uint16_t max_temp = read_uint16_le(b6, 8);
    uint16_t min_temp = read_uint16_le(b6, 10);

    ESP_LOGD(TAG, "Drying: %d°C/%dh, Bed: %d°C, Hotend: %d-%d°C",
             drying_temp, drying_time, bed_temp, min_temp, max_temp);

    publish_num(drying_temp_sensor_, drying_temp);
    publish_num(drying_time_sensor_, drying_time);
    publish_num(bed_temp_sensor_, bed_temp);
    publish_num(max_temp_sensor_, max_temp);
    publish_num(min_temp_sensor_, min_temp);
  }

  // Block 8: Nozzle diameter
  if (b8.size() >= 16) {
    float nozzle = read_float_le(b8, 12);
    ESP_LOGD(TAG, "Nozzle diameter: %.2f mm", nozzle);
    publish_num(nozzle_diameter_sensor_, nozzle);
  }

  // Block 9: Tray UID
  std::string tray_uid = format_hex(b9);
  ESP_LOGD(TAG, "Tray UID: %s", tray_uid.c_str());
  publish_text(tray_uid_sensor_, tray_uid);

  // Block 10: Spool width
  if (b10.size() >= 6) {
    float spool_width = read_uint16_le(b10, 4) / 100.0f;
    ESP_LOGD(TAG, "Spool width: %.1f mm", spool_width);
    publish_num(spool_width_sensor_, spool_width);
  }

  // Block 12: Production date (format: "year_month_day_hour_minute" → "YYYY-MM-DD")
  std::string prod_date_raw = trim_string(b12);
  ESP_LOGD(TAG, "Production date raw: %s", prod_date_raw.c_str());
  std::string prod_date = prod_date_raw;
  if (prod_date_raw.size() >= 10 && prod_date_raw[4] == '_' && prod_date_raw[7] == '_') {
    prod_date = prod_date_raw.substr(0, 4) + "-" + prod_date_raw.substr(5, 2) + "-" + prod_date_raw.substr(8, 2);
  }
  publish_text(production_date_sensor_, prod_date);

  // Block 14: Filament length
  if (b14.size() >= 6) {
    uint16_t length = read_uint16_le(b14, 4);
    ESP_LOGD(TAG, "Filament length: %d m", length);
    publish_num(filament_length_sensor_, length);
  }

  // Last scan timestamp
  if (last_scan_date_sensor_ != nullptr) {
    time_t now_ts = ::time(nullptr);
    struct tm timeinfo;
    localtime_r(&now_ts, &timeinfo);
    if (timeinfo.tm_year > 100) {
      char buf[32];
      size_t len = strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", &timeinfo);
      // Append timezone offset
      strftime(buf + len, sizeof(buf) - len, "%z", &timeinfo);
      last_scan_date_sensor_->publish_state(std::string(buf));
    }
  }
  return true;
}

}  // namespace bambu_nfc
}  // namespace esphome
