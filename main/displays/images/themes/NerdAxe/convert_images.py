"""
Convert PNG images to LVGL LV_IMG_CF_TRUE_COLOR_ALPHA C source files.
Format: 3 bytes per pixel [low_rgb565, high_rgb565, alpha]
"""

from PIL import Image
import os

BASE_DIR = os.path.dirname(os.path.abspath(__file__))
RAW_DIR = os.path.join(BASE_DIR, "Raw Images")

# (filename_stem, target_w, target_h) — matches existing .c file dimensions
IMAGES = [
    ("btcscreen",     321, 171),
    ("globalStats",   321, 170),
    ("initscreen2",   320, 170),
    ("miningscreen2", 320, 170),
    ("portalscreen",  321, 169),
    ("settingsscreen",320, 170),
    ("splashscreen2", 320, 170),
]


def rgb_to_rgb565_bytes(r, g, b):
    """Return (high_byte, low_byte) of RGB565 in big-endian order (as SquareLine Studio outputs)."""
    packed = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)
    return (packed >> 8) & 0xFF, packed & 0xFF


def convert(stem, target_w, target_h):
    src = os.path.join(RAW_DIR, f"{stem}.png")
    dst = os.path.join(BASE_DIR, f"ui_img_{stem}_png.c")

    img = Image.open(src).convert("RGBA")
    img = img.resize((target_w, target_h), Image.LANCZOS)
    pixels = list(img.getdata())

    data = []
    for r, g, b, a in pixels:
        hi, lo = rgb_to_rgb565_bytes(r, g, b)
        data.extend([hi, lo, a])

    # Format data as rows of 16 bytes
    rows = []
    for i in range(0, len(data), 16):
        chunk = data[i:i + 16]
        rows.append("    " + ", ".join(f"0x{v:02X}" for v in chunk) + ", ")

    var_base = f"ui_img_NerdAxe_{stem}_png"

    c_src = f"""#include "lvgl.h"

#ifndef LV_ATTRIBUTE_MEM_ALIGN
    #define LV_ATTRIBUTE_MEM_ALIGN
#endif

// IMAGE DATA: {stem}.png
const LV_ATTRIBUTE_MEM_ALIGN uint8_t {var_base}_data[] = {{
{chr(10).join(rows)}
}};

const lv_img_dsc_t {var_base} = {{
    .header.always_zero = 0,
    .header.w = {target_w},
    .header.h = {target_h},
    .data_size = sizeof({var_base}_data),
    .header.cf = LV_IMG_CF_TRUE_COLOR_ALPHA,
    .data = {var_base}_data
}};
"""

    with open(dst, "w", newline="\n") as f:
        f.write(c_src)

    print(f"  {stem}.png -> {os.path.basename(dst)}  ({target_w}x{target_h}, {len(data)} bytes)")


if __name__ == "__main__":
    print("Converting images...")
    for stem, w, h in IMAGES:
        convert(stem, w, h)
    print("Done.")
