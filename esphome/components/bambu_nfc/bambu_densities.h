#pragma once

#include <cstring>

namespace esphome {
namespace bambu_nfc {

struct DensityEntry {
  const char *detailed_type;
  float density;
};

static const DensityEntry BAMBU_DENSITIES[] = {
  {"ABS Basic", 1.16f},
  {"ABS-GF", 1.08f},
  {"ASA Basic", 1.05f},
  {"ASA-CF", 1.02f},
  {"PA Basic", 1.17f},
  {"PA6-CF", 1.09f},
  {"PA6-GF", 1.09f},
  {"PAHT-CF", 1.06f},
  {"PC Basic", 1.2f},
  {"PET-CF", 1.29f},
  {"PETG Basic", 1.25f},
  {"PETG Translucent", 1.25f},
  {"PETG-CF", 1.25f},
  {"PLA Basic", 1.24f},
  {"PLA Dual Color", 1.24f},
  {"PLA Glow", 1.26f},
  {"PLA Matte", 1.31f},
  {"PLA Silk", 1.32f},
  {"PLA Sparkle", 1.19f},
  {"PLA Translucent", 1.22f},
  {"PLA+WOOD Basic", 1.21f},
  {"PLA-CF", 1.22f},
  {"PPS-CF", 1.26f},
  {"PVA Basic", 1.27f},
  {"TPU Basic", 1.22f},
  {"TPU-85A", 1.24f},
  {"TPU-90A", 1.24f},
  {"TPU-95A", 1.22f},
};

static const size_t BAMBU_DENSITIES_COUNT = 28;

inline float find_bambu_density(const char *detailed_type) {
  for (size_t i = 0; i < BAMBU_DENSITIES_COUNT; i++) {
    if (strcasecmp(BAMBU_DENSITIES[i].detailed_type, detailed_type) == 0)
      return BAMBU_DENSITIES[i].density;
  }
  const char *space = strchr(detailed_type, ' ');
  if (space) {
    size_t base_len = space - detailed_type;
    for (size_t i = 0; i < BAMBU_DENSITIES_COUNT; i++) {
      if (strncasecmp(BAMBU_DENSITIES[i].detailed_type, detailed_type, base_len) == 0 &&
          BAMBU_DENSITIES[i].detailed_type[base_len] == ' ')
        return BAMBU_DENSITIES[i].density;
    }
  }
  return 1.24f;
}

}  // namespace bambu_nfc
}  // namespace esphome
