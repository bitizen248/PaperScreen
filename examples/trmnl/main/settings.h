#pragma once

// ── WiFi ─────────────────────────────────────────────────────────────────────
#define TRMNL_WIFI_SSID        "your_ssid"
#define TRMNL_WIFI_PASSWORD    "your_password"
#define TRMNL_WIFI_TIMEOUT_MS  20000   // 20 s max for connection

// ── TRMNL server ─────────────────────────────────────────────────────────────
#define TRMNL_API_KEY          "your_api_key"
#define TRMNL_SERVER           "https://trmnl.app"
#define TRMNL_API_ENDPOINT     "/api/display"
#define TRMNL_FW_VERSION       "1.1.0"

// ── Timing ───────────────────────────────────────────────────────────────────
// Default sleep when the server does not provide refresh_rate.
#define TRMNL_DEFAULT_REFRESH_SEC  900   // 15 min

// Retry backoff after any failure. Counter lives in RTC memory across deep sleeps.
#define TRMNL_RETRY_1_SEC   30
#define TRMNL_RETRY_2_SEC   60
#define TRMNL_RETRY_3_SEC   300

// How long to sleep when the device is not yet registered (status 202).
#define TRMNL_UNREGISTERED_SLEEP_SEC  300   // 5 min, keeps polling pairing server

// ── Image ─────────────────────────────────────────────────────────────────────
// Maximum raw image payload accepted into PSRAM (bytes).
#define TRMNL_MAX_IMAGE_BYTES       (1024 * 1024)   // 1 MB

// Default HTTP timeout for image download (ms). Server can override via image_url_timeout.
#define TRMNL_IMAGE_DOWNLOAD_TIMEOUT_MS  30000

// ── Display ───────────────────────────────────────────────────────────────────
// Anti-ghosting refresh cycle:
//   Every TRMNL_FULL_REFRESH_EVERY partial (DU) updates → do one GL16 update.
//   Every TRMNL_GHOST_CLEAR_EVERY GL16 updates          → do one GC16 update.
#define TRMNL_FULL_REFRESH_EVERY    4    // DU → GL16
#define TRMNL_GHOST_CLEAR_EVERY     4    // GL16 → GC16

// Refresh-rate threshold (seconds) above which we always use GL16 instead of DU
// to avoid ghosting from long idle periods.
#define TRMNL_SLOW_REFRESH_THRESHOLD_SEC  1800  // 30 min
