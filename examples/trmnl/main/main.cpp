/*
 * TRMNL client for LilyGO T5 E-Paper S3 Pro (ED047TC1, 960×540)
 *
 * Wake-up cycle:  WiFi → API → (cache/download) → decode → ONE EPD refresh → deep sleep
 *
 * Bug fixes vs. naive implementations
 * ─────────────────────────────────────
 * 1. Double render (old → new flash):
 *    The physical display is never touched until the complete image is decoded
 *    into the framebuffer.  display_update_once() is the sole EPD operation.
 *
 * 2. Error screen stuck forever:
 *    Every failure calls fail_and_sleep().  The retry counter in RTC memory
 *    drives exponential backoff (30 s → 60 s → 300 s) and the device retries
 *    automatically.  The previous image stays visible without power.
 */

#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <LittleFS.h>
#include <Update.h>
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_sleep.h>

extern "C" {
#include "epdiy.h"
#include "epd_highlevel.h"
}

#include "JPEGDEC.h"
#include "PNGdec.h"
#include "settings.h"

// ── Panel / waveform ──────────────────────────────────────────────────────────
#define DEMO_BOARD epd_board_v7
#define WAVEFORM   EPD_BUILTIN_WAVEFORM
extern const EpdDisplay_t ED047TC1;
static EpdiyHighlevelState s_hl;

// ── RTC state – persists across deep-sleep cycles, cleared on power-off ───────
RTC_DATA_ATTR static int      s_retry_count     = 0;
RTC_DATA_ATTR static uint8_t  s_du_counter      = 0;   // fast-refresh cycle counter
RTC_DATA_ATTR static uint8_t  s_gl_counter      = 0;   // GL16 cycle counter
RTC_DATA_ATTR static char     s_prev_filename[128] = "";

// ── Buffers ───────────────────────────────────────────────────────────────────
// 4bpp decodebuffer: even pixel in low nibble, odd pixel in high nibble (epdiy convention)
static uint8_t  *s_decode_buf  = nullptr;
// Downloaded raw image bytes (PSRAM)
static uint8_t  *s_img_buf     = nullptr;
static size_t    s_img_buf_len = 0;

// ── Decoders ─────────────────────────────────────────────────────────────────
static JPEGDEC s_jpeg;
static PNG     s_png;

// Line buffer shared by PNG callback (max display width)
static uint16_t s_png_line[1024];

static const char *TAG = "TRMNL";

// ── LittleFS cache paths ──────────────────────────────────────────────────────
static constexpr const char *CACHE_IMG  = "/img";
static constexpr const char *CACHE_NAME = "/name";

// ─────────────────────────────────────────────────────────────────────────────
// Sleep / retry helpers
// ─────────────────────────────────────────────────────────────────────────────

static void sleep_for(uint32_t seconds)
{
    ESP_LOGI(TAG, "Deep sleep %lu s", (unsigned long)seconds);
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    esp_deep_sleep((uint64_t)seconds * 1000000ULL);
}

static void fail_and_sleep(const char *reason)
{
    ESP_LOGW(TAG, "Fail [retry %d]: %s", s_retry_count, reason);
    // Previous content stays on EPD without power – no display action needed.
    s_retry_count = (s_retry_count < 3) ? s_retry_count + 1 : 3;
    uint32_t backoff = (s_retry_count == 1) ? TRMNL_RETRY_1_SEC
                     : (s_retry_count == 2) ? TRMNL_RETRY_2_SEC
                                            : TRMNL_RETRY_3_SEC;
    sleep_for(backoff);
}

// ─────────────────────────────────────────────────────────────────────────────
// Pixel helper: write one 4bpp gray pixel into s_decode_buf
// ─────────────────────────────────────────────────────────────────────────────

static inline void write_pixel(int x, int y, uint8_t gray4)
{
    int pitch = (epd_rotated_display_width() + 1) / 2;
    uint8_t *dst = &s_decode_buf[y * pitch + (x >> 1)];
    if (x & 1) *dst = (*dst & 0x0F) | (uint8_t)(gray4 << 4);
    else        *dst = (*dst & 0xF0) | gray4;
}

