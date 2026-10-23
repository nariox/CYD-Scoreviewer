#pragma once

/* wolfSSL feasibility probe (spike branch only).
 *
 * Started from app_main in place of nba_service_start() when the build
 * defines CYD_WOLFSSL_SPIKE=<rounds>, so the heap samples belong to
 * wolfSSL alone (the mbedTLS feed worker never runs in a spike build):
 *
 *   idf.py -DCYD_WOLFSSL_SPIKE=5 build && idf.py -p /dev/ttyUSB0 flash
 *
 * Each round does a raw wolfSSL handshake + GET against the SAME URL the
 * production feed uses (HEAD-vs-GET lesson: probe the full message shape),
 * logging free heap + largest internal block before, a background monitor's
 * minimum across the blocking handshake/read phase, and the negotiated
 * version/cipher/curve. Version alternates 1.2-cap / 1.3-flex; every round
 * loads the static-memory pool (44 KB WOLFMEM_GENERAL + 36 KB
 * WOLFMEM_IO_POOL) via wolfSSL_CTX_load_static_memory() — the point of the
 * spike is whether the heap monitor then stops dipping by the handshake's
 * transient (the pools are live .bss: boot heap reads ~68 KB lower than a
 * poolless spike).
 *
 * Both versions pin the group to X25519 (wolfSSL_UseSupportedCurve +
 * wolfSSL_UseKeyShare). Without the pin, TLS 1.3's ClientHello keyshare
 * defaults to P-256 and TFM (FP_MAX_BITS=8192 for the RSA-4096 cross root)
 * makes its scalar mul want ~10 concurrent 3.2 KB ecc_points - the pool
 * failure that captures 7-9 chased down. X25519 math is stack-based (zero
 * pool objects), and Akamai negotiates it on 1.2 and 1.3 alike.
 */
void wolfssl_spike_start(void);
