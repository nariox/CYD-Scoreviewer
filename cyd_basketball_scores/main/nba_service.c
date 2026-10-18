#include "nba_service.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "esp_system.h"
#include "nvs_settings.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "cards_screen.h"
#include "espn_api.h"
#include "game_detail_screen.h"
#include "time_sync.h"

static const char *TAG = "nba_service";

#define SVC_TASK_STACK     8192
#define SVC_TASK_PRIO      3

/* A page is a fixed number of cards, not a calendar day: a busy slate spills
   over several pages, a quiet stretch pulls several days into one. Paging
   inside the cache is instant; crossing its edges fetches more games. */
#ifndef SVC_PAGE_SIZE
#define SVC_PAGE_SIZE 8
#endif
#define SVC_CACHE_PAGES 4                     /* page shown + 3 deeper */
#define SVC_CACHE_GAMES (SVC_CACHE_PAGES * SVC_PAGE_SIZE)

/* balldontlie answers 15 games per request no matter how wide the date window
   is, so the chunk width is tuned to how busy the slate is: 1 day in a busy
   stretch, up to a week in a quiet one. See svc_note_chunk_yield(). */
#define SVC_CHUNK_DAYS_INIT   2
#define SVC_CHUNK_DAYS_MIN    1
#define SVC_CHUNK_DAYS_MAX    7
#define SVC_CHUNK_PAGES       3  /* max cursor pages per chunk (3 x 15 games) */

#define SVC_HISTORY_DAYS     14  /* do not walk further back than this */
#define SVC_EMPTY_CHUNKS      2  /* empty chunks before "no more history" */
#define SVC_PAGE_CHUNKS       3  /* chunk fetches allowed to reach one page */
/* College off-season slate hunt: jump by a week up to a month, then walk
   back inside the bracket for the nearest day that answers. */
#define SVC_SLATE_PROBE_STEP  7
#define SVC_SLATE_PROBE_MAX  28

/* The free API tier rate-limits us (HTTP 429 after a handful of requests), so
   a failing fetch must not turn into a retry loop. */
#define SVC_BACKOFF_BASE_MS (15 * 1000)
#define SVC_BACKOFF_MAX_MS  (5 * 60 * 1000)

/* ESP-IDF's HTTPS stack keeps a little memory per connection (~200 B here),
 * so an always-on board loses heap slowly. Reboot long before a fetch could
 * OOM: it is invisible to the viewer and gets a clean heap back. */
#define SVC_MIN_FREE_HEAP (25 * 1024)

/* Depth the soak hook (CYD_NBA_AUTOPAGE) pages to before snapping back. */
#ifndef SVC_AUTOPAGE_DEPTH
#define SVC_AUTOPAGE_DEPTH 3
#endif

#define SVC_DAY_SECONDS 86400L

static SemaphoreHandle_t s_wake = NULL;
static bool s_running = false;
static bool s_dirty = false;        /* page moved or refresh requested */
static bool s_forced = false;       /* refresh button: refetch finished games too */
static bool s_rerender_only = false;/* redraw the current page from cache, no HTTP */
static bool s_have_cards = false;   /* are live cards on screen? */
static int  s_fail_streak = 0;

/* One feed on screen at a time (cards-screen header tabs). NBA rides
   balldontlie, the college leagues ride ESPN (svc_fetch_range dispatches).
   CYD_TEST_LEAGUE=N boots straight into another feed (1 = WNBA,
   2 = NCAAB, 3 = NCAAW) so it can be verified without a finger on the
   tab. */
#ifdef CYD_TEST_LEAGUE
static league_t s_league = (league_t)CYD_TEST_LEAGUE;
#else
static league_t s_league = LEAGUE_NBA;
#endif
static bool s_league_switched = false;  /* worker clears the board on the new feed */

static bool svc_is_college(void)
{
    return s_league == LEAGUE_NCAAB || s_league == LEAGUE_NCAAW;
}

/* Which season_type answered last time. "season_type omitted" means "every
   type except preseason", so October needs the preseason query; remembering
   the winner keeps a fetch down to one HTTPS connection per chunk. */
static bool s_preseason_first = false;

/* ---- fetched games, newest-first by kickoff time ----------------------
   s_base is how many *newer* games we have dropped from the cache, so page
   numbers stay put while the cache slides backwards through history. */
static nba_game_t s_cache[SVC_CACHE_GAMES];
static int  s_cache_count = 0;
static int  s_base = 0;
static time_t s_lo_day = 0;         /* oldest day fetched, midnight UTC */
static time_t s_hi_day = 0;         /* newest day fetched */
static int  s_chunk_days = SVC_CHUNK_DAYS_INIT;  /* width of the next older chunk */
/* College off-season (October: zero D1 games on ESPN until the November
   axis days): the newest edge would dead-end on an empty board. This is the
   nearest day ahead of today whose scoreboard answers, remembered so the
   probe costs its ~10 requests once, not every cycle. */
static time_t s_slate_day = 0;

