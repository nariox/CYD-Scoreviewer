#!/usr/bin/env python3
"""Build main/team_venues.c: home arenas for the balldontlie-league teams.

balldontlie (the NBA/WNBA feed on the device) has no arena data at all -
not on games, not on teams - and ESPN's pro /teams endpoint is empty of it
too. ESPN's pro SCOREBOARD does carry it (every event names its venue), so
this tool harvests scoreboards day by day and keeps the home team's arena.
The harvest is cached at tools/logos/venues_harvest.json (gitignored), so
re-runs are offline; --refresh re-walks the ESPN windows.

    python3 tools/make_team_venues.py [--refresh]

Mapping is by team FULL NAME ("Atlanta Hawks"): ESPN's pro abbreviations
differ from balldontlie's (NY vs NYK, WSH vs WAS), but the display names
agree - the same lesson as the logo ids: never join feeds on an
abbreviation. Arenas get renamed by sponsorship eventually; --refresh picks
up what ESPN's scoreboard says now. College needs none of this: its feed
(ESPN scoreboard) carries the venue per game, so NCAAB/NCAAW read
nba_game_t.venue live.
"""

import argparse
import datetime as dt
import json
import os
import sys
import time
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import make_team_logos as mtl  # noqa: E402  (CACHE, balldontlie_teams, WNBA_ROSTER_FALLBACK)

CACHE = os.path.join(mtl.CACHE, "venues_harvest.json")
OUT = os.path.join(HERE, "..", "cyd_basketball_scores", "main", "team_venues.c")
UA = {"User-Agent": "Mozilla/5.0"}
PACE_S = 1.2   # be polite: the same public scoreboard the device's college feed uses

# ESPN league slugs and the date windows to walk, in order. Preseason and
# opening weeks put most teams at home; later windows (and the playoffs)
# mop up the rest. The walk stops early once every roster team has a venue.
WINDOWS = {
    "nba": [
        ("2025-10-04", "2025-10-20"),   # preseason
        ("2025-11-01", "2025-11-12"),   # regular-season opening
        ("2026-01-16", "2026-01-20"),   # MLK week (dense)
        ("2026-04-08", "2026-04-12"),   # final regular week
        ("2026-05-04", "2026-05-12"),   # playoff first round
        ("2026-06-03", "2026-06-15"),   # Finals
    ],
    "wnba": [
        ("2026-05-08", "2026-05-24"),
        ("2026-07-01", "2026-07-12"),
        ("2026-08-05", "2026-08-16"),
        ("2026-09-09", "2026-09-20"),
        ("2026-10-01", "2026-10-17"),   # through the playoffs
    ],
}

LEAGUES = (("nba", "NBA"), ("wnba", "WNBA"))


def scoreboard_day(slug, day):
    url = ("https://site.api.espn.com/apis/site/v2/sports/basketball/"
           f"{slug}/scoreboard?dates={day.strftime('%Y%m%d')}")
    req = urllib.request.Request(url, headers=UA)
    with urllib.request.urlopen(req, timeout=30) as r:
        return json.load(r)


def harvest_walk(slug, want_names):
    """Walk the windows, collecting home team full name -> arena, and stop
    as soon as every name the roster needs has been seen."""
    found = {}
    for start, end in WINDOWS[slug]:
        day = dt.date.fromisoformat(start)
        hi = dt.date.fromisoformat(end)
        while day <= hi:
            try:
                d = scoreboard_day(slug, day)
            except Exception as e:  # noqa: BLE001 - one bad day must not kill the walk
                print(f"    {day}: {e}")
                day += dt.timedelta(days=1)
                time.sleep(PACE_S)
                continue
            for ev in d.get("events", []):
                comp = (ev.get("competitions") or [{}])[0]
                venue = (comp.get("venue") or {}).get("fullName")
                if not venue:
                    continue
                for c in comp.get("competitors", []):
                    if c.get("homeAway") != "home":
                        continue
                    name = (c.get("team") or {}).get("displayName")
                    if not name:
                        continue
                    prev = found.get(name)
                    if prev and prev != venue:
                        print(f"    !! {name}: {prev!r} -> {venue!r} (renamed? keeping latest)")
                    found[name] = venue
            print(f"    {day}: {len(found)}/{len(want_names)} covered")
            if want_names <= found.keys():
                return found
            day += dt.timedelta(days=1)
            time.sleep(PACE_S)
    return found


