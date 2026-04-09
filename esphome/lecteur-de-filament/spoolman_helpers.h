#pragma once
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include "lvgl.h"
#include "spoolman_catalog.h"

namespace spoolman_helpers {

// Density fallback by material family for variants without a spoolman_id.
// Delegates to the generated table (sourced from SpoolmanDB materials.json)
// which does a longest-prefix match on the material name.
inline float default_density(const char *material) {
  return spoolman_catalog::material_density(material);
}


// RFC 3986 URL encoding for query string components.
// Encodes everything except unreserved chars [A-Za-z0-9-._~].
inline std::string url_encode(const std::string &s) {
  std::string out;
  out.reserve(s.size() * 3);
  char buf[4];
  for (unsigned char c : s) {
    if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
        c == '-' || c == '_' || c == '.' || c == '~') {
      out += static_cast<char>(c);
    } else {
      snprintf(buf, sizeof(buf), "%%%02X", c);
      out += buf;
    }
  }
  return out;
}

// Searches for `"<key>":` followed by an optional space, then parses the
// value as a float. Returns `default_value` if the field is not found.
// The exact `"<key>":` bracketing avoids matching substrings.
inline float json_field_float(const std::string &json, const char *key,
                              float default_value) {
  std::string needle = "\"";
  needle += key;
  needle += "\":";
  size_t pos = json.find(needle);
  if (pos == std::string::npos) return default_value;
  pos += needle.size();
  while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t')) pos++;
  if (pos >= json.size()) return default_value;
  char *end = nullptr;
  float v = std::strtof(json.c_str() + pos, &end);
  if (end == json.c_str() + pos) return default_value;
  return v;
}

// Parses a JSON boolean field (true/false).
inline bool json_field_bool(const std::string &json, const char *key,
                            bool default_value) {
  std::string needle = "\"";
  needle += key;
  needle += "\":";
  size_t pos = json.find(needle);
  if (pos == std::string::npos) return default_value;
  pos += needle.size();
  while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t')) pos++;
  if (pos + 4 <= json.size() && json.compare(pos, 4, "true") == 0) return true;
  if (pos + 5 <= json.size() && json.compare(pos, 5, "false") == 0) return false;
  return default_value;
}

// Updates both the main and detail "remaining" labels with the same text and
// applies the contrast color. The labels and color luminance are passed as
// arguments so this header has no compile-time dependency on YAML-defined ids.
inline void set_remaining_label(lv_obj_t *main_label, lv_obj_t *detail_label,
                                const char *text, float color_lum) {
  uint32_t fg = color_lum > 0.5f ? 0x000000 : 0xFFFFFF;
  lv_label_set_text(main_label, text);
  lv_obj_set_style_text_color(main_label, lv_color_hex(fg), 0);
  lv_label_set_text(detail_label, text);
}

}  // namespace spoolman_helpers
