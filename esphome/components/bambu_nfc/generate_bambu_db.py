#!/usr/bin/env python3
"""Genere bambu_colors.h et bambu_densities.h depuis SpoolmanDB."""

import json
import urllib.request

URL = "https://raw.githubusercontent.com/Donkie/SpoolmanDB/main/filaments/bambulab.json"


def determine_detailed_type(fil):
    material = fil["material"]
    name_tpl = fil.get("name", "{color_name}")
    finish = fil.get("finish", "")
    glow = fil.get("glow", False)
    translucent = fil.get("translucent", False)
    pattern = fil.get("pattern", "")
    multi = fil.get("multi_color_direction", "")

    if "-" in material:
        return material
    if finish == "matte":
        return f"{material} Matte"
    if glow:
        return f"{material} Glow"
    if translucent:
        return f"{material} Translucent"
    if pattern == "galaxy":
        return f"{material} Galaxy"
    if pattern == "sparkle":
        return f"{material} Sparkle"
    if finish == "glossy" and "Silk" in name_tpl:
        return f"{material} Silk"
    if finish == "glossy" and "Marble" in name_tpl:
        return f"{material} Marble"
    if multi:
        return f"{material} Dual Color"
    return f"{material} Basic"


def fetch_and_generate():
    print(f"Fetching {URL}...")
    with urllib.request.urlopen(URL) as resp:
        data = json.loads(resp.read())

    # First pass: collect all entries, mark support/variant names
    all_entries = []  # (detailed_type, hex, display_name, is_variant)
    densities = {}

    for fil in data["filaments"]:
        dt = determine_detailed_type(fil)
        name_tpl = fil.get("name", "{color_name}")
        density = fil.get("density", 1.24)

        if dt not in densities:
            densities[dt] = density

        for color in fil.get("colors", []):
            cn = color["name"]
            hx = color.get("hex", "")
            if not hx:
                hexes = color.get("hexes", [])
                if hexes:
                    hx = hexes[0]
            if not hx:
                continue
            hx = hx.upper()
            display = name_tpl.replace("{color_name}", cn)
            is_variant = display.startswith("Support") or display.startswith("Tough+") or display.startswith("For AMS")
            all_entries.append((dt, hx, display, is_variant))

    # Second pass: deduplicate, prefer normal names over variants
    colors = []
    seen = {}  # key -> (display, is_variant)

    for dt, hx, display, is_variant in all_entries:
        key = f"{dt}|{hx}"
        if key in seen:
            prev_display, prev_is_variant = seen[key]
            # Replace variant with normal name
            if prev_is_variant and not is_variant:
                colors = [(d, h, n) for d, h, n in colors if f"{d}|{h}" != key]
                colors.append((dt, hx, display))
                seen[key] = (display, is_variant)
        else:
            seen[key] = (display, is_variant)
            colors.append((dt, hx, display))

    colors.sort()

    # Write bambu_colors.h
    with open("bambu_colors.h", "w") as f:
        f.write("#pragma once\n\n#include <cstring>\n\n")
        f.write("namespace esphome {\nnamespace bambu_nfc {\n\n")
        f.write("struct BambuColorEntry {\n  const char *detailed_type;\n")
        f.write("  const char *hex;\n  const char *color_name;\n};\n\n")
        f.write("static const BambuColorEntry BAMBU_COLORS[] = {\n")
        for dt, hx, name in colors:
            name_esc = name.replace('"', '\\"')
            f.write(f'  {{"{dt}", "{hx}", "{name_esc}"}},\n')
        f.write("};\n\n")
        f.write(f"static const size_t BAMBU_COLORS_COUNT = {len(colors)};\n\n")
        f.write(
            "inline const char *find_bambu_color_name(const char *detailed_type, const char *hex) {\n"
        )
        f.write("  for (size_t i = 0; i < BAMBU_COLORS_COUNT; i++) {\n")
        f.write(
            "    if (strcasecmp(BAMBU_COLORS[i].detailed_type, detailed_type) == 0 &&\n"
        )
        f.write("        strcasecmp(BAMBU_COLORS[i].hex, hex) == 0)\n")
        f.write("      return BAMBU_COLORS[i].color_name;\n")
        f.write("  }\n  return nullptr;\n}\n\n")
        f.write("}  // namespace bambu_nfc\n}  // namespace esphome\n")

    # Write bambu_densities.h
    sorted_densities = sorted(densities.items())
    with open("bambu_densities.h", "w") as f:
        f.write("#pragma once\n\n#include <cstring>\n\n")
        f.write("namespace esphome {\nnamespace bambu_nfc {\n\n")
        f.write(
            "struct DensityEntry {\n  const char *detailed_type;\n  float density;\n};\n\n"
        )
        f.write("static const DensityEntry BAMBU_DENSITIES[] = {\n")
        for dt, d in sorted_densities:
            f.write(f'  {{"{dt}", {d}f}},\n')
        f.write("};\n\n")
        f.write(
            f"static const size_t BAMBU_DENSITIES_COUNT = {len(sorted_densities)};\n\n"
        )
        f.write(
            "inline float find_bambu_density(const char *detailed_type) {\n"
        )
        f.write("  for (size_t i = 0; i < BAMBU_DENSITIES_COUNT; i++) {\n")
        f.write(
            "    if (strcasecmp(BAMBU_DENSITIES[i].detailed_type, detailed_type) == 0)\n"
        )
        f.write("      return BAMBU_DENSITIES[i].density;\n")
        f.write("  }\n")
        f.write("  const char *space = strchr(detailed_type, ' ');\n")
        f.write("  if (space) {\n")
        f.write("    size_t base_len = space - detailed_type;\n")
        f.write("    for (size_t i = 0; i < BAMBU_DENSITIES_COUNT; i++) {\n")
        f.write(
            "      if (strncasecmp(BAMBU_DENSITIES[i].detailed_type, detailed_type, base_len) == 0 &&\n"
        )
        f.write(
            "          BAMBU_DENSITIES[i].detailed_type[base_len] == ' ')\n"
        )
        f.write("        return BAMBU_DENSITIES[i].density;\n")
        f.write("    }\n  }\n")
        f.write("  return 1.24f;\n}\n\n")
        f.write("}  // namespace bambu_nfc\n}  // namespace esphome\n")

    print(f"bambu_colors.h: {len(colors)} couleurs")
    print(f"bambu_densities.h: {len(sorted_densities)} densites")


if __name__ == "__main__":
    fetch_and_generate()