// RGB565 (little-endian) → 4-bit luminance
static inline uint8_t rgb565_to_gray4(uint16_t rgb)
{
    uint8_t r5 = (rgb >> 11) & 0x1F;
    uint8_t g6 = (rgb >>  5) & 0x3F;
    uint8_t b5 =  rgb        & 0x1F;
    // BT.601 luma on native 5/6/5 ranges, shift into [0..255]
    uint8_t luma = (uint8_t)((616u * r5 + 600u * g6 + 232u * b5) >> 8);
    return luma >> 4;
}

// ─────────────────────────────────────────────────────────────────────────────
// JPEG decode callback
// ─────────────────────────────────────────────────────────────────────────────

static int jpeg_draw_cb(JPEGDRAW *d)
{
    int ep_w = epd_rotated_display_width();
    int ep_h = epd_rotated_display_height();
    for (int row = 0; row < d->iHeight; row++) {
        int y = d->y + row;
        if (y < 0 || y >= ep_h) continue;
        for (int col = 0; col < d->iWidth; col++) {
            int x = d->x + col;
            if (x < 0 || x >= ep_w) continue;
            write_pixel(x, y, rgb565_to_gray4(d->pPixels[row * d->iWidth + col]));
        }
    }
    return 1;
}

// ─────────────────────────────────────────────────────────────────────────────
// PNG decode callback
// ─────────────────────────────────────────────────────────────────────────────

