#include "espn_api.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "pro_team_ids.h"

static const char *TAG = "espn_api";

/* The one scoreboard behind all four feeds has no published quota (unlike
   the balldontlie tier it replaced), but it is still a public API we want to
   be a good guest on. Scoreboard days stop changing once the day is over,
   and the service refreshes every 15 min, so a few seconds between requests
   is plenty. */
#ifndef ESPN_API_MIN_GAP_MS
#define ESPN_API_MIN_GAP_MS 4000
#endif

/* ESPN's edges 404 unknown clients (the logo CDN in tools/make_team_logos.py
   taught us that the hard way): ask politely, in browser. */
#define ESPN_API_UA "Mozilla/5.0"

static int64_t s_last_req_us = 0;

static void espn_pace(void)
{
    if (s_last_req_us == 0) {
        s_last_req_us = esp_timer_get_time();
        return;
    }
    int64_t due = s_last_req_us + (int64_t)ESPN_API_MIN_GAP_MS * 1000;
    int64_t now = esp_timer_get_time();
    if (due > now) {
        int wait_ms = (int)((due - now) / 1000);
        ESP_LOGI(TAG, "rate limit pacing: waiting %d ms", wait_ms);
        vTaskDelay(pdMS_TO_TICKS(wait_ms));
    }
    s_last_req_us = esp_timer_get_time();
}

/* ---- streaming scanner -------------------------------------------------
   ESPN pads every scoreboard event with stats, leaders, records, broadcasts
   and headlines: a covered day is 10-20 events but 200-400 KB of JSON, a
   ~3-4x cJSON tree this board's heap would die on. So the body is streamed
   through a character-level scanner with constant memory: it tracks the JSON
   skeleton (each container's type, and the key that introduced it) and reads
   values only where the scoreboard schema puts the fields the board renders:

     events[]:              id, date, status.type{state,id,shortDetail}
     competitions[].status: period, displayClock
     competitions[].venue:  fullName + address{city,state}
     competitors[]:         homeAway, score, team{id,abbreviation,displayName}
     competitors[].
       linescores[]:        value

   Everything else is walked past without being looked at. Array elements
   inherit the array's introducing key, which is exactly the virtual name
   ("events", "competitors", "linescores") the matchers below use; a deep
   stats blob can never put a stray "score" or "value" on the read path
   because every matcher demands the right container *and* depth.
   --------------------------------------------------------------------- */

#define SC_STACK   20   /* tracked nesting depth; real max is ~10, stats pads deeper */
#define SC_NAMELEN 13   /* "abbreviation"/"displayClock" are the longest keys: 12+NUL */

typedef struct {
    /* JSON skeleton */
    int   depth;
    char  ctype[SC_STACK];             /* 'o' object / 'a' array per open container */
    char  name[SC_STACK][SC_NAMELEN];  /* key that introduced the container */
    char  key[SC_STACK][SC_NAMELEN];   /* last key read inside the container */
    bool  in_string, str_escape;
    bool  after_colon;                 /* ':' seen -> next string is a value */
    char  tok[64];
    int   tok_len;
    char  bare[24];                    /* unquoted token (numbers, true, null) */
    int   bare_len;

    /* schema positions: object depth on the read path, 0 = off path */
    int  event_depth;
    int  status_depth, status_type_depth;
    int  comp_status_depth;
    int  venue_depth, venue_addr_depth;
    int  competitor_depth;
    int  linescore_depth;
    int  lines_idx;
    int  side;                         /* 0 away/visitor, 1 home, -1 unknown */

    /* the event currently being collected */
    nba_game_t g;
    char short_detail[32];
    char state[8];
    int  type_id;
    char venue_city[20];
    char venue_state[4];

    /* output */
    league_t   league;
    nba_game_t *out;
    int         max_games;
    int        *count;
} scan_t;

/* ~4 KB of scanner, static: one fetcher task, and too big for its stack. */
static scan_t s_sc;

/* Deliberate truncation of a bounded scanner token: strnlen + memcpy, not
   strncpy - gcc's -Wstringop-truncation (on by -Werror) treats a dropped
   NUL as a bug even when the caller terminates right after. */