# Current-team namespace per league (balldontlie ids): the same edge the logo
# tables assume. balldontlie's /teams list also carries retired franchises
# (1940s teams, Euro clubs, ids into the 200k range) whose names can collide
# with live ones ("Denver Nuggets" is both #8 today and #50 in the BAA), so
# rows are capped here and the harvest only trusts what a current arena needs.
CAPS = {"nba": 30, "wnba": 13}


def render_table(cache, slug, league_c):
    teams = mtl.balldontlie_teams(
        slug, f"teams_{slug}.json",
        mtl.WNBA_ROSTER_FALLBACK if slug == "wnba" else None)
    # Rows: current-league teams the harvest saw hosting. ESPN's pro
    # scoreboards only name current teams, so an unmapped id inside the cap
    # is a harvest hole, and a retired team above it is noise filtered away
    # (an uncapped table would index ~216k NULL pointers into flash).
    cap = CAPS[slug]
    rows = sorted(((t["id"], t["full_name"], cache[slug].get(t["full_name"]))
                   for t in teams if t["id"] <= cap), key=lambda r: r[0])
    rows = [r for r in rows if r[2]]
    mapped = {tid for tid, _, _ in rows}
    holes = [str(tid) for tid in range(1, cap + 1) if tid not in mapped]
    if holes:
        print(f"  WARNING {league_c}: no venue for ids {', '.join(holes)}")
    tbl = f"{league_c}_VENUES"
    size = cap
    out = [f"#define {tbl}_MAX {size}",
           f"static const char *const {tbl}[{tbl}_MAX + 1] = {{"]
    for tid, name, arena in rows:
        if arena:
            esc = arena.replace("\\", "\\\\").replace('"', '\\"')
            out.append(f'    [{tid}] = "{esc}",   /* {name} */')
        else:
            out.append(f"    /* {tid} {name}: NO VENUE HARVESTED */")
    out.append("};")
    out.append("")
    fn = f"{league_c.lower()}_team_venue"
    out += [f"const char *{fn}(int id)", "{",
            f"    return (id >= 0 && id <= {tbl}_MAX) ? {tbl}[id] : NULL;",
            "}", ""]
    return "\n".join(out)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--refresh", action="store_true", help="re-walk ESPN scoreboards")
    args = ap.parse_args()

    cache = {}
    if os.path.exists(CACHE) and not args.refresh:
        with open(CACHE) as f:
            cache = json.load(f)

    for slug, league_c in LEAGUES:
        print(f"{league_c}:")
        teams = mtl.balldontlie_teams(
            slug, f"teams_{slug}.json",
            mtl.WNBA_ROSTER_FALLBACK if slug == "wnba" else None)
        # Current namespace only (see CAPS): retired names in the roster could
        # never be covered by a live scoreboard, so counting them would keep
        # the walk running its full window and the ratio honest-but-useless.
        want = {t["full_name"] for t in teams if t["id"] <= CAPS[slug]}
        if args.refresh or not cache.get(slug):
            print("  walking ESPN scoreboards...")
            cache[slug] = harvest_walk(slug, want)
        mapped = want & cache[slug].keys()
        print(f"  mapped {len(mapped)}/{len(want)} current teams")

    with open(CACHE, "w") as f:
        json.dump(cache, f, indent=1)

    parts = ["/* GENERATED by tools/make_team_venues.py - do not edit.",
             " * Home arenas harvested from ESPN pro scoreboards "
             f"({dt.date.today().isoformat()}).",
             " * Keyed by balldontlie team id - NBA and WNBA ids are separate,",
             " * overlapping namespaces, hence one table per league.",
             " */",
             '#include "team_venues.h"',
             '#include <stddef.h>              /* NULL */',
             ""]
    for slug, league_c in LEAGUES:
        parts.append(render_table(cache, slug, league_c))
    with open(OUT, "w") as f:
        f.write("\n".join(parts))
    print(f"wrote {os.path.relpath(OUT, os.path.join(HERE, '..'))}")


if __name__ == "__main__":
    main()
