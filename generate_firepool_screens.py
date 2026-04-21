"""
FirePool Edition - NerdQaxe+ Screen Theme Generator
Replaces pink/magenta with dark blue, swaps logos, adds FirePool Edition branding.
Run from any Python env with Pillow: pip install Pillow
"""

import colorsys
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont

# ── Paths ────────────────────────────────────────────────────────────────────
RAW_DIR   = Path(r"M:\Bitaxe Project\NerdAxe_Cluster\main\displays\images\themes\NerdQaxePlus\Raw Images")
OUT_DIR   = RAW_DIR   # overwrite in-place (originals backed up first)
CLUSTER_LOGO = Path(r"M:\Bitaxe Project\NerdAxe_Cluster\ClusterQlogo.png")
FP_LOGO      = Path(r"F:\Vault Tec Images\FPlogo_white.png")

# ── Brand colours ────────────────────────────────────────────────────────────
BG_DARK       = (5,  14,  40)    # near-black navy  #050E28
BLUE_MID      = (12, 42, 110)    # mid navy          #0C2A6E
BLUE_BRIGHT   = (26, 82, 200)    # bright accent     #1A52C8
BLUE_BAND     = (18, 55, 145)    # replaces pink band #123791
WHITE         = (255, 255, 255)
ORANGE_FIRE   = (255, 120,  20)  # FirePool accent

SCREEN_W, SCREEN_H = 320, 170

# ── Helpers ──────────────────────────────────────────────────────────────────

def best_font(size: int) -> ImageFont.ImageFont:
    """Try common bold Windows fonts, fall back to default."""
    candidates = [
        "arialbd.ttf", "Arial Bold.ttf",
        "impact.ttf", "Impact.ttf",
        "segoeuib.ttf", "calibrib.ttf",
        "C:/Windows/Fonts/arialbd.ttf",
        "C:/Windows/Fonts/impact.ttf",
        "C:/Windows/Fonts/segoeuib.ttf",
    ]
    for c in candidates:
        try:
            return ImageFont.truetype(c, size)
        except Exception:
            pass
    return ImageFont.load_default()


def replace_pink(img: Image.Image) -> Image.Image:
    """Replace pink/magenta pixels with dark-blue equivalents (hue-based).
    Catches both vivid and desaturated/light pinks. Output is capped to
    rich navy so no pixel goes near white."""
    img = img.convert("RGBA")
    pixels = img.load()
    w, h = img.size
    for y in range(h):
        for x in range(w):
            r, g, b, a = pixels[x, y]
            if a < 10:
                continue
            hv, sv, vv = colorsys.rgb_to_hsv(r / 255, g / 255, b / 255)
            hue = hv * 360
            # Pink / magenta / hot-pink range: ~270–355°
            # Lower sv threshold to also catch desaturated light pinks
            if sv > 0.18 and vv > 0.20 and 265 <= hue <= 358:
                # Map brightness 0→1 to BG_DARK → BLUE_BAND (never exceeds BLUE_BAND)
                t = min(vv, 1.0)
                nr = int(BG_DARK[0] + t * (BLUE_BAND[0] - BG_DARK[0]))
                ng = int(BG_DARK[1] + t * (BLUE_BAND[1] - BG_DARK[1]))
                nb = int(BG_DARK[2] + t * (BLUE_BAND[2] - BG_DARK[2]))
                pixels[x, y] = (nr, ng, nb, a)
    return img


def black_to_alpha(img: Image.Image, threshold: int = 40) -> Image.Image:
    """Make near-black pixels transparent (for ClusterQAxe+ logo)."""
    img = img.convert("RGBA")
    pixels = img.load()
    for y in range(img.height):
        for x in range(img.width):
            r, g, b, a = pixels[x, y]
            if r < threshold and g < threshold and b < threshold:
                pixels[x, y] = (0, 0, 0, 0)
    return img


