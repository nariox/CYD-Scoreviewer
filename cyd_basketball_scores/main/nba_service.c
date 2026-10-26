#include "nba_service.h"

#include <stdio.h>
#include <string.h>
#include <limits.h>
#include <time.h>

#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "nvs_settings.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "cards_screen.h"
#include "espn_api.h"
#include "game_detail_screen.h"
#include "mbedtls_arena.h"
#include "splash_screen.h"
#include "time_sync.h"
#include "wifi_backend.h"

static const char *TAG = "nba_service";

#define SVC_TASK_STACK     8192
#define SVC_TASK_PRIO      3

/* A page is a fixed number of cards, not a calendar day: a busy slate spills
   over several pages, a quiet stretch pulls several days into one. Paging
   inside the cache is instant; crossing its edges fetches more games.
   3 is the adopted shape (device-proven, see tools/mbedtls_opt README):
   3 x 46 px + 2 x 8 px gap = 154 px inside the 164 px card band, so a page
   never scrolls, and the shrunken render/RAM footprint is what pays for
   the 44 KB mbedtls arena + a 48 KB LVGL pool (3 cards = 75% pool). */
#ifndef SVC_PAGE_SIZE
#define SVC_PAGE_SIZE 3
#endif
#define SVC_CACHE_PAGES 4                     /* page shown + 3 deeper */
#define SVC_CACHE_GAMES (SVC_CACHE_PAGES * SVC_PAGE_SIZE)

/* The tab page boxes hold two pages each: revisit lands on the remembered
   page (0 or 1) instantly, and the third step onward is the only one that
   can touch the network. (NVS snapshots stay at one page - partition GC
   headroom.)
   Overridable (-DCYD_NBA_BOX_GAMES=N). Default depth 4 pages = 12 games:
   every tab keeps 4 instantly-pageable games, the RAM figure that lets the
   44 KB arena link in production shape. */
#ifndef SVC_BOX_GAMES
#define SVC_BOX_GAMES (4 * SVC_PAGE_SIZE)
#endif

/* ESPN's scoreboard answers one day per request (any league), so a chunk is
   a single day; walking "older" is walking days. HISTORY covers a finished
   league's real tail: the WNBA Finals span ~3 weeks and the NBA preseason
   starts ~3.5 weeks back (the balldontlie-era 14 clipped the Finals' early
   rounds). It costs nothing to widen - the empty-streak rule below ends a
   false-bottom walk after 5 requests long before this line. */
#define SVC_HISTORY_DAYS     35  /* do not walk further back than this */
#define SVC_EMPTY_CHUNKS      5  /* DISTINCT empty days before "no more
                                  * history". Was 2 (balldontlie quota era);
                                  * a finished league's newest edge is dead
                                  * calendar immediately (WNBA: 10 empty days
                                  * the week after the Finals) and spaced
                                  * slates - playoff rest days, round gaps,
                                  * ESPN's network-only college selection -
                                  * legitimately span 3-5 empty days. The
                                  * fill walk's 3-day budget stays the cheap
                                  * newest-edge probe. */
#define SVC_PAGE_CHUNKS       3  /* day fetches allowed to reach one page */
/* Idle budget: after a successful cycle the worker quietly walks this many
   more days into the cache so the user's next taps land on 0 ms renders.
   ESPN has no request quota; the 4 s pacing inside espn_api keeps it polite,
   and every extra day is one more invisible background request. */
#define SVC_IDLE_PREFETCH_DAYS 3
/* College off-season slate hunt: jump by a week up to a month, then walk
   back inside the bracket for the nearest day that answers. (Pro leagues
   can use it too during their September dead zone.) */
#define SVC_SLATE_PROBE_STEP  7
#define SVC_SLATE_PROBE_MAX  28

/* A failing fetch must not turn into a retry loop (Wi-Fi down, ESPN edge
   hiccup): failures back off instead of hammering. (ESPN has no 429 quota
   like the old balldontlie tier had, but politeness and battery agree.) */
#define SVC_BACKOFF_BASE_MS (15 * 1000)
#define SVC_BACKOFF_MAX_MS  (5 * 60 * 1000)

/* Wake re-check interval while offline (see the warm-up gate in svc_task):
   CONNECTED events poke s_wake and land instantly; the poll only guards
   against a poke consumed by a pass that ignored it. */
#define SVC_NET_WAIT_POLL_MS (10 * 1000)

/* ESP-IDF's HTTPS stack keeps a little memory per connection (~200 B here),
 * so an always-on board loses heap slowly. Reboot long before a fetch could
 * OOM: it is invisible to the viewer and gets a clean heap back. */
#define SVC_MIN_FREE_HEAP (25 * 1024)

/* The TLS-storm floors. Device-measured (2026-10 stress soak,
   -DCYD_NBA_TLSSTRESS): fresh handshakes start failing around 45 K free heap
   with PSA -141 (INSUFFICIENT_MEMORY, mislabeled "PSA signature verification
   failed" by esp-x509-crt-bundle), and below ~41 K no handshake succeeds at
   all until a reboot - the ESP's crypto layer is out of memory, the CDN is
   innocent. The post-pre-warm baseline is ~46-47 K and esp-tls keeps a couple
   of hundred bytes per connection, so ordinary refresh days used to walk
   across that line: the once-a-day "cert storm". The floors keep the feed
   away from it; the storm floor heals with a restart, and cannot reboot-loop
   (post-boot heap is ~54 K, well above the floor - it takes a day of drift to
   arm again).
   Both floors are STOCK-ALLOCATOR logic: they guard the band where a fresh
   handshake can fail. With the mbedtls arena installed (mbedtls_arena_active)
   handshakes draw from .bss instead, the boot pool sits below the band by
   design, and both floors are skipped - there they would only misread
   ordinary network outages as storms. */
#define SVC_STORM_HEAP_FLOOR    (46 * 1024)   /* fail streak here -> restart   */
#define SVC_PREWARM_HEAP_FLOOR  (48 * 1024)   /* below: defer the pre-warm     */

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

/* One feed on screen at a time (cards-screen header tabs); all four are
   ESPN scoreboards now (espn_api dispatches the paths). CYD_TEST_LEAGUE=N
   boots straight into another feed (1 = WNBA, 2 = NCAAB, 3 = NCAAW) so it
   can be verified without a finger on the tab. */
#ifdef CYD_TEST_LEAGUE
static league_t s_league = (league_t)CYD_TEST_LEAGUE;
#else
static league_t s_league = LEAGUE_NBA;
#endif
static bool s_league_switched = false;  /* worker applies the queued feed switch */

/* Tab taps land on the LVGL task; everything they touch - s_league itself,
   the cache, the pager - belongs to the worker, which re-reads it per request.
   A tap therefore only parks the target here (plus the flags below); the
   worker applies it at a cycle boundary and drops any in-flight cycle at its
   next request. (The old design mutated s_league/cache straight from the tab
   callback: a tap during a running fill let that fill merge the old league's
   games into the freshly-wiped cache, and the cycle rendered them under the
   new tab - the "NCAA shows NBA games" bug. The pre-warm made the window big
   enough to hit it in normal use.) */
static volatile int s_pending_league = -1;   /* league_t, or -1 for none */

/* Where the viewer left off in each tab (worker-owned, indexed by league_t).
   A revisit paints the remembered page from the tab's box when the box still
   reaches it, instead of always snapping back to page 1. */
static int s_last_page[4];

static bool svc_switch_pending(void)
{
    return s_pending_league >= 0;
}

/* ---- fetched games, newest-first by kickoff time ----------------------
   s_base is how many *newer* games we have dropped from the cache, so page
   numbers stay put while the cache slides backwards through history. */
static nba_game_t s_cache[SVC_CACHE_GAMES];
static int  s_cache_count = 0;
static int  s_base = 0;
static time_t s_lo_day = 0;         /* oldest day fetched, midnight UTC */
static time_t s_hi_day = 0;         /* newest day fetched */
static int    s_lo_day_games = -1;  /* games ESPN reported for s_lo_day's day
                                     * (-1 = unknown): tells a walk floor whose
                                     * tail the re-anchor merge chopped off from
                                     * a complete one, so it can be refetched. */
static bool   s_lo_day_repair_used; /* that refetch is once per floor day (a day
                                     * bigger than the window can never "fill"
                                     * it - without this the repair would bounce
                                     * the window inside the day forever). */
