/* wolfSSL feasibility probe - see wolfssl_spike.h.
 *
 * Question this answers with device numbers (paper can't):
 *   1. What does a wolfSSL handshake actually cost the system heap,
 *      peak included? (mbedTLS: ~20-30 KB transient, fails under ~45.5 K
 *      free - the "cert storm" band.)
 *   2. Does the GET body path survive Akamai's 16 KB records? (mbedTLS
 *      needs the full 16 KB IN buffer; wolfSSL may size differently.)
 *   3. Flash weight of the lib in a linked build.
 *
 * Rounds alternate TLS 1.2-capped and version-flexible so both handshake
 * shapes get sampled. Only this task touches the network in a spike build
 * (app_main swaps nba_service_start() out), so every heap line is pure wolfSSL.
 */

#include "wolfssl_spike.h"

#ifdef CYD_WOLFSSL_SPIKE

#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <sys/time.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "lwip/sockets.h"
#include "lwip/netdb.h"

/* The component sets -DWOLFSSL_USER_SETTINGS only for its own sources; our
 * TU needs it too or wolfssl/wolfcrypt/settings.h falls into its
 * "No configuration for wolfSSL detected" warning path (-Werror). */
#define WOLFSSL_USER_SETTINGS
#include "wolfssl/ssl.h"
#include "ca_isrg_root_x1.h"

static const char *TAG = "wolfspike";

/* Same host + path shape as espn_api.c (message-shape lesson). */
#define SPIKE_HOST "site.api.espn.com"
#define SPIKE_PATH "/apis/site/v2/sports/basketball/nba/scoreboard?dates=20261021"
#define SPIKE_REQ_TIMEOUT_MS 15000

/* Rounds run against a static-memory pool (heap rounds were measured in the
 * second capture: 37-38 KB transient on TLS 1.2, 50-52 KB on TLS 1.3 and the
 * pool eats the heap budget anyway). The pool is the canonical TWO-region
 * layout: WOLFMEM_GENERAL carves bucket blocks (real occupants per the
 * alloc-trace capture: Cert 3356, TFM RsaKey 5348, ecc_key 4288, ecc_point
 * 3196 - the 30400-bucket folklore is a WOLFSSL object that never materialises
 * here, it is 948 B) and WOLFMEM_IO_POOL carves the WOLFMEM_IO_SZ (16992 B)
 * record buffer - a general-only region has no io slots and exhausts mid
 * connect (capture 3: MEMORY_E at 15 ms). ONE slot suffices: the capture-11
 * alloc trace replayed per round shows the io region never held more than a
 * single live block (IN buffer during body reads; OUT stays a ~96 B gen
 * block, our GETs are tiny), so the pool drops 36 K -> 20 K (capture 18).
 * If the monitor stops dipping by the handshake's transient,
 * WOLFSSL_STATIC_MEMORY works. */
static int round_flex(int idx)   { return (idx % 2) == 0; }
static int round_static(int idx) { (void)idx; return 1; }

#ifdef WOLFSSL_STATIC_MEMORY
static unsigned char s_genpool[44 * 1024] __attribute__((aligned(16)));
static unsigned char s_iopool[20 * 1024] __attribute__((aligned(16)));
#endif

/* ------------------------------------------------------------------ */
/* Heap monitor: samples while the blocking handshake/read run.        */

static TaskHandle_t s_mon;
static volatile bool s_mon_run;
static volatile uint32_t s_mon_min_free;
static volatile uint32_t s_mon_min_largest;

static void mon_task(void *arg)
{
    (void)arg;
    while (s_mon_run) {
        uint32_t free_now = esp_get_free_heap_size();
        uint32_t largest = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
        if (free_now < s_mon_min_free) s_mon_min_free = free_now;
        if (largest < s_mon_min_largest) s_mon_min_largest = largest;
        vTaskDelay(pdMS_TO_TICKS(15));
    }
    vTaskDelete(NULL);
}

static void mon_start(void)
{
    s_mon_min_free = UINT32_MAX;
    s_mon_min_largest = UINT32_MAX;
    s_mon_run = true;
    xTaskCreate(mon_task, "wspike_mon", 2048, NULL, 5, &s_mon);
}