def paste_logo(base: Image.Image, logo: Image.Image,
               x: int, y: int, w: int, h: int) -> Image.Image:
    """Resize logo to (w×h) and paste at (x, y) with alpha."""
    logo_r = logo.resize((w, h), Image.LANCZOS)
    base.paste(logo_r, (x, y), logo_r)
    return base


def draw_text_shadow(draw: ImageDraw.ImageDraw, pos, text, font,
                     fill=WHITE, shadow=(0, 0, 0)):
    """Draw text with a 1-pixel drop shadow."""
    x, y = pos
    draw.text((x + 1, y + 1), text, font=font, fill=shadow)
    draw.text((x, y), text, font=font, fill=fill)


def load_cluster_logo() -> Image.Image:
    logo = Image.open(CLUSTER_LOGO).convert("RGBA")
    return black_to_alpha(logo)


def load_fp_logo() -> Image.Image:
    return Image.open(FP_LOGO).convert("RGBA")


def open_orig(name: str) -> Image.Image:
    """Open the original backup (_orig.png) if it exists, else the live file."""
    orig = RAW_DIR / name.replace(".png", "_orig.png")
    if orig.exists():
        return Image.open(orig)
    return Image.open(RAW_DIR / name)


# ── Screen generators ────────────────────────────────────────────────────────

def make_mining_screen(cluster_logo: Image.Image) -> Image.Image:
    """miningscreen2 — main stats screen."""
    base = replace_pink(open_orig("miningscreen2.png"))

    draw = ImageDraw.Draw(base)

    # Blank out the full logo + ghost zone — the original pink "Qaxe+" trails
    # down to ~y=75 after replace_pink turns it navy, so cover the whole area
    # Flatten the old logo zone to BG_DARK — completely erases the ghost.
    # BG_DARK (5,14,40) is nearly identical to the surrounding dark background
    # so it won't produce a visible rectangle edge on the display.
    base = base.convert("RGBA")
    pixels = base.load()
    for py in range(16, 73):
        for px in range(0, 143):
            _, _, _, a = pixels[px, py]
            pixels[px, py] = (*BG_DARK, a)
    draw = ImageDraw.Draw(base)

    # Paste ClusterQAxe+ logo — below top banner, clear of fan area
    paste_logo(base, cluster_logo, x=2, y=17, w=140, h=42)

    # "FirePool Edition" tag just below logo
    font_small = best_font(9)
    draw_text_shadow(draw, (4, 62), "FirePool Edition", font_small,
                     fill=ORANGE_FIRE, shadow=(0, 0, 0))
    return base.convert("RGB")


def make_btc_screen(cluster_logo: Image.Image) -> Image.Image:
    """btcscreen — BTC price."""
    base = replace_pink(open_orig("btcscreen.png"))
    draw = ImageDraw.Draw(base)

    # Darken the light-gray accent stripe region (diagonal band + label boxes).
    # Paint a near-opaque dark navy overlay over the whole right-side stripe so
    # the gray boxes go dark. LVGL draws its own live °C/GH/s labels on top.
    overlay = Image.new("RGBA", base.size, (0, 0, 0, 0))
    odraw = ImageDraw.Draw(overlay)
    odraw.polygon(
        [(190, 0), (SCREEN_W, 0), (SCREEN_W, SCREEN_H), (155, SCREEN_H)],
        fill=(*BG_DARK, 220)   # strong overlay — kills the gray, keeps icons faintly
    )
    base = Image.alpha_composite(base.convert("RGBA"), overlay)
    draw = ImageDraw.Draw(base)

    # Small logo bottom-left
    paste_logo(base, cluster_logo, x=2, y=SCREEN_H - 28, w=110, h=24)

    font_tiny = best_font(8)
    draw_text_shadow(draw, (3, SCREEN_H - 12), "FirePool Edition", font_tiny,
                     fill=ORANGE_FIRE)
    return base.convert("RGB")


