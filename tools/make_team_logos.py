#!/usr/bin/env python3
"""Build main/team_logos.c: NBA + WNBA team logos as const LVGL images.

On-device SVG/PNG decoding is out of the question (no ThorVG, and 30+ fetches per
cold start against the API budget), so the logos are rasterised here once and
compiled into flash as LV_COLOR_FORMAT_RGB565A8 bitmaps - colour plane
(stride = w*2, little-endian RGB565) followed by the A8 mask, which is what
LVGL 9 expects for that format.

    python3 tools/make_team_logos.py [--league nba|wnba|all] [--height 22] [--width 28]

Team ids are balldontlie's (the id each game's team object carries), so the
WNBA ids come from /wnba/v1/teams and are a SEPARATE namespace from the NBA
ids - hence one lookup per league in the generated header.

Logo sources:
  NBA : https://cdn.nba.com/logos/nba/<nba_team_id>/primary/L/logo.svg
        (ESPN nba png as fallback)
  WNBA: https://a.espncdn.com/i/teamlogos/wnba/500/<slug>.png
        (the NBA CDN has no /logos/wnba/ tree; ESPN's slugs are the lower-cased
        balldontlie abbreviations except WSH -> was)
Caching under tools/logos/ (gitignored) - delete a file to re-fetch it.
Writes tools/logos/contact_sheet.png so the id -> logo mapping can be eyeballed
instead of trusted (the NBA CDN ids are NOT balldontlie id + 1610612736).

NCAA note: D1 men/women share one 362-school ESPN logo set (ncaa/500/<id>.png,
~1.4 KB each packed = ~502 KB). That fits the partition but eats ~77% of the
free space for a feed that does not exist yet, so it is deliberately NOT baked
here; if a college scoreboard feed lands, add an "ncaa" league block below and
regenerate (measured footprint in TODO.md).
"""

import argparse
import io
import json
import os
import subprocess
import sys
import time
import urllib.error
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
}

# balldontlie WNBA id -> keep filter (1..13 are the current franchises; the API
# also lists defunct teams 14-15 and all-star artifacts 16+ that never show up
# in a live slate and have no ESPN logo; team_logo returns NULL for those and
# the card falls back to text).
WNBA_ACTIVE_IDS = set(range(1, 14))