/* ---- cross-reboot score cache (NVS) ------------------------------------
   balldontlie is the feed with a request budget (~5/min free), so its last
   page of games is parked in NVS: boot - or a league-tab switch - shows the
   most recent fetched page immediately, and a boot inside the refresh
   interval skips the first fetch entirely. ESPN has no published quota, so
   the college feeds are not persisted. One page (8 games ~2.6 KB) per
   balldontlie league keeps the 24 KB NVS partition comfortable. */
#define SVC_PERSIST_GAMES SVC_PAGE_SIZE
#define SVC_BLOB_MAGIC    0x53435631u   /* "SVC1" */

typedef struct {
    uint32_t magic;
    uint32_t game_size;      /* sizeof(nba_game_t): struct-drift tripwire */
    int64_t  saved_at;       /* epoch of this snapshot */
    int32_t  league;
    int32_t  count;
    nba_game_t games[SVC_PERSIST_GAMES];
} svc_blob_t;

static svc_blob_t s_blob;            /* mirror of what NVS holds for s_league */
static bool s_blob_valid = false;
static int  s_empty_chunks = 0;
static bool s_older_exhausted = false;

/* Page shown: 0 = the most recent games. */
static int  s_page = 0;
static int  s_shown_page = -1;

/* One API page of games, kept off the task stack (~6KB). */
#define SVC_API_SLOTS (NBA_API_MAX_GAMES > ESPN_API_MAX_GAMES ? NBA_API_MAX_GAMES \
                                                              : ESPN_API_MAX_GAMES)
static nba_game_t s_api_page[SVC_API_SLOTS];

/* ---- day helpers ------------------------------------------------------ */

static void day_to_iso(time_t day, char *buf, size_t len)
{
    struct tm tm;
    gmtime_r(&day, &tm);
    strftime(buf, len, "%Y-%m-%d", &tm);
}

static time_t iso_to_day(const char *iso)
{
    int y = 0, m = 0, d = 0;
    if (!iso || sscanf(iso, "%4d-%2d-%2d", &y, &m, &d) != 3) {
        return 0;
    }
    struct tm tm = { .tm_year = y - 1900, .tm_mon = m - 1, .tm_mday = d };
    time_t day = mktime(&tm);          /* TZ is UTC on this board */
    return day == (time_t)-1 ? 0 : day;
}

static time_t svc_today_day(void)
{
    time_t day = time_sync_game_day_epoch();
#ifdef CYD_TEST_DAY_SHIFT
    /* Test hook: point the board's "today" N days away, so an off-season
       board can be flashed against real game days
       (idf.py -DCYD_TEST_DAY_SHIFT=-286 build). */
    day += (time_t)CYD_TEST_DAY_SHIFT * SVC_DAY_SECONDS;
#endif
    return day;
}

static int svc_days_ago(time_t day)
{
    time_t today = svc_today_day();
    if (!day || !today) {
        return 0;
    }
    return (int)((today - day) / SVC_DAY_SECONDS);
}

/* ---- cache ------------------------------------------------------------- */

static bool game_newer(const nba_game_t *a, const nba_game_t *b)
{
    /* datetime is ISO 8601 UTC, so the string orders chronologically. */
    int cmp = strcmp(a->datetime, b->datetime);
    if (cmp != 0) {
        return cmp > 0;
    }
    return a->id > b->id;
}

/* Which end of the cache survives a merge that has to make room: the end the
   viewer is closest to. (A page move past the older edge gives a large offset,
   so it is the newest page that gets dropped.) */
static bool svc_keep_newer(void)
{
    int off = s_page * SVC_PAGE_SIZE - s_base;
    return off < s_cache_count / 2;
}

static void svc_cache_insert(const nba_game_t *g, bool keep_newer)
{
    while (s_cache_count >= SVC_CACHE_GAMES && s_cache_count > 1) {
        if (keep_newer) {
            s_cache_count--;        /* drop the oldest */
        } else {
            memmove(&s_cache[0], &s_cache[1],
                    (s_cache_count - 1) * sizeof(nba_game_t));
            s_cache_count--;
            s_base++;               /* one more newer game sits outside the cache */
        }
    }

    int at = s_cache_count;
    for (int i = 0; i < s_cache_count; i++) {
        if (!game_newer(&s_cache[i], g)) {
            at = i;
            break;
        }
    }
    if (at < s_cache_count) {
        memmove(&s_cache[at + 1], &s_cache[at],
                (s_cache_count - at) * sizeof(nba_game_t));
    }
    s_cache[at] = *g;
    s_cache_count++;
    if (at == 0 && s_base > 0) {
        s_base--;                   /* a game newer than the anchor came in */
    }
}

/* Merge a fetched API page into the cache: refresh games we already have (their
   scores move), add the rest, keep the list newest-first. */
static void svc_cache_merge(const nba_game_t *src, int count, bool keep_newer)
{
    for (int i = 0; i < count; i++) {
        int known = -1;
        for (int c = 0; c < s_cache_count; c++) {
            if (s_cache[c].id == src[i].id) {
                known = c;
                break;
            }
        }
        if (known >= 0) {
            s_cache[known] = src[i];
        } else {
            svc_cache_insert(&src[i], keep_newer);
        }
    }
}