static void mon_stop(char *out, size_t out_len)
{
    s_mon_run = false;
    vTaskDelay(pdMS_TO_TICKS(40)); /* let the sampler close out */
    snprintf(out, out_len, "min free %lu, min largest int %lu",
             (unsigned long)s_mon_min_free, (unsigned long)s_mon_min_largest);
}

static void log_heap(const char *where)
{
    ESP_LOGI(TAG, "heap @ %-16s free %u, largest int %u",
             where,
             (unsigned)esp_get_free_heap_size(),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
}

/* ------------------------------------------------------------------ */

static void err_line(const char *what, WOLFSSL *ssl, int ret)
{
    char ebuf[160];
    int err = ssl ? wolfSSL_get_error(ssl, ret) : -1;
    ebuf[0] = '\0';
#ifndef NO_ERROR_STRINGS
    if (err > 0) wolfSSL_ERR_error_string((unsigned long)err, ebuf);
#else
    if (err > 0) snprintf(ebuf, sizeof(ebuf), "code %d", err);
#endif
    ESP_LOGE(TAG, "%s failed (ret %d, wolfssl err %d): %s", what, ret, err, ebuf);
}

static void one_round(int idx, int flex_version, int use_static)
{
    char mon_msg[96];
    int sockfd = -1;
    WOLFSSL_CTX *ctx = NULL;
    WOLFSSL *ssl = NULL;
    int ret;
    int64_t t0;

    ESP_LOGI(TAG, "=== round %d/%d (%s, %s) ===", idx, CYD_WOLFSSL_SPIKE,
             flex_version ? "TLS 1.3 flex" : "TLS 1.2 cap",
             use_static ? "static pool" : "heap alloc");
    log_heap("round start");

    mon_start();

    /* --- TCP --- */
    struct addrinfo hints = { .ai_family = AF_INET, .ai_socktype = SOCK_STREAM };
    struct addrinfo *res = NULL;
    ret = getaddrinfo(SPIKE_HOST, "443", &hints, &res);
    if (ret != 0) {
        ESP_LOGE(TAG, "getaddrinfo %s: %d", SPIKE_HOST, ret);
        goto done;
    }
    sockfd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (sockfd < 0) {
        ESP_LOGE(TAG, "socket: %d", errno);
        freeaddrinfo(res);
        goto done;
    }
    struct timeval tv = { .tv_sec = SPIKE_REQ_TIMEOUT_MS / 1000, .tv_usec = 0 };
    setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(sockfd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    ret = connect(sockfd, res->ai_addr, res->ai_addrlen);
    freeaddrinfo(res);
    if (ret != 0) {
        ESP_LOGE(TAG, "tcp connect: %d", errno);
        goto done;
    }

    /* --- TLS --- */
    ctx = wolfSSL_CTX_new(flex_version ? wolfTLS_client_method()
                                       : wolfTLSv1_2_client_method());
    if (!ctx) {
        ESP_LOGE(TAG, "CTX_new failed (OOM?)");
        goto done;
    }
#ifdef WOLFSSL_STATIC_MEMORY
    if (use_static) {
        WOLFSSL_CTX *ctx_ref = ctx; /* the API takes ctx** for legacy reasons */
        /* maxSz = 0: unlimited concurrent handshakes/IO connections
         * (1 would arm the maxHa<=curHa rejection gate in internal.c). */
        ret = wolfSSL_CTX_load_static_memory(&ctx_ref, NULL, s_genpool,
                                             (int)sizeof(s_genpool),
                                             WOLFMEM_GENERAL, 0);
        if (ret == WOLFSSL_SUCCESS) {
            ret = wolfSSL_CTX_load_static_memory(&ctx_ref, NULL, s_iopool,
                                                 (int)sizeof(s_iopool),
                                                 WOLFMEM_IO_POOL, 0);
        }
        if (ret != WOLFSSL_SUCCESS) {
            err_line("load_static_memory", NULL, ret);
            goto done;
        }
        log_heap("pools loaded");
    }
#endif
    ret = wolfSSL_CTX_load_verify_buffer(ctx,
                                         (const unsigned char *)CA_ISRG_ROOT_X1_PEM,
                                         (long)sizeof(CA_ISRG_ROOT_X1_PEM) - 1,
                                         SSL_FILETYPE_PEM);
    if (ret != WOLFSSL_SUCCESS) {
        err_line("load_verify_buffer", NULL, ret);
        goto done;
    }
    wolfSSL_CTX_set_verify(ctx, SSL_VERIFY_PEER, NULL);

    ssl = wolfSSL_new(ctx);
    if (!ssl) {
        ESP_LOGE(TAG, "SSL_new failed (OOM?)");
        goto done;
    }
    wolfSSL_set_fd(ssl, sockfd);
    wolfSSL_UseSNI(ssl, WOLFSSL_SNI_HOST_NAME, SPIKE_HOST,
                   (unsigned short)strlen(SPIKE_HOST));
    /* Pin the group to X25519. Capture 9: TLS 1.2 picks X25519 on its own
     * (curve25519 math is stack-based - zero pool objects), but the TLS 1.3
     * default ClientHello keyshare is P-256, and TFM's P-256 scalar mul wants
     * ~10 concurrent 3.2K ecc_points (base + M[8] window + tG). Capture 10
     * lesson: wolfSSL_UseSupportedCurve only restricts the advertised
     * supported_groups - the KEY SHARE default is picked by
     * TLSX_PopulateExtensions as preferredGroup[0] (secp256r1) unless a
     * keyshare entry already exists, so seed the x25519 entry explicitly.
     * Akamai serves X25519 on 1.3 too. */
    ret = wolfSSL_UseSupportedCurve(ssl, WOLFSSL_ECC_X25519);
    if (ret != WOLFSSL_SUCCESS) {
        err_line("wolfSSL_UseSupportedCurve", ssl, ret);
        goto done;
    }
    ret = wolfSSL_UseKeyShare(ssl, WOLFSSL_ECC_X25519);
    if (ret != WOLFSSL_SUCCESS) {
        err_line("wolfSSL_UseKeyShare", ssl, ret);
        goto done;
    }
#ifdef HAVE_MAX_FRAGMENT
    /* Stock component settings leave HAVE_MAX_FRAGMENT off - this branch is
     * dead unless a custom settings root (CONFIG_CUSTOM_SETTING_WOLFSSL_ROOT)
     * turns it on. If it ever runs: does Akamai honour MFL_4096? */
    wolfSSL_UseMaxFragment(ssl, MFL_4096);
#endif

    t0 = esp_timer_get_time();
    ret = wolfSSL_connect(ssl);
    if (ret != WOLFSSL_SUCCESS) {
        err_line("wolfSSL_connect", ssl, ret);
#ifdef WOLFSSL_STATIC_MEMORY
        if (use_static) {
            ESP_LOGE(TAG, "(static round: MEMORY_E here = pool too small: gen %dK / io %dK)",
                     (int)(sizeof(s_genpool) / 1024), (int)(sizeof(s_iopool) / 1024));
        }
#endif
        goto done;
    }
    ESP_LOGI(TAG, "handshake OK in %lld ms: %s / %s / curve %s",
             (long long)((esp_timer_get_time() - t0) / 1000),
             wolfSSL_get_version(ssl), wolfSSL_get_cipher_name(ssl),
             wolfSSL_get_curve_name(ssl) ?: "?");
    log_heap("post-handshake");

    /* --- GET (full production message shape, incl. browser UA) --- */
    static char req[512];
    static char buf[2048];
    int req_len = snprintf(req, sizeof(req),
                           "GET %s HTTP/1.1\r\n"
                           "Host: " SPIKE_HOST "\r\n"
                           "User-Agent: Mozilla/5.0\r\n"
                           "Accept-Encoding: identity\r\n"
                           "Connection: close\r\n\r\n",
                           SPIKE_PATH);
    ret = (int)wolfSSL_write(ssl, req, req_len);
    if (ret != req_len) {
        err_line("wolfSSL_write", ssl, ret);
        goto done;
    }

    long long total = 0, events = 0;
    int status = -1, chunks = 0;
    t0 = esp_timer_get_time();
    while (total < 400 * 1024) {
        ret = wolfSSL_read(ssl, buf, sizeof(buf));
        if (ret <= 0) break; /* 0 = peer closed (Connection: close) */
        chunks++;
        total += ret;
        if (status < 0 && total >= 12) {
            if (!strncmp(buf, "HTTP/1.1 200", 12) || !strncmp(buf, "HTTP/1.0 200", 12))
                status = 200;
            else
                status = (buf[9] - '0') * 100 + (buf[10] - '0') * 10 + (buf[11] - '0');
        }
        for (int i = 0; i + 6 <= ret; i++) {
            if (!memcmp(buf + i, "events", 6)) events++;
        }
    }
    ESP_LOGI(TAG, "body: status %d, %lld bytes in %d reads, %lld 'events' hits, %lld ms",
             status, total, chunks, events,
             (long long)((esp_timer_get_time() - t0) / 1000));

done:
    mon_stop(mon_msg, sizeof(mon_msg));
    if (ssl) wolfSSL_free(ssl);
    if (ctx) wolfSSL_CTX_free(ctx);
    if (sockfd >= 0) close(sockfd);
    ESP_LOGI(TAG, "round %d teardown: %s", idx, mon_msg);
    log_heap("post-teardown");
}

static void set_clock_from_build_date(void)
{
    /* The spike replaces the feed worker, so time_sync's Date-header probe
     * never runs and the clock stays at 1970 - wolfSSL then rejects every CA
     * with ASN_BEFORE_DATE_E (-150). Stamp noon UTC of the build day instead:
     * inside every relevant validity window, zero network before round 1. */
    struct tm tm = { 0 };
    char mon[4] = { 0 };
    if (sscanf(__DATE__, "%3s %d %d", mon, &tm.tm_mday, &tm.tm_year) == 3) {
        static const char months[] = "JanFebMarAprMayJunJulAugSepOctNovDec";
        const char *p = strstr(months, mon);
        tm.tm_year -= 1900;
        tm.tm_mon = p ? (int)(p - months) / 3 : 0;
        time_t t = timegm(&tm) + 12 * 3600;
        struct timeval tv = { .tv_sec = t };
        settimeofday(&tv, NULL);
        ESP_LOGI(TAG, "clock stamped %s 12:00 UTC (build day)", __DATE__);
    }
}

static void spike_task(void *arg)
{
    (void)arg;
    set_clock_from_build_date();
    ESP_LOGI(TAG, "wolfSSL spike: %d rounds, host " SPIKE_HOST, CYD_WOLFSSL_SPIKE);

    int ret = wolfSSL_Init();
    if (ret != WOLFSSL_SUCCESS) {
        ESP_LOGE(TAG, "wolfSSL_Init failed: %d", ret);
        vTaskDelete(NULL);
        return;
    }
    log_heap("after init");

    for (int i = 1; i <= CYD_WOLFSSL_SPIKE; i++) {
        one_round(i, round_flex(i), round_static(i));
        vTaskDelay(pdMS_TO_TICKS(6000));
    }

    wolfSSL_Cleanup();
    ESP_LOGI(TAG, "spike task stack: %u bytes used of %u",
             (unsigned)(24576 - uxTaskGetStackHighWaterMark(NULL) * sizeof(StackType_t)), 24576u);
    ESP_LOGI(TAG, "spike done - board idle, no feed ran this boot");
    vTaskDelete(NULL);
}

void wolfssl_spike_start(void)
{
    /* 24 KB: NO_ESP32_CRYPT means software RSA math, and with
     * FP_MAX_BITS=8192 (4096-bit root verification) each TFM fp_int is
     * ~1.1 KB - RSA-4096 verify wants roughly ten of them live at once. */
    xTaskCreate(spike_task, "wolfspike", 24576, NULL, 5, NULL);
}

#else /* !CYD_WOLFSSL_SPIKE */

void wolfssl_spike_start(void) {}

#endif