/* Set of days the scoreboard answered EMPTY this session, as a 64-day
 * bitmap anchored to s_empty_epoch (the US game day at first record, pinned
 * so the bits stay glued to ABSOLUTE days across midnight). An answer, not
 * a gap: a walk step landing on a known day steps below it without a
 * request, and the empty STREAK only counts days outside the set. The
 * window covers [-SVC_EMPTY_AHEAD .. +35] days around the epoch - the slate
 * hunt's +28-day horizon through SVC_HISTORY_DAYS behind - every day a
 * feeder can reach. The contiguous single run this replaces died two ways:
 * it ping-ponged under the WNBA Finals cadence (probe Oct 23, probe Oct 22,
 * snap to the floor, re-probe Oct 23 - "no games in 5 chunk(s)" after 2
 * real days), and a disjoint day had nowhere to be recorded: once the
 * slate hunt anchored the window on Nov 1 above the boot's Oct 23..25
 * block, the idle prefetch re-probed Oct 31 every iteration, forever
 * (2026-10 NCAAB slate, see note_empty_day). */
#define SVC_EMPTY_AHEAD  28  /* future days addressable (== hunt horizon) */
static uint64_t s_empty_days;
static time_t   s_empty_epoch;
/* Dead zone between slates (college October, pro September): the newest edge
   would dead-end on an empty board. Per league (indexed by league_t): the
   nearest day ahead of today whose scoreboard answers, remembered so the
   probe costs its ~10 requests once, not every cycle - and so the boot
   pre-warm can hand a probed tab its anchor too. A FAILED hunt is remembered
   too (s_slate_none_at): a finished league (WNBA after the finals) would
   otherwise re-probe the same four empty days every single cycle. */
static time_t s_slate_day[LEAGUE_NCAAW + 1];
static time_t s_slate_none_at[LEAGUE_NCAAW + 1];  /* last failed hunt's 'from' */

/* ---- cross-reboot score cache (NVS) ------------------------------------
    Every league's newest games are parked in NVS: boot - or a league-tab
    switch - shows the most recent fetched page immediately, and a boot
    inside the refresh interval skips the first fetch entirely. The pro
    leagues snapshot the full 4-page box; college snapshots ONE page
    (user call 2026-10: "even 3 games would be sufficient") - the 24 KB
    partition's GC headroom cannot take two more 4-page entries, and one
    page is all a reboot's cold tab needs before the refill lands. */
/* NVS snapshot depth (pro): default = the full box (12 games), so the "12
   preloaded" board survives reboot at 4 pages (~4.4 KB per pro league,
   fits the partition; watch headroom if PAGE_SIZE grows past 3).
   Overridable with -DCYD_NBA_PERSIST_GAMES=N. */
#ifndef SVC_PERSIST_GAMES
#define SVC_PERSIST_GAMES (4 * SVC_PAGE_SIZE)
#endif
/* College snapshot depth: one page (~1.1 KB per league). */
#ifndef SVC_PERSIST_GAMES_COLLEGE
#define SVC_PERSIST_GAMES_COLLEGE SVC_PAGE_SIZE
#endif
#define SVC_BLOB_MAGIC    0x53435633u   /* "SVC3": v2 snapshots hold UTC-dated game days (see game_day_from_stamp) */

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

#ifdef CYD_NBA_PAGEMON
/* Measurement hook (-DCYD_NBA_PAGEMON=1): step_page stamps, svc_render
   reports. Measures the worker-side latency of a page step (queue wake +
   slice + widget build + refresh); the LVGL button callback calls
   step_page synchronously, so this is press-to-pixels minus a microsecond.
   Written from both the LVGL task and the worker - it is a stopwatch, a
   torn read at worst skims one sample. */
static volatile uint32_t s_step_ms;
#endif

/* One scoreboard day of games, kept off the task stack (~8KB). */
static nba_game_t s_api_page[ESPN_API_MAX_GAMES];

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

/* Insert one game newest-first. max_base caps the head-eviction slide:
   a walk aiming at a page may push the window floor to that page's
   neighborhood but NEVER past it - with a busy day bigger than the gap
   the per-game eviction slid s_base beyond the requested page's start,
   page_slice could then never cover it, and svc_reach_page read that as
   "newer than the cache": re-anchor on today, wipe the games the walk
   just fetched (2026-10 device bug: page 5 unreachable as soon as ONE
   day filled the 12-game window - Oct 22 gave 2 games, Oct 21 gave 11,
   s_base overshot 0 -> 13 > page start 12). Once at the cap, older
   incoming games fall off the tail instead of evicting the head; a game
   NEWER than the head still replaces it (the eviction ++ and the at==0
   -- pair keeps s_base put). Callers that may slide freely pass
   INT_MAX - the pre-fix behavior. */
static void svc_cache_insert(const nba_game_t *g, bool keep_newer, int max_base)
{
    if (!keep_newer && s_base >= max_base &&
        s_cache_count >= SVC_CACHE_GAMES && s_cache_count > 0 &&
        !game_newer(g, &s_cache[0])) {
        return;                          /* older than the capped window */
    }

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
   scores move), add the rest, keep the list newest-first. max_base caps the
   slide (see svc_cache_insert). */
static void svc_cache_merge(const nba_game_t *src, int count, bool keep_newer,
                            int max_base)
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
            svc_cache_insert(&src[i], keep_newer, max_base);
        }
    }
}

