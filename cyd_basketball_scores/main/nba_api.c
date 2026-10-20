#include "nba_api.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#ifdef CONFIG_DEFAULTS_OVERRIDE
#include "defaults_override.h"
#endif

static const char *TAG = "nba_api";

#define NBA_API_URL_BASE   "https://api.balldontlie.io/nba/v1/games"
#define WNBA_API_URL_BASE  "https://api.balldontlie.io/wnba/v1/games"
/* Small on purpose: each game carries quarter-by-quarter stats and cJSON's
   in-memory tree is ~3-4x the text size. Measured: a full 15-game page is
   ~13.3 KB of text and its tree needs ~46.5 KB, but parse-time free heap
   sits at ~46-52 KB depending on the day's fragmentation - a production
   boot failed at offset 12413/13285 once the venue field moved the statics
   up. 12 games (~10.7 KB -> ~42 KB tree) keeps a real margin. Paging past
   a page is free anyway (next_cursor, paced the same). */
#define NBA_API_PER_PAGE   12
#define NBA_API_KEY_MAX    64
#define NBA_API_BODY_INIT  (8 * 1024)
#define NBA_API_BODY_MAX   (32 * 1024)

static char s_api_key[NBA_API_KEY_MAX] = "";

/* balldontlie hosts the WNBA scoreboard on the same key/tier - only the
   path changes. Its payload shape differs a little (see parse_game). */
const char *nba_api_url_nba(void)  { return NBA_API_URL_BASE; }
const char *nba_api_url_wnba(void) { return WNBA_API_URL_BASE; }
bool nba_api_is_balldontlie(league_t l)
{
    return l == LEAGUE_NBA || l == LEAGUE_WNBA;
}

/* Free tier: about 5 requests per minute. Sleep until our next slot instead of
   firing bursts and eating HTTP 429s (13 s is a little over 60/5 for safety). */
#define NBA_API_MIN_GAP_MS 13000

static int64_t s_last_req_us = 0;

void nba_api_note_request(void)
{
    s_last_req_us = esp_timer_get_time();
}

static void pace_request(void)
{
    if (s_last_req_us == 0) {
        nba_api_note_request();      /* first call since boot: no wait */
        return;
    }
    int64_t due = s_last_req_us + (int64_t)NBA_API_MIN_GAP_MS * 1000;
    int64_t now = esp_timer_get_time();
    if (due > now) {
        int wait_ms = (int)((due - now) / 1000);
        ESP_LOGI(TAG, "rate limit pacing: waiting %d ms", wait_ms);
        vTaskDelay(pdMS_TO_TICKS(wait_ms));
    }
    nba_api_note_request();
}

typedef struct {
    char *buf;
    size_t len;
    size_t cap;
    esp_err_t err;
} body_acc_t;

static esp_err_t http_event_cb(esp_http_client_event_t *evt)
{
    if (evt->event_id != HTTP_EVENT_ON_DATA || !evt->data || evt->data_len <= 0) {
        return ESP_OK;
    }

    body_acc_t *acc = (body_acc_t *)evt->user_data;
    size_t n = (size_t)evt->data_len;

    if (acc->len + n + 1 > acc->cap) {
        size_t new_cap = acc->cap * 2;
        if (new_cap < acc->len + n + 1) {
            new_cap = acc->len + n + 1;
        }
        if (new_cap > NBA_API_BODY_MAX) {
            ESP_LOGE(TAG, "response body exceeds %d bytes", NBA_API_BODY_MAX);
            acc->err = ESP_ERR_INVALID_SIZE;
            return ESP_FAIL;
        }
        char *grown = realloc(acc->buf, new_cap);
        if (!grown) {
            acc->err = ESP_ERR_NO_MEM;
            return ESP_FAIL;
        }
        acc->buf = grown;
        acc->cap = new_cap;
    }

    memcpy(acc->buf + acc->len, evt->data, n);
    acc->len += n;
    acc->buf[acc->len] = '\0';
    return ESP_OK;
}

