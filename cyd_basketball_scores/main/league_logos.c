#include "league_logos.h"

const lv_image_dsc_t *game_logo(league_t league, int team_id)
{
    switch (league) {
    case LEAGUE_WNBA:
        return wnba_team_logo(team_id);
    case LEAGUE_NCAAB:
    case LEAGUE_NCAAW:
        return ncaa_team_logo(team_id);
    case LEAGUE_NBA:
    default:
        return team_logo(team_id);
    }
}
