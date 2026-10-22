#include "time_sync.h"

#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <sys/time.h>
#include <time.h>

#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_system.h"

static const char *TAG = "time_sync";

/* Any HTTPS endpoint returns a valid Date header; using ESPN (the one feed we
   always talk to now) keeps the CA/SNI footprint to a single host. ESPN's edge
   404s unknown clients, so send the browser UA the scoreboard wants; the Date
   header arrives regardless of status anyway. */
#define TIME_SYNC_URL "https://site.api.espn.com/apis/site/v2/sports/basketball/nba/scoreboard"
#define TIME_SYNC_UA  "Mozilla/5.0"

static char s_date_header[64];
static int s_synced = 0;

static esp_err_t http_event_cb(esp_http_client_event_t *evt)
{
    if (evt->event_id == HTTP_EVENT_ON_HEADER &&
        evt->header_key && evt->header_value &&
        strcasecmp(evt->header_key, "Date") == 0) {
        strncpy(s_date_header, evt->header_value, sizeof(s_date_header) - 1);
        s_date_header[sizeof(s_date_header) - 1] = '\0';
    }
    return ESP_OK;
}

/* Days since civil epoch (Howard Hinnant's algorithm), valid for
   Gregorian calendar dates well past 1970. */
static long days_from_civil(int y, unsigned m, unsigned d)
{
    y -= m <= 2;
    const int era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = (unsigned)(y - era * 400);
    const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return (long)era * 146097 + (long)doe - 719468;
}

/* Parse RFC 1123 dates: "Sat, 17 Oct 2026 04:33:29 GMT" */
static int parse_rfc1123(const char *s, time_t *out)
{
    static const char *months[] = {
        "Jan", "Feb", "Mar", "Apr", "May", "Jun",
        "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
    };
    int year, hour, min, sec, day;
    char mon[4] = {0};

    /* skip "<Wdy>, " */
    const char *p = strchr(s, ' ');
    if (!p) {
        return 0;
    }
    p++;

    if (sscanf(p, "%d %3s %d %d:%d:%d", &day, mon, &year, &hour, &min, &sec) != 6) {
        return 0;
    }

    int m = -1;
    for (int i = 0; i < 12; i++) {
        if (strncasecmp(mon, months[i], 3) == 0) {
            m = i + 1;
            break;
        }
    }
    if (m < 0) {
        return 0;
    }

    long days = days_from_civil(year, (unsigned)m, (unsigned)day);
    *out = (time_t)(days * 86400L + hour * 3600 + min * 60 + sec);
    return 1;
}

esp_err_t time_sync_from_http(void)
{
    /* HEAD: the Date header is the whole payload we want, and an ESPN
       scoreboard day would otherwise stream 200-400 KB of JSON past us. */
    esp_http_client_config_t cfg = {
        .url = TIME_SYNC_URL,
        .method = HTTP_METHOD_HEAD,
        .event_handler = http_event_cb,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = 10000,
        .buffer_size = 512,
    };

    s_date_header[0] = '\0';

    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) {
        ESP_LOGE(TAG, "HTTP client init failed (heap: %u)",
                 esp_get_free_heap_size());
        return ESP_ERR_NO_MEM;
    }
    esp_http_client_set_header(client, "User-Agent", TIME_SYNC_UA);

    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    /* Only a transport failure (no Date header received) is fatal. */
    if (s_date_header[0] == '\0') {
        ESP_LOGE(TAG, "Time sync request failed: %s (HTTP %d, heap %u, largest internal block %u)",
                 esp_err_to_name(err), status,
                 (unsigned)esp_get_free_heap_size(),
                 (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
        return (err != ESP_OK) ? err : ESP_FAIL;
    }

    ESP_LOGI(TAG, "Date header: \"%s\" (HTTP %d)", s_date_header, status);

    time_t now;
    if (s_date_header[0] == '\0' || !parse_rfc1123(s_date_header, &now)) {
        ESP_LOGE(TAG, "Could not parse Date header");
        return ESP_FAIL;
    }

    struct timeval tv = { .tv_sec = now, .tv_usec = 0 };
    if (settimeofday(&tv, NULL) != 0) {
        ESP_LOGE(TAG, "settimeofday failed");
        return ESP_FAIL;
    }

    s_synced = 1;
    ESP_LOGI(TAG, "Clock set to %s", s_date_header);
    return ESP_OK;
}

int time_sync_is_synced(void)
{
    return s_synced;
}

time_t time_sync_game_day_epoch(void)
{
    if (!s_synced) {
        return 0;
    }
    time_t when = time(NULL) - 12 * 3600;
    struct tm tm;
    gmtime_r(&when, &tm);
    tm.tm_hour = 0;
    tm.tm_min = 0;
    tm.tm_sec = 0;
    time_t day = mktime(&tm);       /* TZ is UTC here */
    return day == (time_t)-1 ? 0 : day;
}
