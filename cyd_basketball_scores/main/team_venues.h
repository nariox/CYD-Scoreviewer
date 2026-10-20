#pragma once

/* Home arenas for the balldontlie-league teams, baked from ESPN's pro
   scoreboards (tools/make_team_venues.py). balldontlie games carry no arena
   data, so a pro game's venue is looked up by the HOME team's id — the same
   per-league namespace rule as the logo tables: NBA ids 1..30 and WNBA ids
   1..13 overlap, pick by the feed the game came from. NULL when the id is
   out of range or the harvest never saw that team host.
   College (NCAAB/NCAAW) does not use these: the ESPN scoreboard carries the
   venue per game, so it rides nba_game_t.venue (correct even at neutral
   sites, where the "home" team's arena is not the venue). */

/* balldontlie NBA team id (1..30) -> arena name, e.g. "Madison Square Garden". */
const char *nba_team_venue(int id);

/* balldontlie WNBA team id (1..13) -> arena name, e.g. "Chase Center". */
const char *wnba_team_venue(int id);
