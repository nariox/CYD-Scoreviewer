#!/usr/bin/env python3
"""Build main/team_logos.c: every NBA team logo as a const LVGL image.

On-device SVG/PNG decoding is out of the question (no ThorVG, and 30 fetches per
cold start against the API budget), so the logos are rasterised here once and
compiled into flash as LV_COLOR_FORMAT_RGB565A8 bitmaps - colour plane
(stride = w*2, little-endian RGB565) followed by the A8 mask, which is what
LVGL 9 expects for that format.

    python3 tools/make_team_logos.py [--size 22] [--width 28]

Sources, in order of preference:
  1. https://cdn.nba.com/logos/nba/<nba_team_id>/primary/L/logo.svg
  2. https://a.espncdn.com/combiner/i?img=/i/teamlogos/nba/500/<abbr>.png
The team list comes from balldontlie (/nba/v1/teams, cached in tools/logos/)
because that is the id the API reports in each game; the NBA.com ids are the
well-known 16106127xx table below (it is NOT offset-linear, hence the table).
Everything is cached under tools/logos/ - delete a file to re-fetch it.

Also writes tools/logos/contact_sheet.png so the id -> logo mapping can be
 eyeballed instead of trusted.
"""

import argparse
import json
import os
import subprocess
import sys
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
CACHE = os.path.join(HERE, "logos")
MAIN = os.path.join(HERE, "..", "cyd_basketball_scores", "main")

# balldontlie team id -> NBA.com team id (the number in the cdn.nba.com URL).
NBA_COM_ID = {
    "ATL": 1610612737, "BOS": 1610612738, "CLE": 1610612739, "NOP": 1610612740,
    "CHI": 1610612741, "DAL": 1610612742, "DEN": 1610612743, "GSW": 1610612744,
    "HOU": 1610612745, "LAC": 1610612746, "LAL": 1610612747, "MIA": 1610612748,
    "MIL": 1610612749, "MIN": 1610612750, "BKN": 1610612751, "NYK": 1610612752,
    "ORL": 1610612753, "IND": 1610612754, "PHI": 1610612755, "PHX": 1610612756,
    "POR": 1610612757, "SAC": 1610612758, "SAS": 1610612759, "OKC": 1610612760,
    "TOR": 1610612761, "UTA": 1610612762, "MEM": 1610612763, "WAS": 1610612764,
    "DET": 1610612765,
    "CHA": 1610612766,
    # 30th team: balldontlie has no 30th entry with these abbreviations if a
    # franchise moves; the generator warns about any team missing from here.
}


def api_key():
    path = os.path.join(MAIN, "defaults_override.h")
    try:
        with open(path) as f:
            for line in f:
                if "NBA_API_KEY" in line and '"' in line:
                    return line.split('"')[1]
    except OSError:
        pass
    return os.environ.get("NBA_API_KEY", "")


def fetch(url, dest, headers=None):
    if os.path.exists(dest) and os.path.getsize(dest) > 0:
        return dest
    os.makedirs(os.path.dirname(dest), exist_ok=True)
    req = urllib.request.Request(url, headers=headers or {"User-Agent": "cyd-scores"})
    with urllib.request.urlopen(req, timeout=30) as r:
        data = r.read()
    with open(dest, "wb") as f:
        f.write(data)
    return dest


def team_list():
    """balldontlie's current teams (ids 1..30), cached."""
    cache = os.path.join(CACHE, "teams.json")
    if not os.path.exists(cache):
        key = api_key()
        if not key:
            sys.exit("No NBA_API_KEY (main/defaults_override.h) and no cached teams.json")
        fetch("https://api.balldontlie.io/nba/v1/teams?per_page=100", cache,
              {"Authorization": key, "User-Agent": "cyd-scores"})
    with open(cache) as f:
        data = json.load(f)["data"]
    return sorted((t for t in data if t["id"] <= 30), key=lambda t: t["id"])


def load_rgba(path, size):
    """Decode an SVG or PNG into a size-constrained RGBA image (Pillow/cairosvg)."""
    from PIL import Image
    if path.endswith(".svg"):
        import cairosvg
        png = cairosvg.svg2png(url=path, output_width=size, output_height=size,
                               background_color=None)
        return Image.open(__import__("io").BytesIO(png)).convert("RGBA")
    return Image.open(path).convert("RGBA")