# balldontlie WNBA abbreviation -> ESPN logo slug.
WNBA_ESPN_SLUG = {
    "NY": "ny", "CON": "con", "IND": "ind", "ATL": "atl", "WSH": "was",
    "CHI": "chi", "MIN": "min", "LV": "lv", "SEA": "sea", "PHX": "phx",
    "DAL": "dal", "LA": "la", "GS": "gs",
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
    for attempt in range(3):
        try:
            with urllib.request.urlopen(req, timeout=30) as r:
                data = r.read()
            break
        except urllib.error.HTTPError as e:
            if e.code == 429 and attempt < 2:   # balldontlie free tier: 5 req/min
                print("  429 rate limited, waiting 65 s...")
                time.sleep(65)
                continue
            raise
    with open(dest, "wb") as f:
        f.write(data)
    return dest


# Current WNBA roster as a fallback: balldontlie's /wnba edge is intermittently
# 404ing on the teams endpoint (the WNBA namespace is newer than the NBA one).
WNBA_ROSTER_FALLBACK = [
    {"id": 1, "abbreviation": "NY"}, {"id": 2, "abbreviation": "CON"},
    {"id": 3, "abbreviation": "IND"}, {"id": 4, "abbreviation": "ATL"},
    {"id": 5, "abbreviation": "WSH"}, {"id": 6, "abbreviation": "CHI"},
    {"id": 7, "abbreviation": "MIN"}, {"id": 8, "abbreviation": "LV"},
    {"id": 9, "abbreviation": "SEA"}, {"id": 10, "abbreviation": "PHX"},
    {"id": 11, "abbreviation": "DAL"}, {"id": 12, "abbreviation": "LA"},
    {"id": 13, "abbreviation": "GS"},
]


def balldontlie_teams(endpoint, cache_name, fallback=None):
    """Current teams from balldontlie (NBA ids 1..30), cached."""
    cache = os.path.join(CACHE, cache_name)
    if not os.path.exists(cache):
        key = api_key()
        try:
            fetch("https://api.balldontlie.io/%s/teams?per_page=100" % endpoint, cache,
                  {"Authorization": key, "User-Agent": "Mozilla/5.0"})
        except Exception as e:                                # noqa: BLE001
            if not fallback:
                raise
            print("  %s teams fetch failed (%s); using baked roster" % (endpoint, e))
            data = fallback
            return sorted(data, key=lambda t: t["id"])
    with open(cache) as f:
        data = json.load(f)["data"]
    return sorted(data, key=lambda t: t["id"])


def load_rgba(path, size):
    """Decode an SVG or PNG into an RGBA image (rendered at `size` px if SVG)."""
    from PIL import Image
    if path.endswith(".svg"):
        import cairosvg
        png = cairosvg.svg2png(url=path, output_width=size, output_height=size,
                               background_color=None)
        return Image.open(io.BytesIO(png)).convert("RGBA")
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
    """NBA/ESPN art carries invisible padding; crop it so the logo fills the box."""
    tight = img.split()[3].getbbox()      # alpha, not RGB: transparent corners are common
    return img.crop(tight) if tight else img


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


def collect_nba(height):
    entries, missing = [], []
    for t in balldontlie_teams("nba", "teams_nba.json"):
        if t["id"] > 30:
            continue                      # historical franchises
        abbr, bid = t["abbreviation"], t["id"]
        nba_id = NBA_COM_ID.get(abbr)
        if not nba_id:
            missing.append("%s (%d)" % (abbr, bid))
            continue
        svg = os.path.join(CACHE, "%s.svg" % abbr)
        img = None
        try:
            fetch("https://cdn.nba.com/logos/nba/%d/primary/L/logo.svg" % nba_id, svg)
            img = load_rgba(svg, height * 4)          # render big, then downsample
        except Exception as e:                            # noqa: BLE001
            print("  %s: cdn failed (%s), trying ESPN png" % (abbr, e))
        if img is None:
            png = os.path.join(CACHE, "%s.png" % abbr.lower())
            try:
                fetch("https://a.espncdn.com/combiner/i?img=/i/teamlogos/nba/500/%s.png"
                      "&w=96&h=96" % abbr.lower(), png, {"User-Agent": "Mozilla/5.0"})
                img = load_rgba(png, 0)
            except Exception as e:                        # noqa: BLE001
                print("  %s: no logo source (%s)" % (abbr, e))
                missing.append(abbr)
                continue
        entries.append((abbr, bid, img))
    return entries, missing


def collect_wnba(height):
    entries, missing = [], []
    for t in balldontlie_teams("wnba", "teams_wnba.json", WNBA_ROSTER_FALLBACK):
        bid = t["id"]
        if bid not in WNBA_ACTIVE_IDS:
            continue
        abbr = t["abbreviation"]
        slug = WNBA_ESPN_SLUG.get(abbr)
        if not slug:
            missing.append("%s (%d)" % (abbr, bid))
            continue
        png = os.path.join(CACHE, "wnba_%s.png" % slug)
        try:
            fetch("https://a.espncdn.com/combiner/i?img=/i/teamlogos/wnba/500/%s.png"
                  "&w=96&h=96" % slug, png, {"User-Agent": "Mozilla/5.0"})
            img = load_rgba(png, 0)
        except Exception as e:                            # noqa: BLE001
            print("  wnba %s: no logo source (%s)" % (abbr, e))
            missing.append(abbr)
            continue
        entries.append((abbr, bid, img))
    return entries, missing


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--league", choices=["nba", "wnba", "all"], default="all")
    ap.add_argument("--height", type=int, default=22, help="logo box height (px)")
    ap.add_argument("--width", type=int, default=28, help="logo box width (px)")
    args = ap.parse_args()

    try:
        import cairosvg  # noqa: F401
        from PIL import Image, ImageDraw  # noqa: F401
    except ImportError:
        sys.exit("needs Pillow + cairosvg:  pip install pillow cairosvg")

    leagues = []   # (name, symbol_prefix, entries, missing)
    sheet_items = []
    if args.league in ("nba", "all"):
        ents, miss = collect_nba(args.height)
        leagues.append(("", ents, miss))
    if args.league in ("wnba", "all"):
        ents, miss = collect_wnba(args.height)
        leagues.append(("wnba_", ents, miss))

    # rasterise + pack everything, build the contact sheet
    packed = []      # (prefix, [(bid, abbr, sym, w, h, data), ...])
    for prefix, entries, missing in leagues:
        rows = []
        for abbr, bid, img in entries:
            img = strip_empty_margins(img)
            img = fit(img, args.width, args.height)
            w, h, color, alpha = pack_rgb565a8(img)
            sym = "%slogo_%s" % (prefix, abbr.lower())
            rows.append((bid, abbr, sym, w, h, color + alpha))
            thumb = img.copy()
            thumb.thumbnail((40, 40))
            sheet_items.append((thumb, "%s%s %d" % ("W " if prefix else "", abbr, bid)))
            print("  %-4s id=%-3d %-3dx%-3d %5d bytes" % (abbr, bid, w, h, len(color) + len(alpha)))
        packed.append((prefix, rows, missing))

    total = sum(len(e[5]) for _, rows, _ in packed for e in rows)
    n = sum(len(rows) for _, rows, _ in packed)
    print("logos: %d, %d bytes of bitmap data (%.1f KB)" % (n, total, total / 1024))
    for _, _, missing in packed:
        if missing:
            print("WARNING - no logo for: %s" % ", ".join(missing))

    # ---- main/team_logos.c ----
    with open(os.path.join(MAIN, "team_logos.c"), "w") as f:
        f.write("/* Generated by tools/make_team_logos.py - do not edit by hand.\n"
                "   RGB565A8 bitmaps in flash: %d logos, %d bytes.\n"
                "   Regenerate: python3 tools/make_team_logos.py\n*/\n\n"
                "#include \"team_logos.h\"\n\n" % (n, total))
        for prefix, rows, _ in packed:
            for bid, abbr, sym, w, h, data in rows:
                assert len(data) == w * 2 * h + w * h, "layout mismatch for %s" % abbr
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
            table = "%slogo_entries" % prefix
            f.write("\nstatic const logo_entry_t %s[] = {\n" % table)
            for bid, abbr, sym, w, h, data in rows:
                f.write("    { %d, \"%s\", &%s },\n" % (bid, abbr, sym))
            f.write("};\n")
        f.write("\nconst logo_entry_t *nba_team_logos(int *count)\n"
                "{\n    *count = (int)sizeof(logo_entries) / sizeof(logo_entries[0]);\n"
                "    return logo_entries;\n}\n\n"
                "const logo_entry_t *wnba_team_logos(int *count)\n"
                "{\n    *count = (int)sizeof(wnba_logo_entries) / sizeof(wnba_logo_entries[0]);\n"
                "    return wnba_logo_entries;\n}\n\n"
                "static const lv_image_dsc_t *lookup(const logo_entry_t *tbl, int count,\n"
                "                                     int team_id)\n"
                "{\n    if (team_id <= 0) {\n        return NULL;\n    }\n"
                "    for (int i = 0; i < count; i++) {\n"
                "        if (tbl[i].team_id == team_id) {\n"
                "            return tbl[i].img;\n        }\n    }\n"
                "    return NULL;\n}\n\n"
                "const lv_image_dsc_t *team_logo(int team_id)\n"
                "{\n    int n = 0;\n"
                "    const logo_entry_t *tbl = nba_team_logos(&n);\n"
                "    return lookup(tbl, n, team_id);\n}\n\n"
                "const lv_image_dsc_t *wnba_team_logo(int team_id)\n"
                "{\n    int n = 0;\n"
                "    const logo_entry_t *tbl = wnba_team_logos(&n);\n"
                "    return lookup(tbl, n, team_id);\n}\n")

    with open(os.path.join(MAIN, "team_logos.h"), "w") as f:
        f.write("""#pragma once

#include "lvgl.h"

/* NBA + WNBA team logos, generated by tools/make_team_logos.py.
   `team_id` is balldontlie's id for that league (the one in each game's team
   object). NBA and WNBA ids are separate namespaces that overlap numerically,
   so each league has its own accessor - the caller picks it by the feed the
   game came from, never by the id itself. */
typedef struct {
    int team_id;
    const char *abbr;
    const lv_image_dsc_t *img;
} logo_entry_t;

/* Logo for a balldontlie NBA team id (1..30), NULL when there is none. */
const lv_image_dsc_t *team_logo(int team_id);

/* Logo for a balldontlie WNBA team id (1..13), NULL when there is none
   (defunct franchises, all-star artifacts). */
const lv_image_dsc_t *wnba_team_logo(int team_id);

/* Table access (for the preview harness and sanity checks). */
const logo_entry_t *nba_team_logos(int *count);
const logo_entry_t *wnba_team_logos(int *count);
""")

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