/* The slice of the cache backing `page`, or false when the page is not fully
   cached. A short slice at the older end only counts once we know there is
   nothing older to fetch. */
static bool page_slice(int page, int *off_out, int *n_out)
{
    if (page < 0) {
        return false;
    }
    int start = page * SVC_PAGE_SIZE - s_base;
    if (start < 0 || start >= s_cache_count) {
        return false;
    }
    int n = SVC_PAGE_SIZE;
    if (start + n > s_cache_count) {
        if (!s_older_exhausted) {
            return false;
        }
        n = s_cache_count - start;
    }
    *off_out = start;
    *n_out = n;
    return true;
}

/* Deepest page we can show without fetching, for clamping after a failure. */
static int svc_cached_page_near(int page)
{
    int off, n;
    if (page_slice(page, &off, &n)) {
        return page;
    }
    for (int d = 1; d <= SVC_CACHE_PAGES + 1; d++) {
        if (page - d >= 0 && page_slice(page - d, &off, &n)) {
            return page - d;
        }
        if (page_slice(page + d, &off, &n)) {
            return page + d;
        }
    }
    return -1;
}

/* ---- fetching --------------------------------------------------------- */

/* Fetch [lo .. hi] with one season_type. balldontlie pages by game id, so a
   busy chunk needs the cursor to collect all of it; the id order says nothing
   about dates, so a partial walk would leave holes we could never fill - hence
   the whole chunk is always consumed. *pages_out reports how many HTTPS calls
   the chunk cost, which is what the chunk width is tuned against. */
static esp_err_t svc_fetch_range_season(time_t lo, time_t hi,
                                        const char *season, bool keep_newer,
                                        int *count_out, int *pages_out)
{
    char lo_iso[16], hi_iso[16];
    day_to_iso(lo, lo_iso, sizeof(lo_iso));
    day_to_iso(hi, hi_iso, sizeof(hi_iso));

    int total = 0;
    int cursor = 0;
    int next = 0;
    int pages = 0;

    for (int page_no = 0; page_no < SVC_CHUNK_PAGES; page_no++) {
        int count = 0;
        next = 0;
        pages++;
        const char *base = (s_league == LEAGUE_WNBA) ? nba_api_url_wnba()
                                                     : nba_api_url_nba();
        esp_err_t err = nba_api_fetch_games(base, lo_iso, hi_iso, season, cursor,
                                            s_api_page, NBA_API_MAX_GAMES,
                                            &count, &next);
        if (err != ESP_OK) {
            if (pages_out) {
                *pages_out = pages;
            }
            return total > 0 ? ESP_OK : err;   /* keep whatever we already got */
        }
        svc_cache_merge(s_api_page, count, keep_newer);
        total += count;
        if (next == 0) {
            break;
        }
        cursor = next;
    }

    if (pages_out) {
        *pages_out = pages;
    }
    *count_out = total;
    return ESP_OK;
}

/* ESPN's scoreboard answers one day per request (the ~10-20 network-covered
   games of that day, everything in March), so there is no cursor to chase:
   a [lo .. hi] window is just one fetch per day. Overlapping event lists on
   adjacent days are handled by the id-keyed merge. */
static esp_err_t svc_fetch_range_espn(time_t lo, time_t hi, bool keep_newer,
                                      int *count_out, int *pages_out)
{
    int total = 0;
    int pages = 0;
    esp_err_t err = ESP_OK;

    for (time_t day = hi; day >= lo; day -= SVC_DAY_SECONDS) {
        char iso[16];
        day_to_iso(day, iso, sizeof(iso));
        int count = 0;
        pages++;
        err = espn_api_fetch_games(s_league, iso, s_api_page,
                                   ESPN_API_MAX_GAMES, &count);
        if (err != ESP_OK) {
            if (total > 0) {
                err = ESP_OK;      /* keep whatever we already got */
            }
            break;
        }
        svc_cache_merge(s_api_page, count, keep_newer);
        total += count;
    }

    if (err == ESP_OK) {
        if (!s_lo_day || lo < s_lo_day) {
            s_lo_day = lo;
        }
        if (!s_hi_day || hi > s_hi_day) {
            s_hi_day = hi;
        }
    }
    *count_out = total;
    if (pages_out) {
        *pages_out = pages;
    }
    return err;
}