/* The slice of the cache backing `page`, or false when the page is not fully
   cached. A short slice at the OLDER end only counts once we know there is
   nothing older to fetch. The newest page (start 0) always renders what the
   cache holds: it is the newest board there is (2026-10 WNBA - a finished
   league's last game day is 1-2 games, and refusing the short page unless
   exhausted conflated "show them" with "no more history", so the 2-empty-day
   rule alone decided whether the tab showed a board at all). */
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
        if (start > 0 && !s_older_exhausted) {
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

/* Fetch [lo .. hi] into the cache. ESPN's scoreboard answers one day per
   request (any league), so the window is walked day by day - no cursor to
   chase, and the id-keyed merge absorbs lists that overlap across adjacent
   days. Pacing (4 s) lives inside espn_api. */
static esp_err_t svc_fetch_range(time_t lo, time_t hi, bool keep_newer,
                                 int max_base, int *count_out, int *pages_out)
{
    int total = 0;
    int pages = 0;
    esp_err_t err = ESP_OK;

    for (time_t day = hi; day >= lo; day -= SVC_DAY_SECONDS) {
        if (svc_switch_pending()) {
            break;                 /* tab tap: stop at this request boundary */
        }
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
        svc_cache_merge(s_api_page, count, keep_newer, max_base);
        total += count;
    }

    if (err == ESP_OK) {
        if (!s_lo_day || lo < s_lo_day) {
            s_lo_day = lo;
            s_lo_day_repair_used = false;   /* fresh floor day, fresh repair budget */
        }
        if (!s_hi_day || hi > s_hi_day) {
            s_hi_day = hi;
        }
        if (lo == hi && lo == s_lo_day) {
            s_lo_day_games = total;   /* exactly what ESPN gave for the walk floor */
        }
    }
    *count_out = total;
    if (pages_out) {
        *pages_out = pages;
    }
    return err;
}

/* Does the ESPN scoreboard for `day` list any game? 1 yes, 0 empty, -1 fetch
   error. (Result lands in s_api_page as scratch; the chosen day is re-fetched
   through the merge.) Takes the league explicitly: the boot pre-warm probes
   leagues other than the one on screen. */
static int slate_day_has_games(league_t lg, time_t day)
{
    char iso[16];
    day_to_iso(day, iso, sizeof(iso));
    int count = 0;
    if (espn_api_fetch_games(lg, iso, s_api_page,
                             ESPN_API_MAX_GAMES, &count) != ESP_OK) {
        return -1;
    }
    return count > 0 ? 1 : 0;
}

/* Nearest upcoming day-with-games ahead of `from`: jump by a week until a
   bracket answers, then walk back inside it. ~9 requests worst case, at
   espn_api's own 4 s pacing. */
static time_t svc_find_nearest_slate(league_t lg, time_t from)
{
    /* A hunt that failed recently already proved (from, from+28d] empty, and
       a later cycle's window is inside that verdict until the week rolls.
       Retry weekly: a dead off-season costs ~4 probes/week, not /cycle. */
    if (s_slate_none_at[lg] &&
        from - s_slate_none_at[lg] < SVC_SLATE_PROBE_STEP * SVC_DAY_SECONDS) {
        return 0;
    }
    time_t hit = 0;
    bool clean_empty = true;   /* every jump probe actually answered */
    for (int off = SVC_SLATE_PROBE_STEP; off <= SVC_SLATE_PROBE_MAX && !hit;
         off += SVC_SLATE_PROBE_STEP) {
        if (svc_switch_pending()) {
            return 0;              /* tab tap: stop at this request boundary */
        }
        time_t day = from + (time_t)off * SVC_DAY_SECONDS;
        int probe = slate_day_has_games(lg, day);
        if (probe > 0) {
            hit = day;
        } else if (probe < 0) {
            clean_empty = false;   /* an error is not a verdict of empty */
        }
    }
    if (!hit) {
        if (clean_empty) {
            s_slate_none_at[lg] = from;   /* weekly retry, see above */
        }
        return 0;
    }
    s_slate_none_at[lg] = 0;             /* a slate exists: verdict void */
    for (int back = SVC_SLATE_PROBE_STEP - 1; back >= 1; back--) {
        if (svc_switch_pending()) {
            return hit;            /* coarse answer beats another probe */
        }
        time_t day = hit - (time_t)back * SVC_DAY_SECONDS;
        if (day <= from) {
            break;
        }
        if (slate_day_has_games(lg, day)) {
            return day;
        }
    }
    return hit;
}

/* Forward bits of the page-box section (defined below with the other box
   machinery): svc_fill_recent's dead-league rescue reads the tab's box. */
typedef struct {
    league_t   league;
    time_t     saved_at;
    int        count;
    nba_game_t games[SVC_BOX_GAMES];       /* newest-first, like s_cache */
} svc_slot_t;
static svc_slot_t s_page_box[4];           /* one recent window per league tab */
static bool box_fresh(league_t lg);
static void note_empty_day(time_t d);      /* known-empty set, defined with
                                            * the back-walk below */

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
    if (s_slate_day[s_league] > today) {
        hi = s_slate_day[s_league];        /* dead zone: the slate beats an empty today */
    }
    /* Drop games dated beyond the window we actually fetched: a shifted
       clock (the CYD_TEST_DAY_SHIFT soak builds, or any clock jump) can
       leave a future day's games parked on the newest edge forever - the
       re-anchor walks backwards from today and never rewrites those slots,
       and persist would keep re-saving them. Pruned *before* the walk so
       the freed page-0 slots are refilled from real dates in this cycle.
       The hunted college slate is legitimate future; ESPN's UTC event
       dates may tip one day past it. */
    time_t edge = today;
    if (s_slate_day[s_league] > edge) {
        edge = s_slate_day[s_league];
    }
    edge += SVC_DAY_SECONDS;
    int stale = 0;
    for (int i = 0; i < s_cache_count; ) {
        time_t d = iso_to_day(s_cache[i].date);
        if (d && d > edge) {
            for (int c = i + 1; c < s_cache_count; c++) {
                s_cache[c - 1] = s_cache[c];
            }
            s_cache_count--;
            stale++;
        } else {
            i++;
        }
    }
    if (stale) {
        ESP_LOGI(TAG, "dropped %d future-dated game(s) from the cache", stale);
    }

    esp_err_t err = ESP_OK;
    int empty_walk = 0;
    bool anchor_touched = false;    /* did this fill merge the newest edge? */

    for (int chunk = 0; chunk < SVC_PAGE_CHUNKS; chunk++) {
        if (svc_switch_pending()) {
            break;                 /* tab tap: stop at this request boundary */
        }
        time_t lo = hi;                  /* one day per request */
        int count = 0;
        int pages = 0;
        err = svc_fetch_range(lo, hi, true, INT_MAX, &count, &pages);
        if (err != ESP_OK) {
            return err;
        }
        if (count == 0) {
            empty_walk++;
            note_empty_day(lo);        /* record the day: the back-walk
                                        * skips it for free */
            if (empty_walk >= SVC_EMPTY_CHUNKS) {
                /* A long empty streak under one fill is a real dead edge:
                   stop probing (page 0 renders short either way now - this
                   is a request-cost cap, not a display gate). */
                s_older_exhausted = true;
                break;
            }
        } else {
            empty_walk = 0;
            anchor_touched = true;
        }
        if (s_cache_count >= SVC_PAGE_SIZE) {
            break;                       /* page 0 is full */
        }
        hi = lo - SVC_DAY_SECONDS;
    }

    /* Newest edge still empty after the whole fill (college October, pro
       September): hunt for the nearest upcoming slate so the board shows
       scheduled games (tip text) instead of a dead end. ESPN has no request
       budget to guard, so every tab may probe; the result is remembered per
       league, so the ~10 probes are paid once, not every cycle. */
    if (err == ESP_OK && s_cache_count == 0 && !svc_switch_pending()) {
        time_t slate = svc_find_nearest_slate(s_league, today);
        if (slate) {
            s_slate_day[s_league] = slate;
            ESP_LOGI(TAG, "no games near today - anchoring on nearest slate, day +%d",
                     (int)((slate - today) / SVC_DAY_SECONDS));
            int count = 0, pages = 0;
            err = svc_fetch_range(slate, slate, true, INT_MAX, &count, &pages);
            if (count > 0) {
                anchor_touched = true;
            }
            /* A forward probe means no season reached backwards either -
               let the short slate render and grey out "<". */
            s_older_exhausted = true;
        } else {
            s_slate_day[s_league] = 0;
            ESP_LOGI(TAG, "no slate within %d days either", SVC_SLATE_PROBE_MAX);
        }
    }

    if (anchor_touched || s_cache_count == 0) {
        s_base = 0;                      /* cache[0] is the newest game again */
    } else if (s_base > 0) {
        /* The window is slid under deep history and this re-anchor merged
           NOTHING newer (a finished league: every probed day answered empty,
           e.g. WNBA after the Finals). Zeroing s_base anyway would mislabel
           those old games as page 1 - and open the persist guards, making
           the deepest fetched history the tab box and the boot blob. The
           box holds the last ANCHORED board: hand it back (that is what
           "cheap way back to now" means when "now" fetches empty); with no
           fresh box the slide stays honest and svc_render clamps. */
        svc_slot_t *box = &s_page_box[s_league];
        if (box_fresh(s_league) && box->count > 0) {
            memcpy(s_cache, box->games, (size_t)box->count * sizeof(nba_game_t));
            s_cache_count = box->count;
            s_base = 0;
            s_older_exhausted = false;
            ESP_LOGI(TAG, "no newer games - board back to the anchored tab box");
        } else {
            ESP_LOGI(TAG, "re-anchor found nothing newer - window stays at base %d",
                     s_base);
        }
    }
    return err;
}

/* How many games of `day` still sit inside the sliding window? Answers
   whether a day the walk already fetched lost its tail to a re-anchor. */
static int cached_on_day(time_t day)
{
    int n = 0;
    for (int i = 0; i < s_cache_count; i++) {
        if (iso_to_day(s_cache[i].date) == day) {
            n++;
        }
    }
    return n;
}

/* Bitmap slot for day `d` (days since the pinned epoch, shifted so the
   slate hunt's +28-day horizon lands on bit 0), or -1 for days outside the
   addressable [-SVC_EMPTY_AHEAD .. +35] window. */
static int empty_bit(time_t d)
{
    if (!s_empty_epoch) {
        return -1;
    }
    long long delta = (long long)(s_empty_epoch - d) / SVC_DAY_SECONDS;
    if (delta < -SVC_EMPTY_AHEAD || delta >= 64 - SVC_EMPTY_AHEAD) {
        return -1;
    }
    return (int)(delta + SVC_EMPTY_AHEAD);
}

/* Record "the scoreboard answered day `d` empty". Past answers stand for the
   session: ESPN publishes slates FORWARD, it never backdates games onto
   finished days - so skipping a known-empty day is free correctness,
   re-probing one is only cost. Recording is idempotent and EVERY feeder may
   do it (fill's day walk, the back-walk, the idle prefetch): with a set,
   one feeder's answer can no longer block another's. The single run this
   replaces could not record a disjoint day at all - and since every walk
   restarts at `anchor - 1 day` when nothing merges, the dead-slate prefetch
   re-probed Oct 31 on every iteration, forever, once the hunt anchored the
   window on Nov 1 above the boot's Oct 23..25 block (2026-10 NCAAB). */
