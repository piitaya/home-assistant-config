#!/usr/bin/env python3
"""Genere bambu_colors.h depuis SpoolmanDB."""

import json
import urllib.request

URL = "https://raw.githubusercontent.com/Donkie/SpoolmanDB/main/filaments/bambulab.json"


def fetch_and_generate():
    with urllib.request.urlopen(URL) as resp:
        data = json.loads(resp.read())

    entries = []
    seen = set()
    for fil in data["filaments"]:
        material = fil["material"]
        name_tpl = fil.get("name", "{color_name}")
        finish = fil.get("finish", "")
        glow = fil.get("glow", False)
        translucent = fil.get("translucent", False)
        pattern = fil.get("pattern", "")
        multi = fil.get("multi_color_direction", "")

        if "-" in material:
            dt = material
        elif finish == "matte":
            dt = f"{material} Matte"
        elif glow:
            dt = f"{material} Glow"
        elif translucent:
            dt = f"{material} Translucent"
        elif pattern == "galaxy":
            dt = f"{material} Galaxy"
        elif pattern == "sparkle":
            dt = f"{material} Sparkle"
        elif finish == "glossy" and "Silk" in name_tpl:
            dt = f"{material} Silk"
        elif finish == "glossy" and "Marble" in name_tpl:
            dt = f"{material} Marble"
        elif multi:
            dt = f"{material} Dual Color"
        else:
            dt = f"{material} Basic"

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
            key = f"{dt}|{hx}"
            if key in seen:
                continue
            seen.add(key)
            entries.append((dt, hx, display))

    entries.sort()

    lines = [
        "#pragma once",
        "",
        "#include <cstring>",
        "",
        "namespace esphome {",
        "namespace bambu_nfc {",
        "",
        "struct BambuColorEntry {",
        "  const char *detailed_type;",
        "  const char *hex;",
        "  const char *color_name;",
        "};",
        "",
        "static const BambuColorEntry BAMBU_COLORS[] = {",
    ]
    for dt, hx, name in entries:
        name_esc = name.replace('"', '\\"')
        lines.append(f'  {{"{dt}", "{hx}", "{name_esc}"}},')
    lines.append("};")
    lines.append("")
    lines.append(f"static const size_t BAMBU_COLORS_COUNT = {len(entries)};")
    lines.append("")
    lines.append(
        "inline const char *find_bambu_color_name(const char *detailed_type, const char *hex) {"
    )
    lines.append("  for (size_t i = 0; i < BAMBU_COLORS_COUNT; i++) {")
    lines.append(
        "    if (strcasecmp(BAMBU_COLORS[i].detailed_type, detailed_type) == 0 &&"
    )
    lines.append("        strcasecmp(BAMBU_COLORS[i].hex, hex) == 0)")
    lines.append("      return BAMBU_COLORS[i].color_name;")
    lines.append("  }")
    lines.append("  return nullptr;")
    lines.append("}")
    lines.append("")
    lines.append("}  // namespace bambu_nfc")
    lines.append("}  // namespace esphome")
    lines.append("")

    with open("bambu_colors.h", "w") as f:
        f.write("\n".join(lines))

    print(f"bambu_colors.h: {len(entries)} couleurs generees")


if __name__ == "__main__":
    fetch_and_generate()