/* Fetch a date range into the cache, with the preseason fallback. */
static esp_err_t svc_fetch_range(time_t lo, time_t hi, bool keep_newer,
                                 int *count_out, int *pages_out)
{
    if (svc_is_college()) {
        return svc_fetch_range_espn(lo, hi, keep_newer, count_out, pages_out);
    }

    /* The preseason/season_type dance is an NBA quirk; WNBA accepts plain
       date ranges. */
    const char *first = (s_league == LEAGUE_NBA && s_preseason_first)
                        ? "preseason" : NULL;
    const char *fallback = (s_league != LEAGUE_NBA) ? NULL
                       : (s_preseason_first ? NULL : "preseason");
    int count = 0;
    int pages = 0;

    esp_err_t err = svc_fetch_range_season(lo, hi, first, keep_newer, &count, &pages);
    if (err == ESP_OK && count == 0) {
        err = svc_fetch_range_season(lo, hi, fallback, keep_newer, &count, &pages);
        if (err == ESP_OK && count > 0) {
            s_preseason_first = !s_preseason_first;
            ESP_LOGI(TAG, "switched season_type to %s",
                     s_preseason_first ? "preseason" : "default");
        }
    }

    if (err == ESP_OK) {
        if (!s_lo_day || lo < s_lo_day) {
            s_lo_day = lo;
        }
        if (!s_hi_day || hi > s_hi_day) {
            s_hi_day = hi;
        }
    }
    *count_out = count;
    if (pages_out) {
        *pages_out = pages;
    }
    return err;
}

/* One request answers 15 games whatever the window is, so the chunk width is
   tuned to how busy the slate is: 1 day in a busy stretch (regular season:
   ~12 games/day), up to a week in a quiet one (preseason: ~2 games/day).
   Either way a page step costs about one HTTPS call - the free tier only
   gives us five a minute. */
static void svc_note_chunk_yield(int games, int pages)
{
    if (svc_is_college()) {
        return;              /* one day per request: nothing to tune */
    }
    if (pages > 1 && s_chunk_days > SVC_CHUNK_DAYS_MIN) {
        s_chunk_days--;                  /* window cost more than one request */
    } else if (games * 2 < SVC_PAGE_SIZE && s_chunk_days < SVC_CHUNK_DAYS_MAX) {
        s_chunk_days *= 2;               /* thin window: cover more days per call */
    }
    ESP_LOGI(TAG, "chunk: %d game(s) in %d request(s) - window now %d day(s)",
             games, pages, s_chunk_days);
}

/* Does the ESPN scoreboard for `day` list any game? (Result lands in
   s_api_page as scratch; the chosen day is re-fetched through the merge.) */
static bool slate_day_has_games(time_t day)
{
    char iso[16];
    day_to_iso(day, iso, sizeof(iso));
    int count = 0;
    if (espn_api_fetch_games(s_league, iso, s_api_page,
                             ESPN_API_MAX_GAMES, &count) != ESP_OK) {
        return false;
    }
    return count > 0;
}

/* Nearest upcoming day-with-games ahead of `from`: jump by a week until a
   bracket answers, then walk back inside it. ~9 requests worst case, at
   espn_api's own 4 s pacing. */
static time_t svc_find_nearest_slate(time_t from)
{
    time_t hit = 0;
    for (int off = SVC_SLATE_PROBE_STEP; off <= SVC_SLATE_PROBE_MAX && !hit;
         off += SVC_SLATE_PROBE_STEP) {
        time_t day = from + (time_t)off * SVC_DAY_SECONDS;
        if (slate_day_has_games(day)) {
            hit = day;
        }
    }
    if (!hit) {
        return 0;
    }
    for (int back = SVC_SLATE_PROBE_STEP - 1; back >= 1; back--) {
        time_t day = hit - (time_t)back * SVC_DAY_SECONDS;
        if (day <= from) {
            break;
        }
        if (slate_day_has_games(day)) {
            return day;
        }
    }
    return hit;
}

/* Anchor the cache on the most recent games: pull the chunk ending today
   (older chunks while page 0 is still not full), then make its newest game the
   page-0 anchor. Nothing is thrown away up front, so a failed fetch leaves the
   games already on screen alone. */
static esp_err_t svc_fill_recent(void)
{
    time_t today = svc_today_day();
    if (!today) {
        return ESP_ERR_INVALID_STATE;
    }

    s_older_exhausted = false;
    s_empty_chunks = 0;

    time_t hi = today;
    if (svc_is_college()) {
        s_chunk_days = SVC_CHUNK_DAYS_MIN;  /* ESPN costs one request per day, always */
        if (s_slate_day > today) {
            hi = s_slate_day;               /* off-season: the slate beats an empty today */
        }
    }
    esp_err_t err = ESP_OK;
    int empty_walk = 0;

    for (int chunk = 0; chunk < SVC_PAGE_CHUNKS; chunk++) {
        time_t lo = hi - (s_chunk_days - 1) * SVC_DAY_SECONDS;
        int count = 0;
        int pages = 0;
        err = svc_fetch_range(lo, hi, true, &count, &pages);
        if (err != ESP_OK) {
            return err;
        }
        svc_note_chunk_yield(count, pages);
        if (count == 0) {
            empty_walk++;
            if (empty_walk >= SVC_EMPTY_CHUNKS) {
                /* The walk itself proved there is nothing older nearby: page 0
                   may be short now, and "<" may grey out. Without this, a
                   quiet slate (< SVC_PAGE_SIZE games) parks the cache with
                   games in it while page_slice refuses to show them. */
                s_older_exhausted = true;
                break;
            }
        } else {
            empty_walk = 0;
        }
        if (s_cache_count >= SVC_PAGE_SIZE) {
            break;                       /* page 0 is full */
        }
        hi = lo - SVC_DAY_SECONDS;
    }

    /* College newest edge still empty after the whole fill: hunt for the
       nearest upcoming slate so the board shows scheduled games (tip text)
       instead of a dead end. NBA/WNBA never probe - balldontlie is the one
       with the request budget. */
    if (err == ESP_OK && svc_is_college() && s_cache_count == 0) {
        time_t slate = svc_find_nearest_slate(today);
        if (slate) {
            s_slate_day = slate;
            ESP_LOGI(TAG, "no games near today - anchoring on nearest slate, day +%d",
                     (int)((slate - today) / SVC_DAY_SECONDS));
            int count = 0, pages = 0;
            err = svc_fetch_range(slate, slate, true, &count, &pages);
            /* A forward probe means no season reached backwards either -
               let the short slate render and grey out "<". */
            s_older_exhausted = true;
        } else {
            s_slate_day = 0;
            ESP_LOGI(TAG, "no slate within %d days either", SVC_SLATE_PROBE_MAX);
        }
    }

    s_base = 0;                          /* cache[0] is the newest game again */
    return err;
}