static esp_err_t http_get_json(const char *url, char **body_out, size_t *len_out,
                               int *http_status_out)
{
    body_acc_t acc = { 0 };
    acc.cap = NBA_API_BODY_INIT;
    acc.buf = malloc(acc.cap);
    if (!acc.buf) {
        return ESP_ERR_NO_MEM;
    }
    acc.buf[0] = '\0';

    esp_http_client_config_t cfg = {
        .url = url,
        .method = HTTP_METHOD_GET,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .event_handler = http_event_cb,
        .user_data = &acc,
        .timeout_ms = 15000,
        .buffer_size = 1024,
    };

    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) {
        free(acc.buf);
        return ESP_ERR_NO_MEM;
    }

    esp_http_client_set_header(client, "Accept", "application/json");
    esp_http_client_set_header(client, "Authorization", s_api_key);
    esp_http_client_set_header(client, "User-Agent", "cyd-basketball-scores/1.0");

    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    /* perform() flags 4xx/5xx as errors, but status + body were still
       received; report the HTTP status and let the caller map it. */
    if (status == 0) {
        ESP_LOGE(TAG, "request failed: %s", esp_err_to_name(err));
        free(acc.buf);
        return (err != ESP_OK) ? err : ESP_FAIL;
    }

    if (acc.err != ESP_OK) {
        free(acc.buf);
        return acc.err;
    }

    *body_out = acc.buf;
    *len_out = acc.len;
    *http_status_out = status;
    return ESP_OK;
}

static void json_str(const cJSON *obj, const char *key, char *dst, size_t n)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (cJSON_IsString(item) && item->valuestring) {
        strncpy(dst, item->valuestring, n - 1);
        dst[n - 1] = '\0';
    } else {
        dst[0] = '\0';
    }
}

static int json_int(const cJSON *obj, const char *key, int missing)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (cJSON_IsNumber(item)) {
        return item->valueint;
    }
    return missing;
}

static bool json_bool(const cJSON *obj, const char *key)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, key);
    return cJSON_IsTrue(item);
}

/* Newest-first (ISO datetimes compare chronologically as strings). */
static int game_datetime_cmp(const void *a, const void *b)
{
    const nba_game_t *x = (const nba_game_t *)a, *y = (const nba_game_t *)b;
    return strcmp(y->datetime, x->datetime);
}

static void parse_game(const cJSON *item, nba_game_t *game)
{
    memset(game, 0, sizeof(*game));

    game->id = json_int(item, "id", 0);
    json_str(item, "date", game->date, sizeof(game->date));
    json_str(item, "datetime", game->datetime, sizeof(game->datetime));

    /* WNBA v1 shape quirks (probed against live playoff data):
       - no "datetime": the full ISO-8601 UTC stamp sits in "date"
         ("2026-10-10T01:30:00.000Z"), so split it out;
       - scores are top-level "away_score"/"home_score";
       - "status" is ESPN-style ("pre"/"in"/"post"), the readable word lives
         in "status_state" ("scheduled"/"in progress"/"final"/"canceled"). */
    if (game->datetime[0] == '\0' && strlen(game->date) > 12) {
        char *t = strchr(game->date, 'T');
        strncpy(game->datetime, game->date, sizeof(game->datetime) - 1);
        game->datetime[sizeof(game->datetime) - 1] = '\0';
        if (t) *t = '\0';                    /* "date" keeps YYYY-MM-DD */
    }

    json_str(item, "status", game->status, sizeof(game->status));
    json_str(item, "status_state", game->status_state, sizeof(game->status_state));
    json_str(item, "time", game->time, sizeof(game->time));
    game->period = json_int(item, "period", -1);
    game->postseason = json_bool(item, "postseason");
    game->postponed = json_bool(item, "postponed");
    game->home_score = json_int(item, "home_team_score", -1);
    game->visitor_score = json_int(item, "visitor_team_score", -1);
    if (game->home_score < 0) {
        game->home_score = json_int(item, "home_score", -1);
    }
    if (game->visitor_score < 0) {
        game->visitor_score = json_int(item, "away_score", -1);
    }
    if (game->status[0] && game->status_state[0] &&
        strcmp(game->status_state, "in progress") == 0) {
        snprintf(game->status, sizeof(game->status), "%d%s%.11s", game->period,
                 game->time[0] ? " " : "", game->time);   /* %.11s: -O2/gcc15
                 format-truncation needs the width bounded (clock strings like
                 "05:31", "Halftime" never get near it) */
    } else if (game->status_state[0]) {
        snprintf(game->status, sizeof(game->status), "%.23s", game->status_state);
    }

    const cJSON *home = cJSON_GetObjectItemCaseSensitive(item, "home_team");
    if (cJSON_IsObject(home)) {
        game->home_id = json_int(home, "id", 0);
        json_str(home, "abbreviation", game->home_abbr, sizeof(game->home_abbr));
        json_str(home, "full_name", game->home_name, sizeof(game->home_name));
    }

    const cJSON *visitor = cJSON_GetObjectItemCaseSensitive(item, "visitor_team");
    if (cJSON_IsObject(visitor)) {
        game->visitor_id = json_int(visitor, "id", 0);
        json_str(visitor, "abbreviation", game->visitor_abbr, sizeof(game->visitor_abbr));
        json_str(visitor, "full_name", game->visitor_name, sizeof(game->visitor_name));
    }

    /* Period-by-period scoring. The list endpoint already carries all of it
       (GET /games/{id} returns the same object), so the detail view needs no
       extra request - and these few bytes per game buy that for free. */
    static const char *const Q_KEYS_HOME[] = {"home_q1",     "home_q2", "home_q3",
                                              "home_q4",     "home_ot1", "home_ot2",
                                              "home_ot3"};
    static const char *const Q_KEYS_VIS[]  = {"visitor_q1",  "visitor_q2",
                                              "visitor_q3",  "visitor_q4",
                                              "visitor_ot1", "visitor_ot2",
                                              "visitor_ot3"};
    for (int i = 0; i < 7; i++) {
        game->home_q[i]     = json_int(item, Q_KEYS_HOME[i], -1);
        game->visitor_q[i]  = json_int(item, Q_KEYS_VIS[i], -1);
    }
    game->season = json_int(item, "season", 0);
    game->home_timeouts     = json_int(item, "home_timeouts_remaining", -1);
    game->visitor_timeouts  = json_int(item, "visitor_timeouts_remaining", -1);
    game->home_bonus        = json_bool(item, "home_in_bonus");
    game->visitor_bonus     = json_bool(item, "visitor_in_bonus");
}