def make_settings_screen(cluster_logo: Image.Image) -> Image.Image:
    """settingsscreen — pool/freq settings."""
    base = replace_pink(open_orig("settingsscreen.png"))
    draw = ImageDraw.Draw(base)
    font_tiny = best_font(8)
    draw_text_shadow(draw, (3, SCREEN_H - 12), "FirePool Edition", font_tiny,
                     fill=ORANGE_FIRE)
    return base.convert("RGB")


def make_global_stats_screen(cluster_logo: Image.Image) -> Image.Image:
    """globalStats — network stats."""
    base = replace_pink(open_orig("globalStats.png"))
    draw = ImageDraw.Draw(base)
    font_tiny = best_font(8)
    draw_text_shadow(draw, (3, SCREEN_H - 12), "FirePool Edition", font_tiny,
                     fill=ORANGE_FIRE)
    return base.convert("RGB")


def make_portal_screen(cluster_logo: Image.Image) -> Image.Image:
    """portalscreen — WiFi captive portal."""
    base = replace_pink(open_orig("portalscreen.png"))
    draw = ImageDraw.Draw(base)

    # Replace logo top-left — expanded to fully cover original text
    draw.rectangle([0, 0, 170, 57], fill=(*BG_DARK, 255))
    paste_logo(base, cluster_logo, x=2, y=4, w=162, h=44)

    font_small = best_font(9)
    draw_text_shadow(draw, (4, 49), "FirePool Edition", font_small,
                     fill=ORANGE_FIRE)
    return base.convert("RGB")