/* Grow the cache backwards by one chunk of days. */
static esp_err_t svc_fetch_older_chunk(int *count_out)
{
    time_t hi = (s_lo_day ? s_lo_day : svc_today_day()) - SVC_DAY_SECONDS;
    time_t lo = hi - (s_chunk_days - 1) * SVC_DAY_SECONDS;

    if (svc_days_ago(lo) > SVC_HISTORY_DAYS) {
        ESP_LOGI(TAG, "history limit reached (%d days)", SVC_HISTORY_DAYS);
        s_older_exhausted = true;
        *count_out = 0;
        return ESP_OK;
    }

    int pages = 0;
    esp_err_t err = svc_fetch_range(lo, hi, false, count_out, &pages);
    if (err != ESP_OK) {
        return err;
    }
    svc_note_chunk_yield(*count_out, pages);
    if (*count_out == 0) {
        s_empty_chunks++;
        if (s_empty_chunks >= SVC_EMPTY_CHUNKS) {
            ESP_LOGI(TAG, "no games in %d chunk(s) back - no more history",
                     s_empty_chunks);
            s_older_exhausted = true;
        }
    } else {
        s_empty_chunks = 0;
    }
    return err;
}

/* Make sure `page` is in the cache, fetching as needed. `page` may change:
   paging past the newest end (games the cache dropped) re-anchors on the most
   recent games instead, which is the cheap way back to "now". */
static esp_err_t svc_reach_page(int *page_io)
{
    int page = *page_io;

    for (int attempt = 0; attempt <= SVC_PAGE_CHUNKS; attempt++) {
        int off, n;
        if (page_slice(page, &off, &n)) {
            *page_io = page;
            return ESP_OK;
        }

        if (page * SVC_PAGE_SIZE - s_base < 0) {
            ESP_LOGI(TAG, "page %d is newer than the cache - re-anchoring on recent games",
                     page + 1);
            esp_err_t err = svc_fill_recent();
            *page_io = 0;
            return err;
        }

        if (s_older_exhausted) {
            return ESP_ERR_NOT_FOUND;
        }

        int count = 0;
        esp_err_t err = svc_fetch_older_chunk(&count);
        if (err != ESP_OK) {
            return err;
        }
        if (count == 0 && s_older_exhausted) {
            return ESP_ERR_NOT_FOUND;
        }
    }

    ESP_LOGW(TAG, "page %d still not cached after %d chunk fetches",
             page + 1, SVC_PAGE_CHUNKS);
    *page_io = page;
    return ESP_ERR_INVALID_STATE;
}

/* Re-fetch the day(s) the current page's games belong to: moves scores and
   clocks on games that are still live. */
static esp_err_t svc_refetch_page(int page)
{
    int off, n;
    if (!page_slice(page, &off, &n)) {
        return ESP_ERR_NOT_FOUND;
    }

    time_t newest_day = iso_to_day(s_cache[off].date);
    time_t oldest_day = iso_to_day(s_cache[off + n - 1].date);
    if (!newest_day) {
        return ESP_ERR_INVALID_STATE;
    }

    int count = 0;
    esp_err_t err = svc_fetch_range(oldest_day ? oldest_day : newest_day,
                                    newest_day, svc_keep_newer(), &count, NULL);
    return err;
}

/* Does this page hold games that can still change? */
static bool page_is_live(int page)
{
    int off, n;
    if (!page_slice(page, &off, &n)) {
        return false;
    }
    for (int i = 0; i < n; i++) {
        if (strcmp(s_cache[off + i].status_state, "final") != 0) {
            return true;
        }
    }
    return false;
}

/* ---- UI --------------------------------------------------------------- */