void nba_api_set_api_key(const char *key)
{
    if (key) {
        strncpy(s_api_key, key, NBA_API_KEY_MAX - 1);
        s_api_key[NBA_API_KEY_MAX - 1] = '\0';
    }
}

esp_err_t nba_api_fetch_games(const char *base_url,
                              const char *start_date, const char *end_date,
                              const char *season_type, int cursor,
                              nba_game_t *out, int max_games, int *out_count,
                              int *next_cursor_out)
{
    if (out_count) {
        *out_count = 0;
    }
    if (next_cursor_out) {
        *next_cursor_out = 0;
    }
    if (!start_date || !end_date || !out || max_games <= 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_api_key[0] == '\0') {
        strncpy(s_api_key, NBA_API_KEY, NBA_API_KEY_MAX - 1);
    }
    if (s_api_key[0] == '\0') {
        ESP_LOGE(TAG, "No API key set — define NBA_API_KEY in defaults_override.h "
                      "or call nba_api_set_api_key()");
        return ESP_ERR_INVALID_STATE;
    }
    pace_request();

    char query[160];
    int q = snprintf(query, sizeof(query), "start_date=%s&end_date=%s",
                     start_date, end_date);
    if (season_type && q < (int)sizeof(query)) {
        q += snprintf(query + q, sizeof(query) - q, "&season_type=%s", season_type);
    }
    if (cursor > 0 && q < (int)sizeof(query)) {
        q += snprintf(query + q, sizeof(query) - q, "&cursor=%d", cursor);
    }
    if (q < 0 || q >= (int)sizeof(query)) {
        ESP_LOGE(TAG, "Query string too long");
        return ESP_ERR_INVALID_SIZE;
    }
    snprintf(query + q, sizeof(query) - q, "&per_page=%d", NBA_API_PER_PAGE);

    char url[256];
    snprintf(url, sizeof(url), "%s?%s", base_url, query);

    ESP_LOGI(TAG, "GET %s (free heap: %u)", url, esp_get_free_heap_size());
    char *body = NULL;
    size_t len = 0;
    int status = 0;
    esp_err_t err = http_get_json(url, &body, &len, &status);
    if (err != ESP_OK) {
        return err;
    }

    ESP_LOGI(TAG, "HTTP %d, body %u bytes (free heap: %u)",
             status, (unsigned)len, esp_get_free_heap_size());

    if (status == 401) {
        ESP_LOGE(TAG, "401 Unauthorized — check API key");
        free(body);
        return ESP_ERR_INVALID_ARG;
    }
    if (status == 429) {
        ESP_LOGE(TAG, "429 rate limited");
        free(body);
        return ESP_ERR_TIMEOUT;
    }
    if (status != 200) {
        ESP_LOGE(TAG, "HTTP %d: %.120s", status, body);
        free(body);
        return ESP_FAIL;
    }

    cJSON *root = cJSON_Parse(body);
    if (!root) {
        /* The error pointer sits where parsing stopped — typically a malloc
           failure inside cJSON when the tree outgrows the heap. */
        ESP_LOGE(TAG, "JSON parse failed at offset %d/%u (free heap: %u) — "
                      "likely out of memory",
                 (int)(cJSON_GetErrorPtr() - body), (unsigned)len,
                 esp_get_free_heap_size());
        free(body);
        return ESP_FAIL;
    }
    free(body);

    const cJSON *data = cJSON_GetObjectItemCaseSensitive(root, "data");
    if (!cJSON_IsArray(data)) {
        ESP_LOGE(TAG, "Missing 'data' array in response");
        cJSON_Delete(root);
        return ESP_FAIL;
    }

    int count = 0;
    int ghosts = 0;
    const cJSON *item = NULL;
    cJSON_ArrayForEach(item, data) {
        if (count >= max_games) {
            break;
        }
        parse_game(item, &out[count]);
        /* Ghost schedule rows: balldontlie keeps games that never get
           played - the WNBA carries playoff placeholders as "post canceled"
           with 0-0 scores and an empty team object (observed 2026-10-14:
           "LV @ GS"). A row that can never have a result is permanent
           noise on the board and in the NVS snapshot: gate it here. */
        if (strcmp(out[count].status_state, "canceled") == 0 ||
            out[count].home_id <= 0 || out[count].visitor_id <= 0) {
            ghosts++;
            continue;
        }
        count++;
    }
    if (ghosts > 0) {
        ESP_LOGI(TAG, "skipped %d ghost game(s) (canceled/incomplete)", ghosts);
    }
    /* balldontlie orders by id, which does not track dates; hand the page to
       the caller newest-first (the cache's newest-N cap keeps these). */
    if (count > 1) {
        qsort(out, (size_t)count, sizeof(nba_game_t), game_datetime_cmp);
    }

    const cJSON *meta = cJSON_GetObjectItemCaseSensitive(root, "meta");
    if (cJSON_IsObject(meta)) {
        const cJSON *next = cJSON_GetObjectItemCaseSensitive(meta, "next_cursor");
        if (cJSON_IsNumber(next) && next_cursor_out) {
            *next_cursor_out = next->valueint;
        }
    }

    cJSON_Delete(root);

    if (out_count) {
        *out_count = count;
    }
    if (cursor > 0) {
        ESP_LOGI(TAG, "Fetched %d game(s) for %s..%s (cursor %d, next %d)",
                 count, start_date, end_date, cursor,
                 next_cursor_out ? *next_cursor_out : 0);
    } else {
        ESP_LOGI(TAG, "Fetched %d game(s) for %s..%s (next cursor %d)",
                 count, start_date, end_date,
                 next_cursor_out ? *next_cursor_out : 0);
    }
    return ESP_OK;
}

void nba_api_log_games(const nba_game_t *games, int count)
{
    ESP_LOGI(TAG, "---- %d NBA game(s) ----", count);
    for (int i = 0; i < count; i++) {
        const nba_game_t *g = &games[i];

        if (g->visitor_score >= 0 && g->home_score >= 0) {
            ESP_LOGI(TAG, "%s | %s (%s) %3d @ %3d (%s) %s | %s%s",
                     g->date, g->visitor_name, g->visitor_abbr, g->visitor_score,
                     g->home_score, g->home_abbr, g->home_name,
                     g->status, g->postseason ? " [postseason]" : "");
        } else {
            ESP_LOGI(TAG, "%s | %s (%s) @ (%s) %s | %s%s",
                     g->date, g->visitor_name, g->visitor_abbr,
                     g->home_abbr, g->home_name,
                     g->status, g->postseason ? " [postseason]" : "");
        }
    }
}
