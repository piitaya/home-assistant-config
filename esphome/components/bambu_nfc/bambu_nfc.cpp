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
  if (filament_type_sensor_) filament_type_sensor_->publish_state("");
  if (filament_color_sensor_) filament_color_sensor_->publish_state("");
  if (tray_uid_sensor_) tray_uid_sensor_->publish_state("");
  if (production_date_sensor_) production_date_sensor_->publish_state("");
  if (last_scan_date_sensor_) last_scan_date_sensor_->publish_state("");
  if (min_temp_sensor_) min_temp_sensor_->publish_state(NAN);
  if (max_temp_sensor_) max_temp_sensor_->publish_state(NAN);
  if (bed_temp_sensor_) bed_temp_sensor_->publish_state(NAN);
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
    // Read Bambu data before turning off RF
    this->read_bambu_data_(nfcid);

    // Fire standard on_tag triggers
    auto tag = make_unique<nfc::NfcTag>(nfcid, nfc::MIFARE_CLASSIC);
    for (auto *trigger : this->triggers_ontag_)
      trigger->process(tag);

    char uid_buf[nfc::FORMAT_UID_BUFFER_SIZE];
    ESP_LOGD(TAG, "Found tag '%s'", nfc::format_uid_to(uid_buf, nfcid));
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

void BambuNfc::read_bambu_data_(nfc::NfcTagUid &uid) {
  if (uid.size() != 4) {
    ESP_LOGW(TAG, "Non-4-byte UID, not a Bambu tag");
    return;
  }

  uint8_t keys[96];
  if (!this->derive_bambu_keys_(uid.data(), uid.size(), keys)) {
    ESP_LOGE(TAG, "Failed to derive Bambu keys");
    return;
  }

  ESP_LOGD(TAG, "Bambu keys derived, reading tag data...");

  // Sector 0 (key at keys[0..5]): blocks 1, 2
  if (this->auth_mifare_classic_block_(uid, 1, nfc::MIFARE_CMD_AUTH_A, &keys[0])) {
    std::vector<uint8_t> block2;
    if (this->read_mifare_classic_block_(2, block2)) {
      std::string type = trim_string(block2);
      ESP_LOGD(TAG, "Filament type: %s", type.c_str());
    }
  } else {
    ESP_LOGE(TAG, "Auth failed sector 0 - not a Bambu tag?");
    return;
  }

  // Sector 1 (key at keys[6..11]): blocks 4, 5, 6
  if (this->auth_mifare_classic_block_(uid, 4, nfc::MIFARE_CMD_AUTH_A, &keys[6])) {
    std::vector<uint8_t> block4, block5, block6;

    // Block 4: Detailed filament type
    if (this->read_mifare_classic_block_(4, block4)) {
      std::string detailed_type = trim_string(block4);
      ESP_LOGD(TAG, "Detailed type: %s", detailed_type.c_str());
      if (filament_type_sensor_ != nullptr)
        filament_type_sensor_->publish_state(detailed_type);
    }

    // Block 5: Color RGBA (4B) + pad (4B) + Diameter float LE (4B)
    if (this->read_mifare_classic_block_(5, block5) && block5.size() >= 4) {
      char color_hex[8];
      snprintf(color_hex, sizeof(color_hex), "#%02X%02X%02X", block5[0], block5[1], block5[2]);
      ESP_LOGD(TAG, "Color: %s", color_hex);
      if (filament_color_sensor_ != nullptr)
        filament_color_sensor_->publish_state(std::string(color_hex));
    }

    // Block 6: Drying temp (2) + Drying time (2) + Bed temp type (2) + Bed temp (2) + Max temp (2) + Min temp (2)
    if (this->read_mifare_classic_block_(6, block6) && block6.size() >= 12) {
      uint16_t bed_temp = block6[6] | (block6[7] << 8);
      uint16_t max_temp = block6[8] | (block6[9] << 8);
      uint16_t min_temp = block6[10] | (block6[11] << 8);

      ESP_LOGD(TAG, "Bed: %d°C, Hotend: %d-%d°C", bed_temp, min_temp, max_temp);

      if (bed_temp_sensor_ != nullptr)
        bed_temp_sensor_->publish_state(bed_temp);
      if (max_temp_sensor_ != nullptr)
        max_temp_sensor_->publish_state(max_temp);
      if (min_temp_sensor_ != nullptr)
        min_temp_sensor_->publish_state(min_temp);
    }
  } else {
    ESP_LOGE(TAG, "Auth failed sector 1 - tag halted, will retry");
    return;
  }

  // Sector 2 (key at keys[12..17]): block 9
  if (this->auth_mifare_classic_block_(uid, 9, nfc::MIFARE_CMD_AUTH_A, &keys[12])) {
    std::vector<uint8_t> block9;
    if (this->read_mifare_classic_block_(9, block9)) {
      char hex_buf[33];
      size_t len = block9.size() > 16 ? 16 : block9.size();
      for (size_t i = 0; i < len; i++)
        snprintf(hex_buf + i * 2, 3, "%02X", block9[i]);
      hex_buf[len * 2] = '\0';
      std::string tray_uid(hex_buf);
      ESP_LOGD(TAG, "Tray UID: %s", tray_uid.c_str());
      if (tray_uid_sensor_ != nullptr)
        tray_uid_sensor_->publish_state(tray_uid);
    }
  } else {
    ESP_LOGE(TAG, "Auth failed sector 2 - tag halted, will retry");
    return;
  }

  // Sector 3 (key at keys[18..23]): block 12
  if (this->auth_mifare_classic_block_(uid, 12, nfc::MIFARE_CMD_AUTH_A, &keys[18])) {
    std::vector<uint8_t> block12;
    if (this->read_mifare_classic_block_(12, block12)) {
      std::string prod_date = trim_string(block12);
      ESP_LOGD(TAG, "Production date: %s", prod_date.c_str());
      if (production_date_sensor_ != nullptr)
        production_date_sensor_->publish_state(prod_date);
    }
  } else {
    ESP_LOGE(TAG, "Auth failed sector 3 - tag halted, will retry");
    return;
  }

  // Record last scan timestamp
  if (last_scan_date_sensor_ != nullptr) {
    time_t now_ts = ::time(nullptr);
    struct tm timeinfo;
    localtime_r(&now_ts, &timeinfo);
    if (timeinfo.tm_year > 100) {
      char buf[20];
      strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M", &timeinfo);
      last_scan_date_sensor_->publish_state(std::string(buf));
    }
  }
}

}  // namespace bambu_nfc
}  // namespace esphome