static void svc_page_info(int page, cards_page_t *info, int off, int n)
{
    info->page = page + 1;
    info->has_later = page > 0;
    info->has_earlier = !s_older_exhausted;
    info->iso_newest = (n > 0) ? s_cache[off].date : NULL;
    info->iso_oldest = (n > 0) ? s_cache[off + n - 1].date : NULL;
}

static void svc_show_loading(void)
{
    cards_page_t info;
    memset(&info, 0, sizeof(info));
    info.page = s_page + 1;
    info.has_later = s_page > 0;
    info.has_earlier = !s_older_exhausted;

    lvgl_port_lock(0);
    cards_screen_show_message("Loading...", &info);
    lvgl_port_unlock();

    /* A failed fetch must not leave the previous page's scores up. */
    s_have_cards = false;
}

static void svc_render(int page, esp_err_t err)
{
    int off = 0, n = 0;
    /* Cached games are worth showing even after a failed fetch - err only picks
       which message to show when there is nothing cached to fall back on. */
    bool ok = page_slice(page, &off, &n) && n > 0;

    cards_page_t info;
    svc_page_info(page, &info, off, ok ? n : 0);

    /* cards_screen_page_visible() reads LVGL state, so it belongs inside the
       port lock like every other LVGL touch from this task. */
    lvgl_port_lock(0);

    if (!cards_screen_page_visible()) {
        /* Some other screen (the game detail view) owns the display: building a
           page of cards behind it would eat the LVGL pool the detail view is
           using. The board asks for a re-render when it comes back. */
        s_have_cards = false;
        s_shown_page = page;
        lvgl_port_unlock();
        return;
    }

    if (ok) {
        cards_screen_show_games(&s_cache[off], n, &info);
        s_have_cards = true;
    } else if (!s_have_cards) {
        cards_screen_show_message(
            err != ESP_OK ? "No data - check Wi-Fi"
                          : svc_is_college() ? "No college games around this date"
                                             : "No games to display. Check Wi-Fi.",
            &info);
    }
    /* else: keep what is on screen - a failed auto-refresh should not blank it */
    lvgl_port_unlock();

    s_shown_page = page;
}

/* Which leagues get an NVS snapshot (NULL = do not persist). */
static const char *svc_blob_key(league_t lg)
{
    switch (lg) {
        case LEAGUE_NBA:  return "games_nba";
        case LEAGUE_WNBA: return "games_wnba";
        default:          return NULL;
    }
}

/* Put the NVS snapshot of this league's newest page into the cache, so the
   board shows real scores while the worker is still dialing out (and can
   even skip that first fetch when the snapshot is fresh). */
static void svc_cache_load(void)
{
    const char *key = svc_blob_key(s_league);
    if (!key || s_cache_count > 0) {
        return;
    }
    if (nvs_settings_load_blob(key, &s_blob, sizeof(s_blob)) != ESP_OK) {
        return;                       /* first boot, or struct changed size */
    }
    if (s_blob.magic != SVC_BLOB_MAGIC ||
        s_blob.game_size != sizeof(nba_game_t) ||
        s_blob.league != (int32_t)s_league ||
        s_blob.count <= 0 || s_blob.count > SVC_PERSIST_GAMES ||
        s_blob.saved_at < 1577836800) {   /* predates 2020: garbage */
        ESP_LOGW(TAG, "score cache blob invalid - ignoring");
        return;
    }
    memcpy(s_cache, s_blob.games, (size_t)s_blob.count * sizeof(nba_game_t));
    s_cache_count = s_blob.count;
    s_base = 0;
    /* The snapshot knows nothing older than itself; the fill walk re-earns
       (or reclaims) the flag as soon as it runs. */
    s_older_exhausted = true;
    s_blob_valid = true;
    /* No age in this line: the clock is not synced yet at boot. The
       fresh-skip log later (post-sync) reports the age. */
    ESP_LOGI(TAG, "score cache: loaded %d game(s) from NVS", s_cache_count);
}

/* Park the newest page in NVS after a successful cycle - but only when it
   actually changed (a finished slate must not burn a flash write per 15 min). */
static void svc_persist(esp_err_t err)
{
    const char *key = svc_blob_key(s_league);
    if (!key || err != ESP_OK || s_cache_count == 0) {
        return;
    }
    int keep = s_cache_count < SVC_PERSIST_GAMES ? s_cache_count : SVC_PERSIST_GAMES;
    if (s_blob_valid && s_blob.count == keep &&
        memcmp(s_blob.games, s_cache, (size_t)keep * sizeof(nba_game_t)) == 0) {
        return;
    }
    s_blob.magic = SVC_BLOB_MAGIC;
    s_blob.game_size = sizeof(nba_game_t);
    s_blob.saved_at = (int64_t)time(NULL);
    s_blob.league = (int32_t)s_league;
    s_blob.count = keep;
    memcpy(s_blob.games, s_cache, (size_t)keep * sizeof(nba_game_t));
    if (nvs_settings_save_blob(key, &s_blob, sizeof(s_blob)) == ESP_OK) {
        s_blob_valid = true;
        ESP_LOGI(TAG, "score cache: %d game(s) saved to NVS", keep);
    } else {
        s_blob_valid = false;
        ESP_LOGW(TAG, "score cache save failed");
    }
}