def fit(img, max_w, max_h):
    """Scale preserving aspect ratio so the logo fits inside max_w x max_h."""
    from PIL import Image
    w, h = img.size
    scale = min(max_w / w, max_h / h)
    nw = max(1, int(round(w * scale)))
    nh = max(1, int(round(h * scale)))
    return img.resize((nw, nh), Image.LANCZOS)


def strip_empty_margins(img):
    """NBA SVGs carry a lot of invisible padding; crop it so the logo fills the box."""
    bbox = img.getbbox()
    alpha = img.split()[3]
    tight = alpha.getbbox()          # alpha, not RGB: transparent corners are common
    box = tight or bbox
    return img.crop(box) if box else img


def pack_rgb565a8(img):
    """LVGL 9 RGB565A8: RGB565 plane (little-endian, stride w*2) then the A8 mask."""
    w, h = img.size
    px = img.load()
    color, alpha = bytearray(), bytearray()
    for y in range(h):
        for x in range(w):
            r, g, b, a = px[x, y]
            v = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)
            color += bytes((v & 0xFF, v >> 8))
            alpha.append(a)
    return w, h, bytes(color), bytes(alpha)


def c_array(data, per_line=16):
    out = []
    for i in range(0, len(data), per_line):
        out.append("    " + ",".join("0x%02x" % b for b in data[i:i + per_line]) + ",")
    return "\n".join(out)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--height", type=int, default=22, help="logo box height (px)")
    ap.add_argument("--width", type=int, default=28, help="logo box width (px)")
    args = ap.parse_args()

    try:
        import cairosvg  # noqa: F401
        from PIL import Image, ImageDraw  # noqa: F401
    except ImportError:
        sys.exit("needs Pillow + cairosvg:  pip install pillow cairosvg")

    teams = team_list()
    entries, missing, sheet_items = [], [], []

    for t in teams:
        abbr, bid = t["abbreviation"], t["id"]
        nba_id = NBA_COM_ID.get(abbr)
        if not nba_id:
            missing.append("%s (%d)" % (abbr, bid))
            continue

        svg = os.path.join(CACHE, "%s.svg" % abbr)
        img = None
        try:
            fetch("https://cdn.nba.com/logos/nba/%d/primary/L/logo.svg" % nba_id, svg)
            img = load_rgba(svg, args.height * 4)          # render big, then downsample
        except Exception as e:                                 # noqa: BLE001
            print("  %s: cdn failed (%s), trying ESPN png" % (abbr, e))
        if img is None:
            png = os.path.join(CACHE, "%s.png" % abbr.lower())
            try:
                fetch("https://a.espncdn.com/combiner/i?img=/i/teamlogos/nba/500/%s.png"
                      "&w=96&h=96" % abbr.lower(), png)
                img = load_rgba(png, 0)
            except Exception as e:                             # noqa: BLE001
                print("  %s: no logo source (%s)" % (abbr, e))
                missing.append(abbr)
                continue

        img = strip_empty_margins(img)
        img = fit(img, args.width, args.height)
        w, h, color, alpha = pack_rgb565a8(img)
        sym = "logo_%s" % abbr.lower()
        entries.append((bid, abbr, sym, w, h, color + alpha))

        thumb = img.copy()
        thumb.thumbnail((40, 40))
        sheet_items.append((thumb, "%s %d" % (abbr, bid)))
        print("  %-3s id=%-3d %-3dx%-3d %5d bytes" % (abbr, bid, w, h, len(color) + len(alpha)))

    total = sum(len(e[5]) for e in entries)
    print("logos: %d, %d bytes of bitmap data (%.1f KB)" % (len(entries), total, total / 1024))
    if missing:
        print("WARNING - no logo for: %s" % ", ".join(missing))

    # ---- main/team_logos.c ----
    with open(os.path.join(MAIN, "team_logos.c"), "w") as f:
        f.write("/* Generated by tools/make_team_logos.py - do not edit by hand.\n"
                "   RGB565A8 bitmaps in flash: %d logos, %d bytes.\n"
                "   Regenerate: python3 tools/make_team_logos.py\n*/\n\n"
                "#include \"team_logos.h\"\n\n" % (len(entries), total))
        for bid, abbr, sym, w, h, data in entries:
            color_len = w * 2 * h
            f.write("\nstatic const uint8_t %s_map[] = {\n%s\n};\n\n" % (sym, c_array(data)))
            f.write("static const lv_image_dsc_t %s = {\n"
                    "    .header = {\n"
                    "        .magic = LV_IMAGE_HEADER_MAGIC,\n"
                    "        .cf = LV_COLOR_FORMAT_RGB565A8,\n"
                    "        .flags = 0,\n"
                    "        .w = %d,\n        .h = %d,\n        .stride = %d,\n"
                    "        .reserved_2 = 0,\n    },\n"
                    "    .data_size = sizeof(%s_map),\n"
                    "    .data = %s_map,\n"
                    "    .reserved = NULL,\n};\n\n"
                    % (sym, w, h, w * 2, sym, sym))
            assert len(data) == color_len + w * h, "layout mismatch for %s" % abbr
        f.write("\nstatic const logo_entry_t s_logos[] = {\n")
        for bid, abbr, sym, w, h, data in entries:
            f.write("    { %d, \"%s\", &%s },\n" % (bid, abbr, sym))
        f.write("};\n\n"
                "const lv_image_dsc_t *team_logo(int team_id)\n{\n"
                "    if (team_id <= 0) {\n        return NULL;\n    }\n"
                "    for (int i = 0; i < (int)sizeof(s_logos) / sizeof(s_logos[0]); i++) {\n"
                "        if (s_logos[i].team_id == team_id) {\n"
                "            return s_logos[i].img;\n        }\n    }\n"
                "    return NULL;\n}\n\n"
                "const logo_entry_t *team_logos(void)\n{\n    return s_logos;\n}\n\n"
                "int team_logos_count(void)\n{\n"
                "    return (int)sizeof(s_logos) / sizeof(s_logos[0]);\n}\n")

    with open(os.path.join(MAIN, "team_logos.h"), "w") as f:
        f.write("""#pragma once

#include "lvgl.h"

/* One logo per NBA team, generated by tools/make_team_logos.py.
   `team_id` is balldontlie's id (the one in each game's team object). */
typedef struct {
    int team_id;
    const char *abbr;
    const lv_image_dsc_t *img;
} logo_entry_t;

/* Logo for a balldontlie team id, NULL when there is none (unknown id, or a
   historical franchise we did not bake). Read-only flash data: costs nothing
   in the LVGL pool beyond the widget that displays it. */
const lv_image_dsc_t *team_logo(int team_id);

/* Table access (for the preview harness and sanity checks). */
const logo_entry_t *team_logos(void);
int team_logos_count(void);
""")

    # the lookup itself lives in a tiny hand-written file next to the data
    with open(os.path.join(MAIN, "team_logos_lookup.c"), "w") as f:
        f.write('#include "team_logos.h"\n\n'
                'const lv_image_dsc_t *team_logo(int team_id)\n{\n'
                '    if (team_id <= 0) return NULL;\n'
                '    for (int i = 0; i < team_logos_count(); i++) {\n'
                '        if (team_logos()[i].team_id == team_id) return team_logos()[i].img;\n'
                '    }\n    return NULL;\n}\n')

    # ---- contact sheet so the id -> logo mapping can be checked by eye ----
    from PIL import Image, ImageDraw
    cols = 6
    cell = 60
    rows = (len(sheet_items) + cols - 1) // cols
    sheet = Image.new("RGB", (cols * cell, rows * (cell + 14)), (22, 33, 62))
    dr = ImageDraw.Draw(sheet)
    for i, (thumb, label) in enumerate(sheet_items):
        cx, cy = (i % cols) * cell, (i // cols) * (cell + 14)
        sheet.paste(thumb, (cx + (cell - thumb.width) // 2, cy + (cell - thumb.height) // 2),
                    thumb)
        dr.text((cx + 4, cy + cell + 2), label, fill=(255, 255, 255))
    out = os.path.join(CACHE, "contact_sheet.png")
    sheet.save(out)
    print("contact sheet: %s" % out)
    subprocess.run(["magick", out, "-scale", "300%", out.replace(".png", "_big.png")],
                   check=False)


if __name__ == "__main__":
    main()