static void note_empty_day(time_t d)
{
    if (!s_empty_epoch) {
        s_empty_epoch = svc_today_day();  /* pinned once: bits stay glued to
                                           * absolute days across midnight */
        if (!s_empty_epoch) {
            return;                       /* clock not synced */
        }
    }
    int bit = empty_bit(d);
    if (bit >= 0) {
        s_empty_days |= 1ULL << bit;
    }
}

static bool day_known_empty(time_t d)
{
    int bit = empty_bit(d);
    return bit >= 0 && ((s_empty_days >> bit) & 1ULL) != 0;
}

/* Grow the cache backwards by one day, resuming below the OLDEST GAME IN THE
   WINDOW - not below the oldest day ever fetched. A re-anchor merge evicts
   already-fetched days out of the sliding window, and walking on from that
   stale day pointer skipped the whole gap (2026-10 device bug: the window
   all Oct 23, s_lo_day still at Oct 21 from the night's idle prefetch, so
   `<` jumped to Oct 20 and Oct 22 was unreachable for the session).
   max_base caps the slide for a page-aimed walk (see svc_cache_insert);
   speculative fills pass INT_MAX. */
static esp_err_t svc_fetch_older_chunk(int *count_out, int max_base)
{
    time_t anchor = s_cache_count ? iso_to_day(s_cache[s_cache_count - 1].date) : 0;
    if (!anchor) {
        anchor = svc_today_day();
        if (!anchor) {
            *count_out = 0;
            return ESP_ERR_INVALID_STATE;   /* clock not synced */
        }
    }
    if (!s_lo_day || s_lo_day < anchor) {
        s_lo_day = anchor;                  /* the window floor is the cursor */
        s_lo_day_games = -1;
        s_lo_day_repair_used = false;
    }

    time_t hi;
    if (!s_cache_count) {
        hi = anchor;                        /* empty window: fetch the day itself */
    } else if (s_lo_day == anchor && !s_lo_day_repair_used
               && s_lo_day_games > cached_on_day(anchor)) {
        hi = anchor;                        /* day fetched whole but the window
                                             * lost its tail: refetch it - dupes
                                             * merge in place, missing games come
                                             * back at the tail. Once only: a day
                                             * bigger than the window can never
                                             * look "complete" to this check. */
        s_lo_day_repair_used = true;
    } else {
        hi = anchor - SVC_DAY_SECONDS;      /* resume below the trusted floor */
    }
    while (day_known_empty(hi)) {
        /* hi answered empty already - the answer stands; step below it
           without a request. Re-probing a known top day after the cursor
           snap is the ping-pong that faked the WNBA bottom, and the streak
           below only counts days OUTSIDE the set now, so "N empty chunks"
           really means N distinct days. The loop is bounded: days older
           than the bitmap window read as unknown and fall through to the
           history check right below. */
        hi -= SVC_DAY_SECONDS;
    }
    time_t lo = hi;

    if (svc_days_ago(lo) > SVC_HISTORY_DAYS) {
        ESP_LOGI(TAG, "history limit reached (%d days)", SVC_HISTORY_DAYS);
        s_older_exhausted = true;
        *count_out = 0;
        return ESP_OK;
    }

    esp_err_t err = svc_fetch_range(lo, hi, false, max_base, count_out, NULL);
    if (err != ESP_OK) {
        return err;
    }
    if (*count_out == 0) {
        note_empty_day(lo);
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
   recent games instead, which is the cheap way back to "now". The walk caps
   its slide at one page above the target (`s_base <= (page-1)*PAGE_SIZE`),
   so the requested page - and the one the viewer pressed `<` from - stay
   inside the window even when the next day is bigger than the gap. */
static esp_err_t svc_reach_page(int *page_io)
{
    int page = *page_io;
    int cap = (page > 0 ? page - 1 : 0) * SVC_PAGE_SIZE;

    for (int attempt = 0; attempt <= SVC_PAGE_CHUNKS; attempt++) {
        if (svc_switch_pending()) {
            return ESP_ERR_INVALID_STATE;   /* tab tap mid-paging */
        }
        int off, n;
        if (page_slice(page, &off, &n)) {
            *page_io = page;
            return ESP_OK;
        }

        if (page * SVC_PAGE_SIZE - s_base < 0) {
            /* The window floor sits below this page's start: those games
               were evicted on an earlier slide (typically a `>` step from
               deep in history). They are not coming back - anchor on the
               newest instead of walking a gap that no longer exists. */
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
        esp_err_t err = svc_fetch_older_chunk(&count, cap);
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
    /* Not found YET - an empty void mid-walk, not a broken state (this used
       to return ESP_ERR_INVALID_STATE, which reads as a programmer error AND
       skipped the edge-is-an-answer branch below, logging a healthy walk's
       pause as "fetch failed" and feeding the network-failure backoff). */
    return ESP_ERR_NOT_FOUND;
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
    /* Same slide cap as the walk: a same-day game list that grew while the
       viewer sits on this page must not slide the window past the page. */
    esp_err_t err = svc_fetch_range(oldest_day ? oldest_day : newest_day,
                                    newest_day, svc_keep_newer(),
                                    (s_page > 0 ? s_page - 1 : 0) * SVC_PAGE_SIZE,
                                    &count, NULL);
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
    if (!ok) {
        /* The requested page is not renderable but the cache may hold others
           (the window slid away from it - a dead-league re-anchor resets to
           page 0 while deep games sit in the cache): show the nearest real
           page and make the pager agree, instead of a stale strip or a blank
           board. Adopting s_page keeps the arrows stepping from what the
           viewer actually sees. */
        int near = svc_cached_page_near(page);
        if (near >= 0) {
            page = near;
            s_page = near;
            ok = page_slice(page, &off, &n) && n > 0;
        }
    }

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
#ifdef CYD_NBA_PAGEMON
        uint32_t t0 = s_step_ms;
        if (t0) {
            s_step_ms = 0;
            ESP_LOGI(TAG, "pagemon: page %d painted %u ms after step",
                     page + 1, (uint32_t)(esp_timer_get_time() / 1000) - t0);
        }
#endif
    } else if (!s_have_cards) {
        cards_screen_show_message(
            err != ESP_OK ? "No data - check Wi-Fi"
                          : "No games around this date",
            &info);
    }
    /* else: keep what is on screen - a failed auto-refresh should not blank it */
    lvgl_port_unlock();

    s_shown_page = page;
}

/* Boot-stage text on the splash, from worker context: only lands while the
   splash is still the screen (it is gone ~5 s into the boot). Takes the
   port lock - unlike the app_main callers, this is not LVGL task context. */
static void svc_splash_status(const char *txt)
{
    lvgl_port_lock(0);
    if (splash_screen_is_current()) {
        splash_screen_set_status(txt);
    }
    lvgl_port_unlock();
}

/* Spin the pager-strip spinner while a cycle is waiting on the network, so
   a page walk / refresh / cold fill reads as "still working", not "hung".
   Worker context: same port-lock rule as svc_splash_status. Hidden outside
   those windows, so idle boards draw nothing extra. */
static void svc_busy(bool on)
{
    lvgl_port_lock(0);
    cards_screen_set_busy(on);
    lvgl_port_unlock();
}

static const char *lg_name(league_t lg)
{
    switch (lg) {
    case LEAGUE_WNBA:  return "WNBA";
    case LEAGUE_NCAAB: return "NCAAB";
    case LEAGUE_NCAAW: return "NCAAW";
    default:           return "NBA";
    }
}

/* ---- per-league page box (RAM, not NVS) --------------------------------
   Every tab keeps its last two pages in RAM so a switch paints from there
   instead of a cold fetch - and (with s_last_page) paints the page the
   viewer was actually on, not always page 1. Three writers fill the boxes:
   each successful cycle parks the shown feed's newest pages (svc_persist),
   a tab-switch stashes the pages the leaving board still holds, and after
   the first successful cycle the worker pre-warms the three tabs it is NOT
   showing - every feed is ESPN now, so dialling there is quota-free, and
   arriving cold otherwise means the dead-zone slate hunt plus the first
   fetch happen in front of a finger on the tab (tens of seconds at the
   4 s pacing). RAM instead of NVS on purpose: a 3rd/4th ~2.9 KB blob would
   eat the 24 KB partition's GC headroom, and "make switching instant" is a
   session-scoped job. */

/* Park the working cache's newest pages into a tab's box. Only while the
   window is ANCHORED on the newest edge: with s_base > 0 (viewer deep in
   the day-walk) s_cache holds older slate, and parking it would make a
   stale board the tab's instant paint AND the boot snapshot. */
static void svc_box_store(league_t lg)
{
    if (s_cache_count <= 0) {
        return;
    }
    if (s_base > 0) {
        ESP_LOGI(TAG, "window slid past the newest edge (base %d) - keeping the anchored %s box",
                 s_base, lg_name(lg));
        return;
    }
    svc_slot_t *box = &s_page_box[lg];
    int keep = s_cache_count < SVC_BOX_GAMES ? s_cache_count
                                             : SVC_BOX_GAMES;
    memcpy(box->games, s_cache, (size_t)keep * sizeof(nba_game_t));
    box->count = keep;
    box->league = lg;
    box->saved_at = time(NULL);
}

/* Is this league's tab box recently filled? One rule for two consumers:
   svc_box_take (instant paint on switch) and svc_prewarm_tabs (do not
   re-hunt a league nobody has touched since its box was filled). The 6 h
   ceiling is svc_box_take's: stale games show for one render and the
   refetch that follows replaces them. */
static bool box_fresh(league_t lg)
{
    const svc_slot_t *box = &s_page_box[lg];
    return box->league == lg && box->count > 0 &&
           time(NULL) - box->saved_at <= 6 * 3600;
}

/* Hand the tab's boxed pages to the cache (switch-time instant paint).
   The 6 h ceiling covers a device running across a day boundary: stale
   games show for one render and the refetch that follows replaces them.
   A future-dated (probed) slate means the hunt already proved nothing
   older is reachable: let a short page render and grey out "<".
   The viewer lands where they left off: s_page snaps to the remembered
   page, clamped into the range the box actually reaches. */
static bool svc_box_take(void)
{
    if (s_cache_count > 0) {
        return false;
    }
    if (!box_fresh(s_league)) {
        return false;
    }
    svc_slot_t *box = &s_page_box[s_league];
    memcpy(s_cache, box->games, (size_t)box->count * sizeof(nba_game_t));
    s_cache_count = box->count;
    s_base = 0;
    s_older_exhausted = s_slate_day[s_league] > svc_today_day();
    int box_pages = (box->count + SVC_PAGE_SIZE - 1) / SVC_PAGE_SIZE;
    s_page = s_last_page[s_league];
    if (s_page > box_pages - 1) {
        s_page = box_pages - 1;
    }
    if (s_page < 0) {
        s_page = 0;
    }
    ESP_LOGI(TAG, "page box: %s ready (%d game(s), %ds old, page %d)",
             lg_name(s_league), s_cache_count,
             (int)(time(NULL) - box->saved_at), s_page + 1);
    return true;
}

/* Capped sorted insertion of a fetched day into a tab box: ESPN hands a busy
   day over oldest-tip-first, the box (like s_cache) is newest-first, and the
   box keeps its newest SVC_BOX_GAMES. Games seen twice (late-UTC tips listed
   on two scoreboard days) update in place instead of duplicating. */
static void svc_slot_merge(svc_slot_t *slot, const nba_game_t *src, int count)
{
    for (int i = 0; i < count; i++) {
        bool known = false;
        for (int c = 0; c < slot->count; c++) {
            if (slot->games[c].id == src[i].id) {
                slot->games[c] = src[i];
                known = true;
                break;
            }
        }
        if (known) {
            continue;
        }
        int at = slot->count;
        for (int c = 0; c < slot->count; c++) {
            if (!game_newer(&slot->games[c], &src[i])) {
                at = c;
                break;
            }
        }
        if (at >= SVC_BOX_GAMES) {
            continue;          /* older than everything that fits */
        }
        if (slot->count < SVC_BOX_GAMES) {
            slot->count++;
        }
        if (at < slot->count - 1) {
            memmove(&slot->games[at + 1], &slot->games[at],
                    (size_t)(slot->count - 1 - at) * sizeof(nba_game_t));
        }
        slot->games[at] = src[i];
    }
}

/* After the first successful cycle, quietly warm the three tabs we are NOT
   showing (all feeds are ESPN - no quota, ~4-7 background requests at the
   4 s pacing). The day is fetched straight into the box - no probe-then-
   refetch double call - and a sparse day reaches one day back to fill the
   two-page box. */
static void svc_prewarm_tabs(void)
{
    time_t today = svc_today_day();
    if (!today) {
        return;
    }
    for (int lg = 0; lg <= LEAGUE_NCAAW; lg++) {
        if ((league_t)lg == s_league || s_dirty || svc_switch_pending()) {
            continue;          /* the active feed covers itself; a poke wins */
        }
        if (box_fresh((league_t)lg)) {
            /* A tab visited this session (or an earlier partial warm)
               already holds a recent board: no reason to re-hunt it. */
            ESP_LOGI(TAG, "pre-warm: %s box still recent - skipped",
                     lg_name((league_t)lg));
            continue;
        }
        time_t day = s_slate_day[lg] > today ? s_slate_day[lg] : today;
        char iso[16];
        day_to_iso(day, iso, sizeof(iso));
        int count = 0;
        if (espn_api_fetch_games((league_t)lg, iso, s_api_page,
                                 ESPN_API_MAX_GAMES, &count) != ESP_OK) {
            continue;
        }
        if (count <= 0) {
            day = svc_find_nearest_slate((league_t)lg, day);
            if (!day) {
                ESP_LOGW(TAG, "pre-warm: no slate for %s either",
                         lg_name((league_t)lg));
                continue;
            }
            s_slate_day[lg] = day;
            day_to_iso(day, iso, sizeof(iso));
            count = 0;
            if (espn_api_fetch_games((league_t)lg, iso, s_api_page,
                                     ESPN_API_MAX_GAMES, &count) != ESP_OK
                || count <= 0) {
                continue;
            }
        }
        svc_slot_t *slot = &s_page_box[lg];
        svc_slot_merge(slot, s_api_page, count);
        /* A quiet day (< 2 pages): reach back one day. Skipped on a hunted
           future slate - the days before it are empty by construction. */
        if (slot->count < SVC_BOX_GAMES && day <= today
            && !s_dirty && !svc_switch_pending()) {
            char prev[16];
            day_to_iso(day - SVC_DAY_SECONDS, prev, sizeof(prev));
            int n2 = 0;
            if (espn_api_fetch_games((league_t)lg, prev, s_api_page,
                                     ESPN_API_MAX_GAMES, &n2) == ESP_OK
                && n2 > 0) {
                svc_slot_merge(slot, s_api_page, n2);
            }
        }
        slot->league = (league_t)lg;
        slot->saved_at = time(NULL);
        ESP_LOGI(TAG, "pre-warm: %s box ready (%d game(s), day %s)",
                 lg_name((league_t)lg), slot->count, iso);
    }
}

/* Which NVS key snapshots a league under. All four do now (college at one
   page deep - see SVC_PERSIST_GAMES_COLLEGE). */
static const char *svc_blob_key(league_t lg)
{
    switch (lg) {
        case LEAGUE_NBA:   return "games_nba";
        case LEAGUE_WNBA:  return "games_wnba";
        case LEAGUE_NCAAB: return "games_ncaab";
        case LEAGUE_NCAAW: return "games_ncaaw";
        default:           return NULL;
    }
}

/* Snapshot depth for a league: the full box for the pro boards, one page
   for the college ones. */
static int svc_persist_cap(league_t lg)
{
    return (lg == LEAGUE_NCAAB || lg == LEAGUE_NCAAW) ? SVC_PERSIST_GAMES_COLLEGE
                                                      : SVC_PERSIST_GAMES;
}

/* Put a stored page into the cache so the board shows real scores at once:
   first the tab's RAM page box (this session's page - every tab has one
   once it has been shown), then the NVS snapshot (pro: 4 pages; college:
   one). The refill that follows a switch still runs -
   this only decides what the user sees while it does. */
static void svc_cache_load(void)
{
    if (s_cache_count > 0) {
        return;
    }
    if (svc_box_take()) {
        svc_render(s_page, ESP_OK);
        return;
    }
    const char *key = svc_blob_key(s_league);
    if (!key) {
        return;
    }
    size_t stored = 0;
    if (nvs_settings_load_blob(key, &s_blob, sizeof(s_blob), &stored) != ESP_OK) {
        /* First boot, struct changed size, or a reset landing inside the
           last persist's NVS write (seen after an autopage soak: the flash
           reset clipped the blob entry). Saying nothing here cost a debug
           session - the cold path then re-fetches and re-saves, so this is
           self-healing, but it should not look like a mystery. */
        ESP_LOGI(TAG, "no stored %s scores yet", lg_name(s_league));
        return;
    }
    if (s_blob.magic != SVC_BLOB_MAGIC ||
        s_blob.game_size != sizeof(nba_game_t) ||
        s_blob.league != (int32_t)s_league ||
        s_blob.count <= 0 || s_blob.count > SVC_PERSIST_GAMES ||
        s_blob.saved_at < 1577836800) {   /* predates 2020: garbage */
        ESP_LOGW(TAG, "score cache blob invalid - ignoring");
        return;
    }
    /* The entry is header + count games (college snapshots are one page,
       not the full box): a clipped write or a foreign depth rejects here. */
    size_t want = sizeof(svc_blob_t)
                  - (size_t)(SVC_PERSIST_GAMES - s_blob.count) * sizeof(nba_game_t);
    if (stored != want) {
        ESP_LOGW(TAG, "score cache blob length %u != %u game(s) - ignoring",
                 (unsigned)stored, s_blob.count);
        return;
    }
    for (int i = 0; i < s_blob.count; i++) {
        if (s_blob.games[i].league != s_league) {
            /* Self-heal: the pre-serialisation build could stamp a mid-switch
               mixed page under the new league's key. Drop the polluted blob;
               the refill this cycle follows regenerates a clean one. */
            ESP_LOGW(TAG, "score cache blob holds %s games - ignoring",
                     lg_name((league_t)s_blob.games[i].league));
            return;
        }
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
    /* Instant paint: on a tab switch the board shows the snapshot before
       the refill runs; at boot it is a no-op behind the splash. */
    svc_render(s_page, ESP_OK);
    svc_splash_status("Showing stored scores...");
}

/* Park the newest pages where revisits can find them fast: always the tab's
   RAM page box (every league), and to NVS too - the pro leagues the full
   4-page box, college one page - but there only when the page actually
   changed (a finished slate must not burn a flash write per 15 min). */
static void svc_persist(esp_err_t err)
{
    if (err == ESP_OK && s_cache_count > 0) {
        svc_box_store(s_league);
    }
    const char *key = svc_blob_key(s_league);
    /* s_base > 0: the window holds slid history, not the newest board -
       the blob is the cross-reboot NEWEST snapshot (see svc_box_store). */
    if (!key || err != ESP_OK || s_cache_count == 0 || s_base > 0) {
        return;
    }
    int cap = svc_persist_cap(s_league);
    int keep = s_cache_count < cap ? s_cache_count : cap;
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
    /* Variable-length entry: header + keep games, so a college snapshot
       costs ~1.1 KB in the partition, not the mirror's full 4.4 KB. */
    size_t blob_len = sizeof(svc_blob_t)
                      - (size_t)(SVC_PERSIST_GAMES - keep) * sizeof(nba_game_t);
    if (nvs_settings_save_blob(key, &s_blob, blob_len) == ESP_OK) {
        s_blob_valid = true;
        ESP_LOGI(TAG, "score cache: %d game(s) saved to NVS", keep);
    } else {
        s_blob_valid = false;
        ESP_LOGW(TAG, "score cache save failed");
    }
}

/* ---- worker ----------------------------------------------------------- */

#ifdef CYD_NBA_TLSSTRESS
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "mbedtls_arena.h"

/* TLS-storm hunt (see main/CMakeLists.txt). The cert "storms" log
   PSA_ERROR_INSUFFICIENT_MEMORY (-141, mislabeled "signature verification
   failed" by esp-x509-crt-bundle): the PSA allocator failing *before* the
   signature is judged. To pin it device-side, hammer fresh TLS handshakes
   and alternate hosts: ESPN (Akamai edge, RSA chain) and Google's
   generate_204 (Google infra, ECDSA chain). A failure streak that spans
   BOTH hosts is host-independent by construction - device-side, closing
   the one gap the natural storms left open. Any HTTP status (even 403)
   means the handshake worked; only status 0 counts as a miss. */
#define TLSSTRESS_ESPN_URL "https://site.api.espn.com/apis/site/v2/sports/basketball/nba/scoreboard"
#define TLSSTRESS_ALT_URL  "https://www.google.com/generate_204"
#define TLSSTRESS_GAP_MS   1500

static bool tlsstress_probe(const char *url, esp_err_t *err_out)
{
    esp_http_client_config_t cfg = {
        .url = url,
        .method = HTTP_METHOD_HEAD,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = 10000,
        .buffer_size = 512,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) {
        *err_out = ESP_ERR_NO_MEM;
        return false;
    }
    esp_http_client_set_header(client, "User-Agent", "Mozilla/5.0");
    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);
    *err_out = err;
    return status != 0;
}

/* Windowed min-free sampler: the per-probe log samples only at probe
   boundaries, but the handshake transient (mbedtls IN/OUT buffers +
   context, ~20-28 K on the stock allocator) lives *inside* the probe.
   Sampling at 15 ms captures it, so the arena run can be compared to the
   stock run on the same metric (the wolfSSL spike's teardown line). */
static volatile uint32_t s_ts_min_free;

static void ts_sampler(void *arg)
{
    (void)arg;
    for (;;) {
        uint32_t f = (uint32_t)esp_get_free_heap_size();
        if (f < s_ts_min_free) {
            s_ts_min_free = f;
        }
        vTaskDelay(pdMS_TO_TICKS(15));
    }
}

static void svc_tls_stress(void)
{
    int ok[2] = {0, 0}, fail[2] = {0, 0};
    int streak = 0, worst = 0;

    /* Measurement anchor: the stress loop makes the whole feed chain
       unreachable, and GCC then does something worse than GC: it proves
       these zero-init arrays are never WRITTEN and promotes them to
       .rodata (measured: .rodata.s_cache in the map, +35 KB flash, the
       DRAM shape silently gone) - the probes would run with far MORE free
       heap than production has. A volatile STORE is what pins them to
       .bss; storing 0 preserves the zero-init semantics, and under
       TLSSTRESS nothing reads them anyway (the feed is skipped). */
    *(volatile uint32_t *)&s_cache[0] = 0;
    *(volatile uint32_t *)&s_page_box[0] = 0;
    *(volatile uint32_t *)&s_api_page[0] = 0;
    *(volatile uint32_t *)&s_blob = 0;

    ESP_LOGW(TAG, "tlsstress: %d alternating handshakes (espn/google), %d ms apart, boot heap %u",
             CYD_NBA_TLSSTRESS, TLSSTRESS_GAP_MS, (unsigned)esp_get_free_heap_size());
    s_ts_min_free = UINT32_MAX;
    xTaskCreate(ts_sampler, "ts_sample", 2048, NULL, 3, NULL);
    for (int i = 1; i <= CYD_NBA_TLSSTRESS; i++) {
        int h = i % 2;                      /* 0 = espn, 1 = google */
        esp_err_t err = ESP_OK;
        bool tls_ok = tlsstress_probe(h == 0 ? TLSSTRESS_ESPN_URL : TLSSTRESS_ALT_URL,
                                      &err);
        if (tls_ok) {
            ok[h]++;
            streak = 0;
        } else {
            fail[h]++;
            streak++;
            if (streak > worst) {
                worst = streak;
            }
            ESP_LOGE(TAG, "tlsstress %d %s: handshake FAIL (%s) heap %u blk %u",
                     i, h == 0 ? "espn" : "goog", esp_err_to_name(err),
                     (unsigned)esp_get_free_heap_size(),
                     (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
            if (streak == 6) {
                /* 6 in a row across alternating hosts = 3 per host: no CDN
                   rotates that fast. This is the latch, if it exists. */
                ESP_LOGE(TAG, "tlsstress: LATCHED - %d consecutive failures across BOTH hosts",
                         streak);
            }
        }
        if (i % 20 == 0) {
            ESP_LOGI(TAG, "tlsstress %d/%d: espn %d/%d google %d/%d, heap %u blk %u, streak %d worst %d, window min free %u",
                     i, CYD_NBA_TLSSTRESS, ok[0], ok[0] + fail[0],
                     ok[1], ok[1] + fail[1],
                     (unsigned)esp_get_free_heap_size(),
                     (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
                     streak, worst, (unsigned)s_ts_min_free);
            mbedtls_arena_dump("tlsstress");
            s_ts_min_free = UINT32_MAX;
        }
        vTaskDelay(pdMS_TO_TICKS(TLSSTRESS_GAP_MS));
    }
    ESP_LOGW(TAG, "tlsstress done: espn %d/%d google %d/%d, worst streak %d, heap %u blk %u",
             ok[0], ok[0] + fail[0], ok[1], ok[1] + fail[1], worst,
             (unsigned)esp_get_free_heap_size(),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
    mbedtls_arena_dump("tlsstress final");
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(60 * 60 * 1000));   /* stay quiet for the capture */
    }
}
#endif

#if defined(CYD_NBA_AUTOTAB) && CYD_NBA_AUTOTAB >= 2
/* Race soak (see main/CMakeLists.txt): flip the feed from a timer task while
   the worker is mid-fetch - the exact interleaving that used to render two
   leagues on one board. With worker-owned feed state the in-flight cycle may
   only be dropped at its next request boundary, so the log should read
   "league switch pending - dropping the half-finished page" -> "switching
   feed to NCAAB" -> boards that only ever show one league's games. */
static void autotab_race_cb(void *arg)
{
    (void)arg;
    ESP_LOGI(TAG, "autotab(race): queueing NCAAB while the worker is mid-cycle");
    nba_service_set_league(LEAGUE_NCAAB);
}
#endif

static void svc_task(void *arg)
{
    (void)arg;

#if defined(CYD_NBA_AUTOTAB) && CYD_NBA_AUTOTAB >= 2
    const esp_timer_create_args_t race_args = {
        .callback = autotab_race_cb,
        .name = "autotab_race",
    };
    esp_timer_handle_t race_timer = NULL;
    if (esp_timer_create(&race_args, &race_timer) == ESP_OK) {
        esp_timer_start_once(race_timer, 15 * 1000 * 1000);  /* mid first fill */
    }
#endif

#ifndef CYD_NBA_TLSSTRESS
    svc_cache_load();          /* boot with the last session's page on screen */
#endif
    /* (stress builds skip it: the early render would overflow the LVGL pool
       of the LVGL-32 arena-test shape, and the stress never shows the board) */

#ifdef CYD_NBA_TLSSTRESS
    svc_tls_stress();          /* diagnostic build: the feed never runs */
#endif

    for (;;) {
        /* Warm-up gate: the board comes from NVS/RAM long before Wi-Fi
           associates (~1.5 s vs ~4 s), and cycle 1 used to race that gap:
           the time-sync probe died in getaddrinfo (error 202 - no DNS yet),
           the game-day anchor refused every fetch with
           ESP_ERR_INVALID_STATE, and boot logged a "fetch failed" that was
           no network failure at all - then burned a fail-streak tick and
           owed the backoff. Only the luck of the CONNECTED poke cut that
           sleep short. Do not start a cycle that needs the network before
           there is one: wait on the loop's own wake signal. A queued tab
           tap or a page/refresh poke breaks the wait anyway - a cached
           page step or a tab's stored board paints fine with no network,
           and that cycle's fetch failure (if any) is the honest kind. */
        if (wifi_backend_get_state() != WIFI_STATE_CONNECTED) {
            ESP_LOGI(TAG, "no network yet - feed waits for Wi-Fi (board stays as shown)");
            while (wifi_backend_get_state() != WIFI_STATE_CONNECTED
                   && !svc_switch_pending() && !s_dirty) {
                (void)xSemaphoreTake(s_wake, pdMS_TO_TICKS(SVC_NET_WAIT_POLL_MS));
            }
        }

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

        if (s_cache_count == 0) {
            svc_splash_status("Fetching first scores...");
        }

        /* Every fetch is anchored on the game day, so the clock has to be set
           first (it comes from the Date header of an HTTPS GET). */
        if (!time_sync_is_synced()) {
            time_sync_from_http();
        }

        if (s_league_switched) {
            /* Apply a queued tab tap. s_league changes *only* here, with no
               fetch in flight, so no cycle can ever mix two leagues. */
            s_league_switched = false;
            int lg = s_pending_league;
            s_pending_league = -1;
            if (lg >= 0 && (league_t)lg != s_league) {
                /* Leave the outgoing tab a page to come back to: the cache
                   still holds the feed we are showing (only the worker
                   touches it), so park its newest page before the wipe. */
                svc_box_store(s_league);
                s_league = (league_t)lg;
                /* Wipe the previous feed's cards before the new league's games
                   come in, so stale NBA scores cannot masquerade as college. */
                s_cache_count = 0;
                s_base = 0;
                s_lo_day = 0;
                s_hi_day = 0;
                s_lo_day_games = -1;
                s_lo_day_repair_used = false;
                s_empty_days = 0;
                s_empty_epoch = 0;
                /* s_slate_day is NOT cleared: the probe result belongs to the
                   league it was found for (and the boot pre-warm may have
                   filled it while this feed was not showing). Stale entries
                   sit at <= today and re-probe. */
                s_empty_chunks = 0;
                s_older_exhausted = false;
                s_fail_streak = 0;
                /* s_page stays 0 here; svc_box_take below may snap it to the
                   page this tab was on when the viewer last left it. */
                s_shown_page = s_page;  /* not a page move: the chain re-anchors */
                s_blob_valid = false;   /* the mirror belonged to the old feed */
                ESP_LOGI(TAG, "switching feed to %s", lg_name(s_league));
                svc_show_loading();
                svc_cache_load();       /* ...unless the page box/NVS has it */
            }
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
            svc_busy(true);
            err = svc_reach_page(&page);
            svc_busy(false);
            s_page = page;
            if (err != ESP_OK) {
                /* Keep the board useful: show the nearest page we already
                   have. */
                int fallback = svc_cached_page_near(page);
                if (fallback >= 0) {
                    ESP_LOGW(TAG, "page %d unavailable - showing page %d",
                             page + 1, fallback + 1);
                    s_page = fallback;
                }
                if (err == ESP_ERR_NOT_FOUND) {
                    /* Edge of history, not a network failure: the next
                       attempt short-circuits on s_older_exhausted before any
                       HTTP, so there is no request loop to fear - and this
                       MUST not join s_fail_streak, or a board that answered
                       exactly as asked gets parked in the backoff (a soak
                       stepping past the edge ate 15/30/60 s backoffs). */
                    err = ESP_OK;
                }
                /* Other errors stay set: the backoff below applies and a
                   failing page move cannot turn into a request loop. */
            }
        } else if (s_cache_count == 0 && !s_older_exhausted) {
            svc_busy(true);
            err = svc_fill_recent();                   /* first load, or a retry */
            svc_busy(false);
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
                svc_busy(true);
                err = svc_fill_recent();
                svc_busy(false);
            }
        } else if (forced || page_is_live(s_page)) {
            svc_busy(true);
            err = svc_refetch_page(s_page);            /* deeper page: refresh it */
            svc_busy(false);
        } else if (poked) {
            ESP_LOGI(TAG, "page %d shows finished games - not refetching",
                     s_page + 1);
        }

        if (svc_switch_pending()) {
            /* A tab tap landed mid-cycle. Do not render or persist the
               half-finished page - some of its games belong to the feed we
               are leaving - and do not count the abort as a fetch failure.
               Consume the tap's latched wake (the switch cycle IS the refresh
               it asked for) and loop: the next iteration applies the switch
               at the top, with no fetch in flight. */
            ESP_LOGI(TAG, "league switch pending - dropping the half-finished page");
            (void)xSemaphoreTake(s_wake, 0);
            continue;
        }

        if (page_slice(s_page, &off, &n)) {
            ESP_LOGI(TAG, "page %d: %d card(s) %s..%s (free heap: %u)",
                     s_page + 1, n, s_cache[off].date, s_cache[off + n - 1].date,
                     esp_get_free_heap_size());
        }
        svc_render(s_page, err);
        svc_persist(err);
        if (err == ESP_OK) {
            s_last_page[s_league] = s_page;
        }

        /* Quietly pre-warm the tabs we are not showing, so the first tap
           lands on a ready board instead of a cold fetch (+ slate hunt).
           Bound to a LIVE connection, not just a successful cycle: an
           offline boot renders the NVS snapshot fine (err == ESP_OK via
           the blob skip) but burning the once-per-boot warm slot without
           network would leave every tab cold for the whole session - so
           the wait continues until a connect lands (the CONNECTED poke
           makes that the very next cycle).
           A league whose box is still recent is skipped (no reason to
           re-hunt a tab filled this session or by a partial earlier run);
           the flag makes it once-per-boot and a partial run self-heals
           (boxes refill on visit, next boot re-runs this).
           Heap gate: deferred while near the TLS failure band (a ~10-
           request burst crosses that line) - stock builds only: with the
           mbedtls arena installed the band is gone (handshakes draw from
           .bss) and the boot pool sits below the floor by design, so the
           warm burst fires on the first connected cycle. */
        static bool prewarmed = false;
        if (!prewarmed && err == ESP_OK &&
            wifi_backend_get_state() == WIFI_STATE_CONNECTED) {
            /* Gate on the cycle-START sample: a mid-cycle read includes the
               live keep-alive TLS session (~22 K) and would sit below the
               floor forever. Cycle start measures the bare pool. */
            uint32_t fh = free_heap;
            if (!mbedtls_arena_active() && fh < SVC_PREWARM_HEAP_FLOOR) {
                ESP_LOGW(TAG, "heap %u < %u - pre-warm deferred to next cycle",
                         (unsigned)fh, (unsigned)SVC_PREWARM_HEAP_FLOOR);
            } else {
                prewarmed = true;
                svc_prewarm_tabs();
            }
        }

        /* Idle prefetch: quietly walk more days into the cache's free slots
           while nobody is poking the board, so the next "<" renders in 0 ms.
           Each day is one polite ESPN request; the loop dies the moment a
           tap (s_dirty) or a switch is pending, and the page step that
           follows finds the games already in RAM. No spinner: the board on
           screen is responsive; the work is invisible by design. */
        if (err == ESP_OK && !s_dirty && !svc_switch_pending()) {
            for (int i = 0; i < SVC_IDLE_PREFETCH_DAYS
                     && !s_dirty && !svc_switch_pending()
                     && s_cache_count < SVC_CACHE_GAMES
                     && !s_older_exhausted; i++) {
                int before = s_cache_count;
                bool exh_before = s_older_exhausted;
                int count = 0;
                /* Cap = the current base: idle prefetch fills the window's
                   FREE SLOTS only - it must never slide an anchored page 0
                   out from under the viewer (a busy day spilling past slot
                   12 would evict the newest games to make room for old). */
                if (svc_fetch_older_chunk(&count, s_base) != ESP_OK) {
                    break;
                }
                if (s_cache_count != before ||
                    s_older_exhausted != exh_before) {
                    /* Page fullness or the "<" arrow may have changed. */
                    svc_render(s_page, ESP_OK);
                }
            }
        }

        if (err == ESP_OK) {
            s_fail_streak = 0;
        } else {
            /* Storm self-heal: consecutive failures while the pool sits in
               the handshake-failure band just re-pick the same losing fight -
               the stress soak showed no recovery from the PSA -141 storms
               short of a reboot. The NVS-snapshot board comes back in ~8 s,
               so an 8 s restart beats limping through storm hours.
               Gate on the CYCLE-START heap (bare pool): a live read here is
               taken mid-burst with the TLS session's ~22 KB allocated and
               would trip the floor even from a healthy 53 K baseline - a
               plain network outage would reboot-loop (soak caught exactly
               this). s_fail_streak counts the PRIOR failures, so >= 2 = third.
               With the arena installed this floor must NOT fire: the pool
               then sits below 46 K permanently (the arena itself is .bss),
               so every outage would look like a storm and reboot-loop - and
               there is no latched PSA state to heal anyway. Arena build:
               a fail streak is just the network; the ordinary backoff is
               the whole response. */
            if (!mbedtls_arena_active() &&
                s_fail_streak >= 2 && free_heap < SVC_STORM_HEAP_FLOOR) {
                ESP_LOGE(TAG, "TLS storm: %d consecutive fails, pool heap %u < %u - restarting for a clean pool",
                         s_fail_streak + 1, (unsigned)free_heap,
                         (unsigned)SVC_STORM_HEAP_FLOOR);
                vTaskDelay(pdMS_TO_TICKS(500));
                esp_restart();
            }
            int backoff_ms = SVC_BACKOFF_BASE_MS << (s_fail_streak > 4 ? 4 : s_fail_streak);
            if (backoff_ms > SVC_BACKOFF_MAX_MS) {
                backoff_ms = SVC_BACKOFF_MAX_MS;
            }
            if (s_fail_streak < 5) {
                s_fail_streak++;
            }
            ESP_LOGW(TAG, "fetch failed (%s) - waiting %d s before retrying",
                     esp_err_to_name(err), backoff_ms / 1000);
            espn_api_conn_release();   /* never park a ~22 K TLS session across a sleep */
            if (xSemaphoreTake(s_wake, pdMS_TO_TICKS(backoff_ms))) {
                /* Tab tap or refresh button during the backoff: stop feeling
                   sorry for ourselves and handle it early. */
                ESP_LOGI(TAG, "wake during backoff - handling it now");
            }
            /* Timeout OR early wake: either way the retry is governed by the
               backoff, never by the bottom take. (Falling through there made
               a failed cycle sleep the full refresh interval whenever nothing
               poked the worker - exposed by a boot whose only failure had no
               pending page poke: silent board for 15 minutes.) */
            continue;
        }

#ifdef CYD_NBA_AUTOPAGE
        /* Soak hook: walk the pager without touching the screen, stepping back
           to the newest page afterwards. Only on success, so a rate-limited or
           offline board does not hammer the API. */
        if (err == ESP_OK && !s_older_exhausted) {
            /* Not while the edge is declared: a step onto it renders the
               nearest page (no HTTP), the render clamp pulls s_page back,
               and the next cycle steps again - a render-only storm that
               buries the soak log. The walk re-earns the page after a
               fresh fill clears the flag. */
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

#ifdef CYD_NBA_AUTOTAB
        /* Soak hook: hop to the college feed and back over two cycles, so the
           whole tab-switch path (leave-stash, page box take, instant paint,
           slate re-anchor without a hunt) is exercised without a finger on
           the glass. */
        static int autotab_phase = 0;
        if (err == ESP_OK && autotab_phase < 2 && s_page_box[LEAGUE_NCAAB].count > 0) {
            autotab_phase++;
            ESP_LOGI(TAG, "autotab: switching to %s",
                     autotab_phase == 1 ? "NCAAB" : "NBA");
            nba_service_set_league(autotab_phase == 1 ? LEAGUE_NCAAB : LEAGUE_NBA);
        }
#endif

        /* Sleep until the next periodic refresh, or until the header button or
           a page-pager button gives the semaphore. Release the keep-alive
           first: an open TLS session parks ~22 KB for the whole 15 minutes,
           which is what pushed cycle-start heap below the safety floor in
           soak (reuse is a within-burst win only). */
        espn_api_conn_release();
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
   belong to the current feed, so only the *request* happens here: the worker
   applies it (wipe + "Loading..." + re-anchor) at a cycle boundary and drops
   in-flight fetches at their next request, which also makes a tap land in
   seconds instead of waiting out the cycle it caught.
   Called from an LVGL event callback, which already holds the port mutex -
   no LVGL calls here, that happens on the worker. */
void nba_service_set_league(league_t lg)
{
    if ((int)lg == s_pending_league) {
        return;                    /* already queued */
    }
    if ((int)lg == (int)s_league) {
        s_pending_league = -1;     /* tapped back before the worker caught up */
        return;
    }
    s_pending_league = (int)lg;
    s_dirty = true;
    s_league_switched = true;
    ESP_LOGI(TAG, "feed switch requested -> %s", lg_name(lg));
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
#ifdef CYD_NBA_PAGEMON
    s_step_ms = (uint32_t)(esp_timer_get_time() / 1000);
#endif
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