/* ---- worker ----------------------------------------------------------- */

static void svc_task(void *arg)
{
    (void)arg;

    svc_cache_load();          /* boot with the last session's page on screen */

    for (;;) {
        /* Sampled a full interval after the previous connection closed, so
           lwIP's deferred socket teardown has already returned its memory:
           a downward trend here is a real leak, not async cleanup. */
        uint32_t free_heap = esp_get_free_heap_size();
        ESP_LOGI(TAG, "cycle start, free heap: %u, page %d, cache %d game(s)",
                 free_heap, s_page + 1, s_cache_count);
        if (free_heap < SVC_MIN_FREE_HEAP) {
            ESP_LOGE(TAG, "low heap (%u) - restarting for a clean pool", free_heap);
            vTaskDelay(pdMS_TO_TICKS(500));
            esp_restart();
        }

        esp_err_t err = ESP_OK;
        int off = 0, n = 0;

        /* Every fetch is anchored on the game day, so the clock has to be set
           first (it comes from the Date header of an HTTPS GET). */
        if (!time_sync_is_synced()) {
            time_sync_from_http();
        }

        if (s_league_switched) {
            /* Wipe the previous feed's cards before the new league's games
               come in, so stale NBA scores cannot masquerade as college. */
            s_league_switched = false;
            s_blob_valid = false;        /* the mirror belonged to the old feed */
            svc_show_loading();
            svc_cache_load();            /* ...unless NVS has this feed's page */
        }

        /* Where this cycle came from: a page move or the refresh button sets
           s_dirty; the periodic refresh is simply a cycle woken by the timeout. */
        bool poked = s_dirty;
        bool forced = s_forced;
        s_dirty = false;
        s_forced = false;

        if (poked && s_rerender_only) {
            s_rerender_only = false;
            /* The board gave its page to the detail view and wants it back:
               svc_render() below rebuilds it from the cache, no fetch. */
        } else if (poked && s_shown_page != s_page) {
            svc_show_loading();
            int page = s_page;
            err = svc_reach_page(&page);
            s_page = page;
            if (err != ESP_OK) {
                /* Keep the board useful: show the nearest page we already
                   have. The error stays set, so the backoff below still
                   applies and a failing page move cannot turn into a
                   request loop. */
                int fallback = svc_cached_page_near(page);
                if (fallback >= 0) {
                    ESP_LOGW(TAG, "page %d unavailable - showing page %d",
                             page + 1, fallback + 1);
                    s_page = fallback;
                }
            }
        } else if (s_cache_count == 0 && !s_older_exhausted) {
            err = svc_fill_recent();                   /* first load, or a retry */
        } else if (s_page == 0) {
            /* Showing the most recent games: re-anchor on today so games that
               just started - or a slate that appeared since the last look -
               come in. Refreshing only the current page's days would keep
               re-reading a finished day forever.
               Fresh NVS snapshot (boot right after a reboot, for instance)
               buys one interval of skipped API calls - but never a manual
               refresh button press. */
            bool fresh = !poked && s_blob_valid && time_sync_is_synced() &&
                         (int64_t)time(NULL) - s_blob.saved_at <
                             NBA_SERVICE_REFRESH_MS / 1000;
            if (fresh) {
                ESP_LOGI(TAG, "score cache is %lld s old - skipping this fetch",
                         (long long)time(NULL) - (long long)s_blob.saved_at);
            } else {
                err = svc_fill_recent();
            }
        } else if (forced || page_is_live(s_page)) {
            err = svc_refetch_page(s_page);            /* deeper page: refresh it */
        } else if (poked) {
            ESP_LOGI(TAG, "page %d shows finished games - not refetching",
                     s_page + 1);
        }

        if (page_slice(s_page, &off, &n)) {
            ESP_LOGI(TAG, "page %d: %d card(s) %s..%s (free heap: %u)",
                     s_page + 1, n, s_cache[off].date, s_cache[off + n - 1].date,
                     esp_get_free_heap_size());
        }
        svc_render(s_page, err);
        svc_persist(err);

        if (err == ESP_OK) {
            s_fail_streak = 0;
        } else {
            int backoff_ms = SVC_BACKOFF_BASE_MS << (s_fail_streak > 4 ? 4 : s_fail_streak);
            if (backoff_ms > SVC_BACKOFF_MAX_MS) {
                backoff_ms = SVC_BACKOFF_MAX_MS;
            }
            if (s_fail_streak < 5) {
                s_fail_streak++;
            }
            ESP_LOGW(TAG, "fetch failed (%s) - waiting %d s before retrying",
                     esp_err_to_name(err), backoff_ms / 1000);
            vTaskDelay(pdMS_TO_TICKS(backoff_ms));
        }

#ifdef CYD_NBA_AUTOPAGE
        /* Soak hook: walk the pager without touching the screen, stepping back
           to the newest page afterwards. Only on success, so a rate-limited or
           offline board does not hammer the API. */
        if (err == ESP_OK) {
            if (s_page >= SVC_AUTOPAGE_DEPTH) {
                nba_service_step_page(-SVC_AUTOPAGE_DEPTH);
            } else {
                nba_service_step_page(1);
            }
        }
#endif

#ifdef CYD_NBA_AUTODETAIL
        /* Soak hook: open the detail view for the newest game on the page, and
           come back on the next cycle, so the whole tap-through round trip
           (card page dropped, pool handover, re-render from cache on the way
           back) gets exercised without a finger on the glass. */
        static int autodelta_phase = 0;
        if (err == ESP_OK && autodelta_phase < 2 && s_cache_count > 0) {
            int o = 0, cnt = 0;
            if (autodelta_phase == 0) {
                if (page_slice(s_page, &o, &cnt) && cnt > 0) {
                    autodelta_phase = 1;
                    lvgl_port_lock(0);
                    cards_screen_open_detail(&s_cache[o]);
                    lvgl_port_unlock();
                    ESP_LOGI(TAG, "autodelta: detail open, pool handed over");
                }
            } else {
                autodelta_phase = 2;
                lvgl_port_lock(0);
                if (!cards_screen_page_visible()) {
                    game_detail_screen_back();
                }
                lvgl_port_unlock();
                ESP_LOGI(TAG, "autodelta: back to the board (expect a re-render)");
            }
        }
#endif

        /* Sleep until the next periodic refresh, or until the header button or
           a page-pager button gives the semaphore. */
        if (xSemaphoreTake(s_wake, pdMS_TO_TICKS(NBA_SERVICE_REFRESH_MS))) {
            ESP_LOGI(TAG, "immediate refresh requested");
        }
    }
}

