#pragma once

// ── WiFi ─────────────────────────────────────────────────────────────────────
#define TRMNL_WIFI_SSID        "your_ssid"
#define TRMNL_WIFI_PASSWORD    "your_password"
#define TRMNL_WIFI_TIMEOUT_MS  20000   // 20 s

// ── TRMNL server ─────────────────────────────────────────────────────────────
#define TRMNL_API_KEY          "your_api_key"
#define TRMNL_SERVER           "https://trmnl.app"
#define TRMNL_API_ENDPOINT     "/api/display"
#define TRMNL_FW_VERSION       "1.0.0"

// ── Timing ───────────────────────────────────────────────────────────────────
// Default sleep between fetches (seconds). The server can override via refresh_rate.
#define TRMNL_DEFAULT_REFRESH_SEC  900   // 15 min

// Retry backoff after API/WiFi failure.
// Counter is stored in RTC memory across deep-sleep cycles.
#define TRMNL_RETRY_1_SEC   30
#define TRMNL_RETRY_2_SEC   60
#define TRMNL_RETRY_3_SEC   300

// ── Image ─────────────────────────────────────────────────────────────────────
// Maximum raw JPEG/BMP payload the device will accept (bytes in PSRAM).
#define TRMNL_MAX_IMAGE_BYTES  (512 * 1024)
