#!/usr/bin/env python3
"""Convert PNGs to LVGL LV_IMG_CF_TRUE_COLOR_ALPHA C arrays."""

import os
from PIL import Image

THEME = "NerdQaxePlus2"
RAW_DIR = os.path.join(os.path.dirname(__file__), "Raw Images")
OUT_DIR = os.path.dirname(__file__)

PNGS = [f for f in os.listdir(RAW_DIR) if f.endswith(".png")]

def to_rgb565_alpha(img):
    img = img.convert("RGBA")
    out = bytearray()
    for r, g, b, a in img.getdata():
        r5 = r >> 3
        g6 = g >> 2
        b5 = b >> 3
        rgb565 = (r5 << 11) | (g6 << 5) | b5
        out.append((rgb565 >> 8) & 0xFF)  # high byte first (big-endian, matches LVGL display)
        out.append(rgb565 & 0xFF)
        out.append(a)
    return bytes(out)

for fname in sorted(PNGS):
    stem = os.path.splitext(fname)[0]
    var_name = f"ui_img_{THEME}_{stem}_png"
    img = Image.open(os.path.join(RAW_DIR, fname))
    w, h = img.size
    data = to_rgb565_alpha(img)

    lines = ["#include \"lvgl.h\"",
             "",
             "#ifndef LV_ATTRIBUTE_MEM_ALIGN",
             "    #define LV_ATTRIBUTE_MEM_ALIGN",
             "#endif",
             "",
             f"// IMAGE DATA: {fname}",
             f"const LV_ATTRIBUTE_MEM_ALIGN uint8_t {var_name}_data[] = {{"]

    row = []
    for i, byte in enumerate(data):
        row.append(f"0x{byte:02X}")
        if len(row) == 16:
            lines.append("    " + ", ".join(row) + ", ")
            row = []
    if row:
        lines.append("    " + ", ".join(row) + ", ")

    lines += ["",
              "};",
              "",
              f"const lv_img_dsc_t {var_name} = {{",
              "    .header.always_zero = 0,",
              f"    .header.w = {w},",
              f"    .header.h = {h},",
              f"    .data_size = sizeof({var_name}_data),",
              "    .header.cf = LV_IMG_CF_TRUE_COLOR_ALPHA,",
              f"    .data = {var_name}_data",
              "};"]

    out_path = os.path.join(OUT_DIR, f"ui_img_{stem}_png.c")
    with open(out_path, "w") as f:
        f.write("\n".join(lines) + "\n")
    print(f"Written: ui_img_{stem}_png.c  ({w}x{h}, {len(data)} bytes)")

print("Done.")