static void png_draw_cb(PNGDRAW *d)
{
    int y = d->y;
    if (y >= epd_rotated_display_height()) return;

    // getLineAsRGB565 handles all PNG pixel types (indexed, gray, truecolor, alpha)
    s_png.getLineAsRGB565(d, s_png_line, PNG_RGB565_LITTLE_ENDIAN, 0xFFFFFFFF);

    int ep_w = epd_rotated_display_width();
    int cols  = (d->iWidth < ep_w) ? d->iWidth : ep_w;
    for (int x = 0; x < cols; x++) {
        write_pixel(x, y, rgb565_to_gray4(s_png_line[x]));
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Image format detection and decoding
// ─────────────────────────────────────────────────────────────────────────────

static bool decode_image(uint8_t *buf, size_t len)
{
    // Detect format from magic bytes
    bool is_png  = (len >= 4 && buf[0] == 0x89 && buf[1] == 0x50);  // \x89PNG
    bool is_jpeg = (len >= 2 && buf[0] == 0xFF && buf[1] == 0xD8);

    // Clear decode buffer to white before writing new pixels
    {
        int w = epd_rotated_display_width();
        int h = epd_rotated_display_height();
        memset(s_decode_buf, 0xFF, ((size_t)(w + 1) / 2) * (size_t)h);
    }

    if (is_jpeg) {
        if (!s_jpeg.openRAM(buf, (int)len, jpeg_draw_cb)) {
            ESP_LOGE(TAG, "JPEG open failed: %d", s_jpeg.getLastError());
            return false;
        }
        s_jpeg.setPixelType(RGB565_LITTLE_ENDIAN);
        bool ok = (s_jpeg.decode(0, 0, 0) == 1);
        if (!ok) ESP_LOGE(TAG, "JPEG decode failed: %d", s_jpeg.getLastError());
        s_jpeg.close();
        return ok;
    }

    if (is_png) {
        if (!s_png.openRAM(buf, (int)len, png_draw_cb)) {
            ESP_LOGE(TAG, "PNG open failed: %d", s_png.getLastError());
            return false;
        }
        bool ok = (s_png.decode(nullptr, 0) == 1);
        if (!ok) ESP_LOGE(TAG, "PNG decode failed: %d", s_png.getLastError());
        s_png.close();
        return ok;
    }

    ESP_LOGE(TAG, "Unknown image format (magic: %02X %02X)", buf[0], buf[1]);
    return false;
}

// ─────────────────────────────────────────────────────────────────────────────
// LittleFS image cache
// ─────────────────────────────────────────────────────────────────────────────

static bool cache_load(const char *expected_filename, uint8_t **out_buf, size_t *out_len)
{
    if (!LittleFS.begin(false)) return false;

    // Verify cached filename matches
    File nf = LittleFS.open(CACHE_NAME, "r");
    if (!nf) return false;
    char stored[128] = {};
    nf.readBytes(stored, sizeof(stored) - 1);
    nf.close();
    if (strcmp(stored, expected_filename) != 0) return false;

    File img = LittleFS.open(CACHE_IMG, "r");
    if (!img) return false;
    size_t sz = img.size();
    if (sz == 0 || sz > TRMNL_MAX_IMAGE_BYTES) { img.close(); return false; }

    uint8_t *buf = (uint8_t *)heap_caps_malloc(sz, MALLOC_CAP_SPIRAM);
    if (!buf) { img.close(); return false; }
    img.read(buf, sz);
    img.close();

    *out_buf = buf;
    *out_len = sz;
    ESP_LOGI(TAG, "Cache hit for '%s' (%zu B)", expected_filename, sz);
    return true;
}

static void cache_save(const char *filename, const uint8_t *buf, size_t len)
{
    if (!LittleFS.begin(true)) return;

    File img = LittleFS.open(CACHE_IMG, "w");
    if (img) { img.write(buf, len); img.close(); }

    File nf = LittleFS.open(CACHE_NAME, "w");
    if (nf) { nf.print(filename); nf.close(); }

    ESP_LOGI(TAG, "Cached '%s' (%zu B)", filename, len);
}

// ─────────────────────────────────────────────────────────────────────────────
// Display init and refresh
// ─────────────────────────────────────────────────────────────────────────────

static void display_init()
{
    epd_init(&DEMO_BOARD, &ED047TC1, EPD_LUT_64K);
    epd_set_vcom(1560);  // adjust per hardware (mV)
    s_hl = epd_hl_init(WAVEFORM);
    epd_set_rotation(EPD_ROT_INVERTED_PORTRAIT);

    int w = epd_rotated_display_width();
    int h = epd_rotated_display_height();
    s_decode_buf = (uint8_t *)heap_caps_calloc(((size_t)(w + 1) / 2) * (size_t)h, 1, MALLOC_CAP_SPIRAM);
    ESP_LOGI(TAG, "EPD %dx%d", w, h);
}

// Choose the best EPD waveform mode based on the refresh rate and cycle counters.
// This implements the anti-ghosting strategy from the official TRMNL firmware:
//   - Slow refresh (>30 min): always use GL16 to avoid ghosting from long idle
//   - Fast refresh: DU most cycles, GL16 every N, GC16 every M×N (deep ghost clear)
static EpdDrawMode select_mode(uint32_t refresh_rate_sec, bool force_full)
{
    if (force_full || refresh_rate_sec >= TRMNL_SLOW_REFRESH_THRESHOLD_SEC) {
        return MODE_GL16;
    }

    s_du_counter++;
    if (s_du_counter % TRMNL_FULL_REFRESH_EVERY != 0) {
        return MODE_DU;
    }
    // Time for a GL16 refresh
    s_gl_counter++;
    if (s_gl_counter % TRMNL_GHOST_CLEAR_EVERY == 0) {
        return MODE_GC16;  // deep ghost-clear every (FULL×GHOST_CLEAR) cycles
    }
    return MODE_GL16;
}

static void display_update_once(uint32_t refresh_rate_sec, bool force_full)
{
    EpdRect full = {0, 0, epd_rotated_display_width(), epd_rotated_display_height()};
    EpdDrawMode mode = select_mode(refresh_rate_sec, force_full);
    ESP_LOGI(TAG, "EPD update mode=%d", (int)mode);

    epd_draw_rotated_image(full, s_decode_buf, epd_hl_get_framebuffer(&s_hl));
    epd_poweron();
    epd_hl_update_screen(&s_hl, mode, epd_ambient_temperature());
    epd_poweroff();
}

// ─────────────────────────────────────────────────────────────────────────────
// WiFi
// ─────────────────────────────────────────────────────────────────────────────

static bool wifi_connect()
{
    WiFi.mode(WIFI_STA);
    WiFi.begin(TRMNL_WIFI_SSID, TRMNL_WIFI_PASSWORD);
    unsigned long t = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - t < TRMNL_WIFI_TIMEOUT_MS) {
        delay(250);
    }
    bool ok = (WiFi.status() == WL_CONNECTED);
    ESP_LOGI(TAG, "WiFi %s RSSI=%d", ok ? "OK" : "FAIL", WiFi.RSSI());
    return ok;
}

// ─────────────────────────────────────────────────────────────────────────────
// TRMNL API response
// ─────────────────────────────────────────────────────────────────────────────

struct ApiResp {
    int      status            = -1;
    uint32_t refresh_rate      = TRMNL_DEFAULT_REFRESH_SEC;
    uint32_t img_timeout_ms    = TRMNL_IMAGE_DOWNLOAD_TIMEOUT_MS;
    bool     update_firmware   = false;
    bool     reset_firmware    = false;
    bool     max_compat        = false;  // maximum_compatibility
    char     image_url[512]    = {};
    char     filename[128]     = {};
    char     firmware_url[512] = {};
    char     special_fn[32]    = {};
};

// ── Minimal JSON field extraction (avoids JSON library dependency) ─────────────

static bool jstr(const char *js, const char *key, char *out, size_t sz)
{
    char pat[128];
    snprintf(pat, sizeof(pat), "\"%s\":", key);
    const char *p = strstr(js, pat);
    if (!p) return false;
    p += strlen(pat);
    while (*p == ' ') p++;
    if (*p != '"') return false;
    p++;
    size_t i = 0;
    while (*p && *p != '"' && i < sz - 1) out[i++] = *p++;
    out[i] = '\0';
    return i > 0;
}

static bool jint(const char *js, const char *key, int *out)
{
    char pat[128];
    snprintf(pat, sizeof(pat), "\"%s\":", key);
    const char *p = strstr(js, pat);
    if (!p) return false;
    p += strlen(pat);
    while (*p == ' ') p++;
    *out = atoi(p);
    return true;
}

static bool jbool(const char *js, const char *key)
{
    char pat[128];
    snprintf(pat, sizeof(pat), "\"%s\":", key);
    const char *p = strstr(js, pat);
    if (!p) return false;
    p += strlen(pat);
    while (*p == ' ') p++;
    return (strncmp(p, "true", 4) == 0);
}

static bool fetch_api(ApiResp &r)
{
    WiFiClientSecure sec;
    sec.setInsecure();
    HTTPClient http;
    String url = String(TRMNL_SERVER) + TRMNL_API_ENDPOINT;
    if (!http.begin(sec, url)) return false;
    http.setTimeout(15000);

    uint8_t mac[6]; WiFi.macAddress(mac);
    char mac_str[18];
    snprintf(mac_str, sizeof(mac_str), "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    http.addHeader("ID",            mac_str);
    http.addHeader("access-token",  TRMNL_API_KEY);
    http.addHeader("FW_VERSION",    TRMNL_FW_VERSION);
    http.addHeader("RSSI",          String(WiFi.RSSI()));
    http.addHeader("WIDTH",         String(epd_rotated_display_width()));
    http.addHeader("HEIGHT",        String(epd_rotated_display_height()));
    // Tell the server whether we have a locally cached image for this filename.
    // The server may still send a new URL if the content changed.
    http.addHeader("IMAGE_CACHED",  (s_prev_filename[0] != '\0') ? "true" : "false");

    int code = http.GET();
    if (code != 200) {
        ESP_LOGW(TAG, "API HTTP %d", code);
        http.end();
        return false;
    }

    String body = http.getString();
    http.end();

    const char *js = body.c_str();
    int v = -1;
    jint(js, "status", &v);          r.status = v;
    v = TRMNL_DEFAULT_REFRESH_SEC;
    jint(js, "refresh_rate", &v);    r.refresh_rate = (uint32_t)v;
    v = TRMNL_IMAGE_DOWNLOAD_TIMEOUT_MS;
    jint(js, "image_url_timeout", &v); r.img_timeout_ms = (uint32_t)(v * 1000);

    r.update_firmware = jbool(js, "update_firmware");
    r.reset_firmware  = jbool(js, "reset_firmware");
    r.max_compat      = jbool(js, "maximum_compatibility");

    jstr(js, "image_url",        r.image_url,    sizeof(r.image_url));
    jstr(js, "filename",         r.filename,     sizeof(r.filename));
    jstr(js, "firmware_url",     r.firmware_url, sizeof(r.firmware_url));
    jstr(js, "special_function", r.special_fn,   sizeof(r.special_fn));

    ESP_LOGI(TAG, "API status=%d rate=%lu fn='%s' url=%.60s",
             r.status, (unsigned long)r.refresh_rate, r.special_fn, r.image_url);
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Image download
// ─────────────────────────────────────────────────────────────────────────────

static bool download_image(const char *url, uint32_t timeout_ms)
{
    WiFiClientSecure sec;
    sec.setInsecure();
    HTTPClient http;
    if (!http.begin(sec, url)) return false;
    http.setTimeout((int)timeout_ms);

    int code = http.GET();
    if (code != 200) {
        ESP_LOGW(TAG, "Image HTTP %d", code);
        http.end();
        return false;
    }

    int clen = http.getSize();
    if (clen <= 0 || clen > TRMNL_MAX_IMAGE_BYTES) {
        ESP_LOGE(TAG, "Bad content-length %d", clen);
        http.end();
        return false;
    }

    if (s_img_buf) { heap_caps_free(s_img_buf); s_img_buf = nullptr; }
    s_img_buf = (uint8_t *)heap_caps_malloc((size_t)clen, MALLOC_CAP_SPIRAM);
    if (!s_img_buf) {
        ESP_LOGE(TAG, "PSRAM alloc %d B failed", clen);
        http.end();
        return false;
    }
    s_img_buf_len = 0;

    WiFiClient *stream = http.getStreamPtr();
    unsigned long deadline = millis() + timeout_ms;
    while (http.connected() && s_img_buf_len < (size_t)clen && millis() < deadline) {
        size_t avail = stream->available();
        if (avail == 0) { delay(1); continue; }
        size_t chunk = min(avail, (size_t)(clen - s_img_buf_len));
        stream->readBytes(&s_img_buf[s_img_buf_len], chunk);
        s_img_buf_len += chunk;
    }
    http.end();

    if (s_img_buf_len != (size_t)clen) {
        ESP_LOGE(TAG, "Short read %zu/%d", s_img_buf_len, clen);
        heap_caps_free(s_img_buf); s_img_buf = nullptr;
        return false;
    }
    ESP_LOGI(TAG, "Downloaded %zu B", s_img_buf_len);
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// OTA firmware update
// ─────────────────────────────────────────────────────────────────────────────

static void perform_ota(const char *url)
{
    ESP_LOGI(TAG, "OTA from %s", url);
    WiFiClientSecure sec;
    sec.setInsecure();
    HTTPClient http;
    if (!http.begin(sec, url)) { ESP_LOGE(TAG, "OTA begin failed"); return; }
    http.setTimeout(60000);

    int code = http.GET();
    int clen = http.getSize();
    if (code != 200 || clen <= 0) {
        ESP_LOGE(TAG, "OTA HTTP %d len %d", code, clen);
        http.end(); return;
    }

    if (!Update.begin((size_t)clen)) {
        ESP_LOGE(TAG, "Update.begin failed: %s", Update.errorString());
        http.end(); return;
    }

    WiFiClient *stream = http.getStreamPtr();
    size_t written = Update.writeStream(*stream);
    http.end();

    if (!Update.end() || !Update.isFinished()) {
        ESP_LOGE(TAG, "OTA write failed after %zu B: %s", written, Update.errorString());
        return;
    }
    ESP_LOGI(TAG, "OTA complete (%zu B), restarting", written);
    esp_restart();
}

// ─────────────────────────────────────────────────────────────────────────────
// Special functions
// ─────────────────────────────────────────────────────────────────────────────

// Returns true when the caller should skip the normal image render and go straight to sleep.
static bool handle_special(const ApiResp &api)
{
    if (api.special_fn[0] == '\0') return false;

    if (strcmp(api.special_fn, "sleep") == 0) {
        ESP_LOGI(TAG, "special: sleep");
        return true;  // sleep for the server-given refresh_rate
    }

    if (strcmp(api.special_fn, "identify") == 0) {
        ESP_LOGI(TAG, "special: identify – flashing display");
        // Brief visual flash so the user can locate this device
        epd_hl_set_all_white(&s_hl);
        epd_poweron();
        epd_hl_update_screen(&s_hl, MODE_DU, epd_ambient_temperature());
        epd_poweroff();
        delay(300);
        epd_poweron();
        epd_hl_update_screen(&s_hl, MODE_DU, epd_ambient_temperature());
        epd_poweroff();
        return false;  // continue with normal render after identify
    }

    ESP_LOGI(TAG, "special function '%s' not implemented", api.special_fn);
    return false;
}

// ─────────────────────────────────────────────────────────────────────────────
// Main entry point
// ─────────────────────────────────────────────────────────────────────────────

void setup()
{
    Serial.begin(115200);
    ESP_LOGI(TAG, "Boot  retry=%d du=%d gl=%d prev='%s'",
             s_retry_count, s_du_counter, s_gl_counter, s_prev_filename);

    display_init();

    // ── 1. WiFi ───────────────────────────────────────────────────────────────
    if (!wifi_connect()) {
        fail_and_sleep("WiFi failed");
        return;
    }

    // ── 2. Fetch API metadata ─────────────────────────────────────────────────
    ApiResp api;
    if (!fetch_api(api)) {
        fail_and_sleep("API request failed");
        return;
    }

    // ── 3. Handle server-level status codes ───────────────────────────────────
    if (api.reset_firmware) {
        // Server requests credential wipe.  Clear RTC state and restart.
        ESP_LOGW(TAG, "Server requested reset");
        s_retry_count = 0;
        s_du_counter  = 0;
        s_gl_counter  = 0;
        s_prev_filename[0] = '\0';
        esp_restart();
        return;
    }

    if (api.status == 202) {
        // Device not yet registered – show last known content (or blank) and poll slowly
        ESP_LOGI(TAG, "Not registered – sleeping %d s", TRMNL_UNREGISTERED_SLEEP_SEC);
        s_retry_count = 0;
        sleep_for(TRMNL_UNREGISTERED_SLEEP_SEC);
        return;
    }

    if (api.status != 0) {
        fail_and_sleep("API non-zero status");
        return;
    }

    // ── 4. OTA update ─────────────────────────────────────────────────────────
    if (api.update_firmware && api.firmware_url[0] != '\0') {
        perform_ota(api.firmware_url);
        // If OTA fails we continue with normal render cycle rather than crashing.
    }

    // ── 5. Special functions ──────────────────────────────────────────────────
    if (handle_special(api)) {
        s_retry_count = 0;
        sleep_for(api.refresh_rate);
        return;
    }

    // ── 6. Content-unchanged check (RTC cache) ────────────────────────────────
    if (api.filename[0] != '\0' && strcmp(api.filename, s_prev_filename) == 0) {
        ESP_LOGI(TAG, "Content unchanged, skipping render");
        s_retry_count = 0;
        sleep_for(api.refresh_rate);
        return;
    }

    // ── 7. Try LittleFS image cache before downloading ────────────────────────
    bool from_cache = false;
    if (api.filename[0] != '\0') {
        from_cache = cache_load(api.filename, &s_img_buf, &s_img_buf_len);
    }

    // ── 8. Download image into PSRAM (only if not in cache) ───────────────────
    if (!from_cache) {
        if (api.image_url[0] == '\0') {
            fail_and_sleep("No image_url in response");
            return;
        }
        if (!download_image(api.image_url, api.img_timeout_ms)) {
            fail_and_sleep("Image download failed");
            return;
        }
        // Persist to LittleFS so the next boot can skip re-downloading
        if (api.filename[0] != '\0') {
            cache_save(api.filename, s_img_buf, s_img_buf_len);
        }
    }

    // ── 9. Decode into framebuffer (display is NOT touched yet) ───────────────
    bool decoded = decode_image(s_img_buf, s_img_buf_len);
    heap_caps_free(s_img_buf);
    s_img_buf = nullptr;

    if (!decoded) {
        fail_and_sleep("Image decode failed");
        return;
    }

    // ── 10. ONE single EPD refresh with the new content ───────────────────────
    display_update_once(api.refresh_rate, api.max_compat);

    // ── 11. Update state and sleep ────────────────────────────────────────────
    strlcpy(s_prev_filename, api.filename, sizeof(s_prev_filename));
    s_retry_count = 0;
    ESP_LOGI(TAG, "Done – sleeping %lu s", (unsigned long)api.refresh_rate);
    sleep_for(api.refresh_rate);
}

void loop()
{
    // Never reached: device deep-sleeps in setup() each cycle.
}