void nba_service_start(void)
{
    if (s_running) {
        return;
    }

    if (!s_wake) {
        s_wake = xSemaphoreCreateBinary();
        if (!s_wake) {
            ESP_LOGE(TAG, "failed to create wake semaphore");
            return;
        }
    }

    if (xTaskCreate(svc_task, "nba_service", SVC_TASK_STACK, NULL,
                    SVC_TASK_PRIO, NULL) != pdPASS) {
        ESP_LOGE(TAG, "failed to create worker task");
        return;
    }
    s_running = true;
    ESP_LOGI(TAG, "score feed worker started (%d cards/page, refresh every %d min)",
             SVC_PAGE_SIZE, NBA_SERVICE_REFRESH_MS / 60000);
}

static void svc_wake(void)
{
    if (!s_wake) {
        return;
    }
    if (xSemaphoreGive(s_wake) != pdTRUE) {
        ESP_LOGD(TAG, "wake already pending");
    }
}

void nba_service_request_refresh(void)
{
    s_forced = true;
    s_dirty = true;
    svc_wake();
}

/* Switch the feed shown on the board. The cache, pager and window state all
   belong to the current feed, so they are reset; the worker then shows
   "Loading..." and re-anchors the new league on its most recent games.
   Called from an LVGL event callback, which already holds the port mutex -
   no LVGL calls here, that happens on the worker (s_league_switched). */
void nba_service_set_league(league_t lg)
{
    if (lg == s_league) {
        return;
    }
    s_league = lg;
    s_cache_count = 0;
    s_base = 0;
    s_lo_day = 0;
    s_hi_day = 0;
    s_chunk_days = svc_is_college() ? 1 : SVC_CHUNK_DAYS_INIT;
    s_slate_day = 0;
    s_empty_chunks = 0;
    s_older_exhausted = false;
    s_preseason_first = false;
    s_fail_streak = 0;
    s_page = 0;
    s_shown_page = s_page;   /* not a page move: the branch chain re-anchors */
    s_dirty = true;
    s_league_switched = true;
    ESP_LOGI(TAG, "switching feed to %s",
             lg == LEAGUE_NCAAW ? "NCAAW" : lg == LEAGUE_NCAAB ? "NCAAB"
                          : lg == LEAGUE_WNBA ? "WNBA" : "NBA");
    svc_wake();
}

league_t nba_service_league(void)
{
    return s_league;
}

/* Re-render the current page from the cache with no HTTP: the board hands its
   LVGL pool to the game detail view and asks for the page back afterwards. */
void nba_service_rerender(void)
{
    s_have_cards = false;
    s_rerender_only = true;
    s_dirty = true;
    svc_wake();
}

void nba_service_step_page(int delta)
{
    int next = s_page + delta;
    if (next < 0) {
        next = 0;
    }
    if (next == s_page) {
        return;
    }

    s_page = next;
    s_dirty = true;
    ESP_LOGI(TAG, "paging %s: page %d", delta > 0 ? "older" : "newer", s_page + 1);
    svc_wake();
}

int nba_service_page(void)
{
    return s_page;
}

bool nba_service_is_running(void)
{
    return s_running;
}
