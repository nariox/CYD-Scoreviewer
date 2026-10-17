#include "espn_api.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "espn_api";

/* Different host and a far friendlier quota than balldontlie's free tier, but
   still a public API we want to be a good guest on. Scoreboard days stop
   changing once the day is over, and the service only refreshes every 15 min,
   so a few seconds between requests is plenty. */
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
    int  competitor_depth;
    int  linescore_depth;
    int  lines_idx;
    int  side;                         /* 0 away/visitor, 1 home, -1 unknown */

    /* the event currently being collected */
    nba_game_t g;
    char short_detail[32];
    char state[8];
    int  type_id;

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
            copy_str(v, sc->g.date, 11);   /* "YYYY-MM-DD" prefix of the stamp */
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

esp_err_t espn_api_fetch_games(league_t league, const char *day_iso,
                               nba_game_t *out, int max_games, int *out_count)
{
    if ((league != LEAGUE_NCAAB && league != LEAGUE_NCAAW) || !day_iso
        || !out || max_games <= 0 || !out_count) {
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

    const char *path = (league == LEAGUE_NCAAW) ? "womens-college-basketball"
                                                : "mens-college-basketball";
    char url[256];
    snprintf(url, sizeof(url),
             "https://site.api.espn.com/apis/site/v2/sports/basketball/%s"
             "/scoreboard?dates=%s", path, ymd);

    espn_pace();
    scan_reset(&s_sc, league, out, max_games, out_count);

    esp_http_client_config_t cfg = {
        .url = url,
        .method = HTTP_METHOD_GET,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .event_handler = scan_http_cb,   /* streams: nothing buffered */
        .timeout_ms = 15000,
        .buffer_size = 1024,
    };

    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) {
        return ESP_ERR_NO_MEM;
    }
    esp_http_client_set_header(client, "Accept", "application/json");
    esp_http_client_set_header(client, "User-Agent", ESPN_API_UA);

    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    ESP_LOGI(TAG, "%s %s: %d game(s), http %d, free heap %u",
             (league == LEAGUE_NCAAW) ? "NCAAW" : "NCAAB", day_iso,
             *out_count, status, (unsigned)esp_get_free_heap_size());

    /* perform() reports 4xx as errors but the status is what matters here
       (same lesson as nba_api); 200 with zero events is a normal quiet day. */
    if (status == 200) {
        return ESP_OK;
    }
    if (status == 429) {
        return ESP_ERR_TIMEOUT;
    }
    if (status != 0) {
        return ESP_FAIL;
    }
    return (err != ESP_OK) ? err : ESP_FAIL;
}