def make_init_splash(cluster_logo: Image.Image, fp_logo: Image.Image) -> Image.Image:
    """initscreen2 — first boot splash (fully custom)."""
    img = Image.new("RGBA", (SCREEN_W, SCREEN_H), (*BG_DARK, 255))
    draw = ImageDraw.Draw(img)

    # Dark blue gradient band at bottom
    for y in range(SCREEN_H - 38, SCREEN_H):
        t = (y - (SCREEN_H - 38)) / 38
        r = int(BLUE_BAND[0] * (1 - t) + BLUE_MID[0] * t)
        g = int(BLUE_BAND[1] * (1 - t) + BLUE_MID[1] * t)
        b = int(BLUE_BAND[2] * (1 - t) + BLUE_MID[2] * t)
        draw.line([(0, y), (SCREEN_W, y)], fill=(r, g, b))

    # FP logo — right side watermark, large
    fp_h = 120
    fp_w = int(fp_logo.width * fp_h / fp_logo.height)
    fp_r = fp_logo.resize((fp_w, fp_h), Image.LANCZOS)
    # Darken alpha so it's a ghost watermark
    r2, g2, b2, a2 = fp_r.split()
    a2 = a2.point(lambda p: int(p * 0.28))
    fp_r.putalpha(a2)
    fp_x = SCREEN_W - fp_w - 4
    fp_y = (SCREEN_H - fp_h) // 2 - 10
    img.paste(fp_r, (fp_x, fp_y), fp_r)

    # ClusterQAxe+ logo — centred, large
    logo_w = 220
    logo_h = int(cluster_logo.height * logo_w / cluster_logo.width)
    paste_logo(img, cluster_logo, x=(SCREEN_W - logo_w) // 2 - 20, y=20,
               w=logo_w, h=logo_h)

    # "FIREPOOL EDITION" subtitle
    font_fp = best_font(17)
    text = "FIREPOOL EDITION"
    bbox = draw.textbbox((0, 0), text, font=font_fp)
    tw = bbox[2] - bbox[0]
    draw_text_shadow(draw, ((SCREEN_W - tw) // 2, SCREEN_H - 34),
                     text, font_fp, fill=ORANGE_FIRE)

    # Thin accent line above subtitle
    draw.line([(20, SCREEN_H - 40), (SCREEN_W - 20, SCREEN_H - 40)],
              fill=BLUE_BRIGHT, width=1)

    return img.convert("RGB")


def make_loading_splash(cluster_logo: Image.Image, fp_logo: Image.Image) -> Image.Image:
    """splashscreen2 — loading / 'Loading initial config' screen."""
    img = Image.new("RGBA", (SCREEN_W, SCREEN_H), (*BG_DARK, 255))
    draw = ImageDraw.Draw(img)

    # Subtle blue bar at top
    draw.rectangle([0, 0, SCREEN_W, 46], fill=(*BLUE_MID, 255))
    draw.line([(0, 46), (SCREEN_W, 46)], fill=BLUE_BRIGHT, width=2)

    # ClusterQAxe+ logo in top bar
    paste_logo(img, cluster_logo, x=4, y=4, w=148, h=38)

    # "FirePool Edition" tag top-right
    font_tag = best_font(9)
    draw_text_shadow(draw, (SCREEN_W - 88, 16), "FirePool Edition",
                     font_tag, fill=ORANGE_FIRE)

    # FP logo — centre ghost
    fp_h = 90
    fp_w = int(fp_logo.width * fp_h / fp_logo.height)
    fp_r = fp_logo.resize((fp_w, fp_h), Image.LANCZOS)
    r2, g2, b2, a2 = fp_r.split()
    a2 = a2.point(lambda p: int(p * 0.22))
    fp_r.putalpha(a2)
    img.paste(fp_r, ((SCREEN_W - fp_w) // 2, 55), fp_r)

    # "Loading initial config..." text
    font_load = best_font(13)
    draw_text_shadow(draw, (12, 56), "Loading initial config...",
                     font_load, fill=WHITE)

    # Dotted progress bar
    bar_y = 100
    draw.rectangle([12, bar_y, SCREEN_W - 12, bar_y + 6],
                   outline=BLUE_BRIGHT, width=1)
    for i in range(6):
        x0 = 14 + i * 36
        draw.rectangle([x0, bar_y + 2, x0 + 24, bar_y + 4],
                       fill=BLUE_BRIGHT)

    # Small credit line
    font_tiny = best_font(8)
    draw_text_shadow(draw, (12, SCREEN_H - 20),
                     "ClusterQAxe+ | FirePool Edition", font_tiny,
                     fill=(130, 160, 220))

    return img.convert("RGB")


# ── Main ─────────────────────────────────────────────────────────────────────

def main():
    print("Loading logos...")
    cluster_logo = load_cluster_logo()
    fp_logo      = load_fp_logo()

    screens = {
        "miningscreen2.png":  make_mining_screen(cluster_logo),
        "btcscreen.png":      make_btc_screen(cluster_logo),
        "settingsscreen.png": make_settings_screen(cluster_logo),
        "globalStats.png":    make_global_stats_screen(cluster_logo),
        "portalscreen.png":   make_portal_screen(cluster_logo),
        "initscreen2.png":    make_init_splash(cluster_logo, fp_logo),
        "splashscreen2.png":  make_loading_splash(cluster_logo, fp_logo),
    }

    for name, img in screens.items():
        out = OUT_DIR / name
        # Back up original if not already backed up
        bak = OUT_DIR / (name.replace(".png", "_orig.png"))
        if not bak.exists():
            orig = Image.open(out)
            orig.save(str(bak))
            print(f"  Backed up {name}")
        # Enforce exactly 320×170 — crop if slightly off (e.g. globalStats orig is 321px)
        if img.size != (SCREEN_W, SCREEN_H):
            img = img.crop((0, 0, SCREEN_W, SCREEN_H))
        img.save(str(out))
        print(f"  Saved {out.name}  ({img.width}x{img.height})")

    print("\nDone! All 7 screens written to Raw Images.")
    print("Next step: run  python create_themes.py  in the Converter Tool folder,")
    print("then rebuild firmware with  idf.py build")


if __name__ == "__main__":
    main()