static void copy_str(const char *src, char *dst, int n)
{
    size_t len = strnlen(src, (size_t)n - 1);
    memcpy(dst, src, len);
    dst[len] = '\0';
}

/* The US game day of a UTC stamp like "2026-10-23T02:30Z" (ESPN writes
   minutes, no seconds). Evening US games are already the NEXT day in UTC,
   so stamping the date prefix shows tonight's game as tomorrow (device bug
   2026-10: tonight's Nuggets@OKC listed on the 23rd). Subtract 12 h - the
   same convention as time_sync_game_day_epoch() (day 0 = now - 12 h) and
   the day ESPN keys its scoreboard `dates=` query on. */
static void game_day_from_stamp(const char *iso, char *out, int n)
{
    int y = 0, mo = 0, d = 0, hh = 0;
    if (sscanf(iso, "%4d-%2d-%2dT%2d:", &y, &mo, &d, &hh) != 4
        || y < 2000 || y > 9999 || mo < 1 || mo > 12
        || d < 1 || d > 31 || hh > 23) {
        copy_str(iso, out, 11);  /* unexpected shape: keep the YYYY-MM-DD prefix
                                    (callers pass the 12-byte date field) */
        return;
    }
    if (hh < 12) {               /* the 12 h shift crosses UTC midnight */
        if (--d == 0) {
            if (--mo == 0) { mo = 12; y--; }
            static const unsigned char mlen[12] =
                { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
            d = mlen[mo - 1];
            if (mo == 2 && (y % 4 == 0 && (y % 100 != 0 || y % 400 == 0))) d++;
        }
    }
    /* The legitimate output is always 10 chars + NUL; n is the caller's
       whole date field (12), which also keeps -Wformat-truncation happy
       when it loses the guards' ranges through the decrement branch. */
    snprintf(out, (size_t)n, "%04d-%02d-%02d", y, mo, d);
}

static void tok_add(scan_t *sc, char c)
{
    if (sc->tok_len < (int)sizeof(sc->tok) - 1) {
        sc->tok[sc->tok_len++] = c;
        sc->tok[sc->tok_len] = '\0';
    }
}

static void open_event(scan_t *sc)
{
    nba_game_t *g = &sc->g;
    memset(g, 0, sizeof(*g));
    g->period = -1;
    g->home_score = g->visitor_score = -1;
    for (int i = 0; i < 7; i++) {
        g->home_q[i] = g->visitor_q[i] = -1;
    }
    g->home_timeouts = g->visitor_timeouts = -1;
    sc->short_detail[0] = '\0';
    sc->state[0] = '\0';
    sc->type_id = 0;
    sc->side = -1;
    sc->status_depth = sc->status_type_depth = 0;
    sc->comp_status_depth = sc->competitor_depth = 0;
    sc->venue_depth = sc->venue_addr_depth = 0;
    sc->venue_city[0] = sc->venue_state[0] = '\0';
    sc->linescore_depth = 0;
    sc->lines_idx = 0;
}

static void finish_event(scan_t *sc)
{
    nba_game_t *g = &sc->g;
    sc->event_depth = 0;
    if (g->id <= 0 || !g->datetime[0]) {
        return;                    /* junk, or a body truncated mid-event */
    }
    g->league = sc->league;

    /* Pro leagues: ESPN's team ids are its own namespace, but the baked logo
       and arena tables are keyed by balldontlie id (the pre-migration feed),
       and the two ranges OVERLAP (ESPN 17 = Brooklyn, balldontlie 17 =
       Portland), so map at the parse boundary: everything downstream sees
       the id space it was built for. Unmapped -> 0 (logo ring), never the
       raw ESPN id. College needs no map: the NCAA logo table IS keyed by
       ESPN school ids. */
    if (g->league == LEAGUE_NBA || g->league == LEAGUE_WNBA) {
        g->home_id = espn_pro_feed_id(g->league, g->home_id);
        g->visitor_id = espn_pro_feed_id(g->league, g->visitor_id);
    }

    /* ESPN status.type ids past 3 are the ugly ones (4 postponed, 5 canceled);
       the state string ("pre"/"in"/"post") carries the ordinary flow. */
    if (sc->type_id == 4) {
        copy_str("postponed", g->status_state, sizeof(g->status_state));
        copy_str("Postponed", g->status, sizeof(g->status));
        g->postponed = true;
    } else if (sc->type_id == 5) {
        copy_str("canceled", g->status_state, sizeof(g->status_state));
        copy_str("Canceled", g->status, sizeof(g->status));
    } else if (strcmp(sc->state, "pre") == 0) {
        copy_str("scheduled", g->status_state, sizeof(g->status_state));
        copy_str("Scheduled", g->status, sizeof(g->status));
        /* The scoreboard pre-initialises a game with a zeroed clock ("0:00"),
           so for scheduled games the tip-off text ("11/10 - 6:30 PM EST")
           replaces it as the card's centre. Unannounced times come as "TBD" -
           not worth a card line, leave it to "Scheduled". */
        if (sc->short_detail[0] && strcmp(sc->short_detail, "TBD") != 0) {
            copy_str(sc->short_detail, g->time, sizeof(g->time));
        }
    } else if (strcmp(sc->state, "in") == 0) {
        copy_str("in_progress", g->status_state, sizeof(g->status_state));
        copy_str(sc->short_detail, g->status, sizeof(g->status));  /* "2nd 8:59" */
    } else {
        copy_str("final", g->status_state, sizeof(g->status_state));
        if (sc->short_detail[0]) {
            copy_str(sc->short_detail, g->status, sizeof(g->status));  /* "Final/2OT" */
        } else {
            copy_str("Final", g->status, sizeof(g->status));
        }
    }

    /* ESPN pre-plays the data: unplayed (and postponed/canceled) games come
       with "score":"0" and a "0:00" clock. Only games with a result - played
       or in progress - carry scores. */
    if (strcmp(g->status_state, "in_progress") != 0
        && strcmp(g->status_state, "final") != 0) {
        g->home_score = g->visitor_score = -1;
        for (int i = 0; i < 7; i++) {
            g->home_q[i] = g->visitor_q[i] = -1;
        }
        if (g->period <= 0) {
            g->period = -1;        /* the scoreboard pre-seeds period 0 */
        }
    }

    if (sc->out && sc->count) {
        /* Keep the NEWEST max_games of the day, not the first ones seen:
           ESPN lists a 69-game November slate oldest tip-off first, so a
           plain append would drop the marquee evening games the board
           wants to show. Insertion keeps `out` sorted newest-first too. */
        nba_game_t *arr = sc->out;
        int n = *sc->count;
        int pos = n;
        while (pos > 0 && strcmp(arr[pos - 1].datetime, g->datetime) < 0) {
            pos--;
        }
        if (pos < sc->max_games) {
            int end = (n < sc->max_games) ? n : sc->max_games - 1;
            for (int k = end; k > pos; k--) {
                arr[k] = arr[k - 1];
            }
            arr[pos] = *g;
            if (n < sc->max_games) {
                (*sc->count)++;
            }
        }
    }
}

static void assign_value(scan_t *sc, bool bare)
{
    const char *v = bare ? sc->bare : sc->tok;
    const int d = sc->depth;
    const char *k = sc->key[d];

    if (d == sc->event_depth) {
        if (strcmp(k, "id") == 0) {
            sc->g.id = atoi(v);
        } else if (!bare && strcmp(k, "date") == 0) {
            /* ESPN writes "2026-01-24T19:00Z" (no seconds) - still ISO, still
               sorts chronologically as a string. */
            copy_str(v, sc->g.datetime, sizeof(sc->g.datetime));
            game_day_from_stamp(v, sc->g.date, sizeof(sc->g.date));
        }
        return;
    }
    if (d == sc->status_type_depth) {
        if (strcmp(k, "id") == 0) {
            sc->type_id = atoi(v);
        } else if (strcmp(k, "state") == 0) {
            copy_str(v, sc->state, sizeof(sc->state));
        } else if (strcmp(k, "shortDetail") == 0) {
            copy_str(v, sc->short_detail, sizeof(sc->short_detail));
        }
        return;
    }
    if (d == sc->comp_status_depth) {
        if (strcmp(k, "period") == 0) {
            sc->g.period = atoi(v);
        } else if (strcmp(k, "displayClock") == 0) {
            copy_str(v, sc->g.time, sizeof(sc->g.time));
        }
        return;
    }
    if (d == sc->venue_depth) {
        if (!bare && strcmp(k, "fullName") == 0) {
            copy_str(v, sc->g.venue, sizeof(sc->g.venue));
        }
        return;
    }
    if (d == sc->venue_addr_depth) {
        if (!bare && strcmp(k, "city") == 0) {
            copy_str(v, sc->venue_city, sizeof(sc->venue_city));
        } else if (!bare && strcmp(k, "state") == 0) {
            copy_str(v, sc->venue_state, sizeof(sc->venue_state));
        }
        return;
    }
    if (d == sc->competitor_depth) {
        if (strcmp(k, "homeAway") == 0) {
            sc->side = (strcmp(v, "home") == 0) ? 1 : 0;
        } else if (strcmp(k, "score") == 0 && sc->side >= 0) {
            if (sc->side == 1) {
                sc->g.home_score = atoi(v);
            } else {
                sc->g.visitor_score = atoi(v);
            }
        }
        return;
    }
    if (sc->competitor_depth && d == sc->competitor_depth + 1
        && strcmp(sc->name[d], "team") == 0 && sc->side >= 0) {
        char *abbr = (sc->side == 1) ? sc->g.home_abbr : sc->g.visitor_abbr;
        char *nm   = (sc->side == 1) ? sc->g.home_name : sc->g.visitor_name;
        int  *id   = (sc->side == 1) ? &sc->g.home_id : &sc->g.visitor_id;
        if (strcmp(k, "id") == 0) {
            *id = atoi(v);
        } else if (strcmp(k, "abbreviation") == 0) {
            copy_str(v, abbr, 8);
        } else if (strcmp(k, "displayName") == 0) {
            copy_str(v, nm, 48);
        }
        return;
    }
    if (d == sc->linescore_depth && strcmp(k, "value") == 0
        && sc->side >= 0 && sc->lines_idx < 7) {
        if (sc->side == 1) {
            sc->g.home_q[sc->lines_idx] = atoi(v);
        } else {
            sc->g.visitor_q[sc->lines_idx] = atoi(v);
        }
        sc->lines_idx++;
    }
}

static void finish_bare(scan_t *sc)
{
    if (sc->bare_len == 0) {
        return;
    }
    if (sc->after_colon && sc->depth > 0 && sc->depth < SC_STACK
        && sc->ctype[sc->depth] == 'o') {
        assign_value(sc, true);
    }
    sc->bare_len = 0;
    sc->after_colon = false;
}

static void open_container(scan_t *sc, char c)
{
    int d = sc->depth + 1;
    sc->depth = d;
    /* The ':' that led here is consumed by the container itself: without
       this, the first key of every object would read as a value (the token
       after ':' never arrived as a scalar) and everything inside it keys off
       the wrong slot. */
    sc->after_colon = false;
    if (d >= SC_STACK) {
        return;                    /* deep padding: values here stay off-path */
    }
    sc->ctype[d] = (c == '{') ? 'o' : 'a';
    /* Seed with the parent's last key: object values get their own key, and
       array elements inherit the array's key as their virtual name. Arrays
       never overwrite key[] (they have no keys), so every element of
       "competitors" is named "competitors". */
    const char *intro = sc->key[d - 1];
    copy_str(intro, sc->name[d], SC_NAMELEN);
    memcpy(sc->key[d], sc->name[d], SC_NAMELEN);

    if (c != '{') {
        if (strcmp(sc->name[d], "linescores") == 0
            && d - 1 == sc->competitor_depth) {
            sc->lines_idx = 0;              /* fresh line set per competitor */
        }
        return;
    }

    const char *dn = sc->name[d];
    if (sc->event_depth == 0 && strcmp(dn, "events") == 0) {
        open_event(sc);
        sc->event_depth = d;
        return;
    }
    if (strcmp(dn, "status") == 0) {
        if (d - 1 == sc->event_depth) {
            sc->status_depth = d;             /* event.status */
        } else if (strcmp(sc->name[d - 1], "competitions") == 0) {
            sc->comp_status_depth = d;        /* competition.status */
        }
    } else if (strcmp(dn, "type") == 0 && d - 1 == sc->status_depth) {
        sc->status_type_depth = d;
    } else if (strcmp(dn, "competitors") == 0) {
        sc->competitor_depth = d;
        sc->side = -1;
    } else if (strcmp(dn, "venue") == 0
               && strcmp(sc->name[d - 1], "competitions") == 0) {
        sc->venue_depth = d;                  /* competition.venue */
    } else if (strcmp(dn, "address") == 0 && d - 1 == sc->venue_depth) {
        sc->venue_addr_depth = d;
    } else if (strcmp(dn, "linescores") == 0
               && d - 1 == sc->competitor_depth + 1) {
        sc->linescore_depth = d;
    }
}

static void close_container(scan_t *sc)
{
    int d = sc->depth;
    if (d > 0 && d < SC_STACK) {
        if (d == sc->linescore_depth) {
            sc->linescore_depth = 0;
        }
        if (d == sc->competitor_depth) {
            sc->competitor_depth = 0;
            sc->side = -1;
        }
        if (d == sc->comp_status_depth) {
            sc->comp_status_depth = 0;
        }
        if (d == sc->venue_depth) {
            /* Arena + city/state -> "Breslin Center, East Lansing MI".
               The arena name is the payload (real ones run to 51 chars), so
               the ", City ST" tail is appended only when it fits WHOLE - a
               half-cut city ("...Center, Rock H") is worse than none. Hand-
               appended: no snprintf format for gcc's range analysis to
               bound; copy_str is the deliberate-truncation helper. */
            if (sc->g.venue[0] && sc->venue_city[0]) {
                const size_t vlen = strnlen(sc->g.venue, sizeof(sc->g.venue) - 1);
                const size_t clen = strnlen(sc->venue_city, sizeof(sc->venue_city) - 1);
                const size_t slen = strnlen(sc->venue_state, sizeof(sc->venue_state) - 1);
                const size_t tail = 2 + clen + (slen ? 1 + slen : 0);
                const size_t room = sizeof(sc->g.venue) - 1 - vlen;
                if (tail <= room) {   /* the whole tail, or none of it */
                    sc->g.venue[vlen] = ',';
                    sc->g.venue[vlen + 1] = ' ';
                    memcpy(&sc->g.venue[vlen + 2], sc->venue_city, clen);
                    size_t end = vlen + 2 + clen;
                    if (slen) {
                        sc->g.venue[end++] = ' ';
                        memcpy(&sc->g.venue[end], sc->venue_state, slen);
                        end += slen;
                    }
                    sc->g.venue[end] = '\0';
                }
            }
            sc->venue_depth = sc->venue_addr_depth = 0;
            sc->venue_city[0] = sc->venue_state[0] = '\0';
        }
        if (d == sc->status_type_depth) {
            sc->status_type_depth = 0;
        }
        if (d == sc->status_depth) {
            sc->status_depth = 0;
        }
        if (d == sc->event_depth) {
            finish_event(sc);
        }
    }
    if (sc->depth > 0) {
        sc->depth--;
    }
}

static void sc_char(scan_t *sc, char c)
{
    if (sc->in_string) {
        if (sc->str_escape) {
            sc->str_escape = false;
            tok_add(sc, c);                /* \" etc: keep the literal */
            return;
        }
        if (c == '"') {
            sc->in_string = false;
            if (sc->depth > 0 && sc->depth < SC_STACK
                && sc->ctype[sc->depth] == 'o') {
                if (sc->after_colon) {
                    assign_value(sc, false);
                    sc->after_colon = false;
                } else {
                    copy_str(sc->tok, sc->key[sc->depth], SC_NAMELEN);
                }
            } else {
                sc->after_colon = false;   /* string in an array / deep padding */
            }
        } else if (c == '\\') {
            sc->str_escape = true;
        } else {
            tok_add(sc, c);
        }
        return;
    }

    switch (c) {
    case '"':
        sc->in_string = true;
        sc->tok_len = 0;
        sc->tok[0] = '\0';
        return;
    case ':':
        sc->after_colon = true;
        return;
    case '{':
        finish_bare(sc);
        open_container(sc, '{');
        return;
    case '[':
        finish_bare(sc);
        open_container(sc, '[');
        return;
    case '}':
    case ']':
        finish_bare(sc);
        sc->after_colon = false;
        close_container(sc);
        return;
    case ',':
        finish_bare(sc);
        sc->after_colon = false;
        return;
    default:
        if (c == ' ' || c == '\n' || c == '\r' || c == '\t') {
            finish_bare(sc);
            return;
        }
        if (sc->bare_len < (int)sizeof(sc->bare) - 1) {
            sc->bare[sc->bare_len++] = c;
            sc->bare[sc->bare_len] = '\0';
        }
    }
}

static void scan_reset(scan_t *sc, league_t league, nba_game_t *out,
                       int max_games, int *count)
{
    memset(sc, 0, sizeof(*sc));
    sc->league = league;
    sc->out = out;
    sc->max_games = max_games;
    sc->count = count;
    sc->side = -1;
    if (count) {
        *count = 0;
    }
}

esp_err_t espn_parse_scoreboard(const char *body, size_t len, league_t league,
                                nba_game_t *out, int max_games, int *out_count)
{
    if (!body || !out || !out_count || max_games <= 0) {
        return ESP_ERR_INVALID_ARG;
    }
    scan_reset(&s_sc, league, out, max_games, out_count);
    for (size_t i = 0; i < len; i++) {
        sc_char(&s_sc, body[i]);
    }
    return ESP_OK;
}

/* ---- fetch ------------------------------------------------------------ */

static esp_err_t scan_http_cb(esp_http_client_event_t *evt)
{
    if (evt->event_id == HTTP_EVENT_ON_DATA && evt->data && evt->data_len > 0) {
        const char *bytes = (const char *)evt->data;
        for (int i = 0; i < evt->data_len; i++) {
            sc_char(&s_sc, bytes[i]);
        }
    }
    return ESP_OK;
}

/* All four feeds ride the same site API and the same JSON shape: the pro
   scoreboards carry full league slates (every game has TV), quarters + OT
   linescores, live clocks, and per-game venues, exactly like college's. */
static const char *espn_path(league_t lg)
{
    switch (lg) {
    case LEAGUE_NBA:   return "nba";
    case LEAGUE_WNBA:  return "wnba";
    case LEAGUE_NCAAW: return "womens-college-basketball";
    default:           return "mens-college-basketball";
    }
}

static const char *espn_lg_name(league_t lg)
{
    switch (lg) {
    case LEAGUE_NBA:   return "NBA";
    case LEAGUE_WNBA:  return "WNBA";
    case LEAGUE_NCAAW: return "NCAAW";
    default:           return "NCAAB";
    }
}

/* ---- keep-alive persistent client -------------------------------------- */

/* All four feeds ride one host, and the expensive, memory-risky part of a
   request is the TLS handshake, not the GET: measured on-device (stress soak,
   2026-10), every fresh handshake churns ~20-25 KB of mbedTLS record/session
   allocations and fails outright once free heap dips below ~45 KB (PSA -141,
   mislabeled "PSA signature verification failed"). A day-walk, a slate hunt
   or a pre-warm burst costs ONE handshake this way instead of one per
   request. Reuse is deliberately BURST-scoped: the worker calls
   espn_api_conn_release() whenever it goes to sleep, because an open session
   parks those same ~22 KB while idle (soak caught it holding the board's
   heap at 24 K across the refresh gap - worse than reconnecting). Within a
   burst the gaps are the 4 s pacing, so the socket stays warm; the idle gate
   below is only a belt-and-suspenders for the half-dead-socket case, and a
   transport failure tears the client down too. */
#define ESPN_KEEPALIVE_MAX_IDLE_S 55
#define ESPN_KEEPALIVE_MAX_IDLE_US (ESPN_KEEPALIVE_MAX_IDLE_S * 1000000LL)

static esp_http_client_handle_t s_http = NULL;
static int64_t s_http_last_us = 0;

static esp_err_t http_client_get(const char *url)
{
    if (s_http) {
        int64_t idle = esp_timer_get_time() - s_http_last_us;
        if (idle > ESPN_KEEPALIVE_MAX_IDLE_US) {
            ESP_LOGI(TAG, "keep-alive idle %u s - fresh connection",
                     (unsigned)(idle / 1000000));
            esp_http_client_cleanup(s_http);
            s_http = NULL;
        }
    }
    if (!s_http) {
        esp_http_client_config_t cfg = {
            .url = url,
            .method = HTTP_METHOD_GET,
            .crt_bundle_attach = esp_crt_bundle_attach,
            .event_handler = scan_http_cb,   /* streams: nothing buffered */
            .timeout_ms = 15000,
            .buffer_size = 1024,
            .keep_alive_enable = true,
        };
        s_http = esp_http_client_init(&cfg);
        if (!s_http) {
            return ESP_ERR_NO_MEM;
        }
        esp_http_client_set_header(s_http, "Accept", "application/json");
        esp_http_client_set_header(s_http, "User-Agent", ESPN_API_UA);
        return ESP_OK;
    }
    return esp_http_client_set_url(s_http, url);
}

static void http_client_drop(const char *why)
{
    if (s_http) {
        ESP_LOGW(TAG, "keep-alive dropped: %s", why);
        esp_http_client_cleanup(s_http);
        s_http = NULL;
    }
}

void espn_api_conn_release(void)
{
    if (!s_http) {
        return;
    }
    ESP_LOGI(TAG, "feed idle - keep-alive released (instant heap %u; lwIP returns the rest within seconds)",
             (unsigned)esp_get_free_heap_size());
    esp_http_client_cleanup(s_http);
    s_http = NULL;
}

esp_err_t espn_api_fetch_games(league_t league, const char *day_iso,
                               nba_game_t *out, int max_games, int *out_count)
{
    if (!day_iso || !out || max_games <= 0 || !out_count) {
        return ESP_ERR_INVALID_ARG;
    }

    char ymd[16];
    int j = 0;
    for (const char *p = day_iso; *p && j < (int)sizeof(ymd) - 1; p++) {
        if (*p != '-') {
            ymd[j++] = *p;
        }
    }
    ymd[j] = '\0';

    char url[256];
    snprintf(url, sizeof(url),
             "https://site.api.espn.com/apis/site/v2/sports/basketball/%s"
             "/scoreboard?dates=%s", espn_path(league), ymd);

    espn_pace();
    scan_reset(&s_sc, league, out, max_games, out_count);

    esp_err_t err = http_client_get(url);
    if (err != ESP_OK) {
        return err;
    }

    err = esp_http_client_perform(s_http);
    int status = esp_http_client_get_status_code(s_http);
    s_http_last_us = esp_timer_get_time();

    ESP_LOGI(TAG, "%s %s: %d game(s), http %d, free heap %u",
             espn_lg_name(league), day_iso,
             *out_count, status, (unsigned)esp_get_free_heap_size());

    /* No HTTP status at all (-1 = the parser's untouched initial value in
       IDF 6, NOT 0 as one might remember from esp_http_client's HTTP_STATUS_
       UNINITIALIZED era): transport/handshake died. The cert-storm signature
       (PSA_ERROR_INSUFFICIENT_MEMORY, logged by esp-x509-crt-bundle as
       "PSA signature verification failed") is an allocation failure inside
       the PSA/mbedTLS layer, so capture the allocator's view at the moment of
       failure: total free vs the largest contiguous internal block. Healthy
       total + tiny largest block = fragmentation; both healthy = fixed-pool
       (PSA key/op slot) exhaustion, invisible to these counters. */
    if (err != ESP_OK && status <= 0) {
        ESP_LOGE(TAG, "transport fail: heap %u, largest internal block %u",
                 (unsigned)esp_get_free_heap_size(),
                 (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
        http_client_drop("transport fail");
    } else if (err != ESP_OK && status == 200) {
        /* A 200 whose body died mid-stream leaves the socket out of sync -
           same treat-as-dead rule. */
        http_client_drop("body truncated mid-stream");
    }

    /* perform() reports 4xx as errors but the status is what matters here
       (same lesson as nba_api); 200 with zero events is a normal quiet day. */
    if (status == 200) {
        return ESP_OK;
    }
    if (status == 429) {
        return ESP_ERR_TIMEOUT;
    }
    if (status > 0) {
        return ESP_FAIL;
    }
    return (err != ESP_OK) ? err : ESP_FAIL;
}
