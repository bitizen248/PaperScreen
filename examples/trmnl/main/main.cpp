/*
 * TRMNL client for LilyGO T5 E-Paper S3 Pro (ED047TC1, 960×540)
 *
 * Fetch-render cycle (fixes two common bugs):
 *
 *  Bug 1 – double render (old → new flash):
 *    The EPD is never touched until the full image is downloaded and decoded
 *    into the framebuffer.  One single refresh happens at the very end.
 *
 *  Bug 2 – server-error screen stuck forever:
 *    On any failure the device goes straight to deep sleep with an
 *    exponential retry backoff.  Previous content stays on screen (e-ink
 *    retains the image without power) and the device retries automatically.
 */

#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_sleep.h>

extern "C" {
#include "epdiy.h"
#include "epd_highlevel.h"
}

#include "JPEGDEC.h"
#include "settings.h"

// ── Panel / waveform ──────────────────────────────────────────────────────────
#define DEMO_BOARD epd_board_v7
#define WAVEFORM   EPD_BUILTIN_WAVEFORM

// Declared in epdiy's epd_display.h / displays.c
extern const EpdDisplay_t ED047TC1;

static EpdiyHighlevelState hl;

// ── State persisted across deep-sleep cycles ──────────────────────────────────
RTC_DATA_ATTR static int     s_retry_count    = 0;
RTC_DATA_ATTR static char    s_prev_filename[128] = "";
RTC_DATA_ATTR static uint32_t s_next_sleep_sec = TRMNL_DEFAULT_REFRESH_SEC;

// ── Image download buffer (PSRAM) ─────────────────────────────────────────────
static uint8_t *s_img_buf     = nullptr;
static size_t   s_img_buf_len = 0;

// ── JPEG decoder ──────────────────────────────────────────────────────────────
static JPEGDEC s_jpeg;

// ── Decodebuffer: 4bpp grayscale, (width+1)/2 bytes per row ──────────────────
// This is the intermediate buffer passed to epd_draw_rotated_image().
// Even-x pixel in low nibble, odd-x pixel in high nibble (epdiy convention).
static uint8_t *s_decodebuffer = nullptr;

static const char *TAG = "TRMNL";

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────

static uint32_t retry_sleep_seconds()
{
    switch (s_retry_count) {
        case 0:  return TRMNL_RETRY_1_SEC;
        case 1:  return TRMNL_RETRY_2_SEC;
        default: return TRMNL_RETRY_3_SEC;
    }
}

static void sleep_for(uint32_t seconds)
{
    ESP_LOGI(TAG, "Deep sleep for %lu s", (unsigned long)seconds);
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    esp_deep_sleep((uint64_t)seconds * 1000000ULL);
}

static void fail_and_sleep(const char *reason)
{
    ESP_LOGW(TAG, "Failure: %s  (retry %d)", reason, s_retry_count + 1);
    // Previous content stays on the EPD (no power needed for e-ink retention).
    s_retry_count = (s_retry_count < 3) ? s_retry_count + 1 : 3;
    sleep_for(retry_sleep_seconds());
}

// ─────────────────────────────────────────────────────────────────────────────
// Display init / render
// ─────────────────────────────────────────────────────────────────────────────

static void display_init()
{
    epd_init(&DEMO_BOARD, &ED047TC1, EPD_LUT_64K);
    epd_set_vcom(1560);               // adjust to your hardware (mV)
    hl = epd_hl_init(WAVEFORM);
    epd_set_rotation(EPD_ROT_INVERTED_PORTRAIT);

    int w = epd_rotated_display_width();
    int h = epd_rotated_display_height();
    size_t buf_bytes = ((size_t)(w + 1) / 2) * (size_t)h;
    s_decodebuffer = (uint8_t *)heap_caps_calloc(buf_bytes, 1, MALLOC_CAP_SPIRAM);
    ESP_LOGI(TAG, "EPD %dx%d, decodebuffer %zu bytes", w, h, buf_bytes);
}

// Called once, after the framebuffer is fully populated with new content.
static void display_update_once()
{
    EpdRect full_area = {
        .x      = 0,
        .y      = 0,
        .width  = epd_rotated_display_width(),
        .height = epd_rotated_display_height(),
    };

    epd_draw_rotated_image(full_area, s_decodebuffer, epd_hl_get_framebuffer(&hl));
    epd_poweron();
    epd_hl_update_screen(&hl, MODE_GL16, epd_ambient_temperature());
    epd_poweroff();
}

// ─────────────────────────────────────────────────────────────────────────────
// JPEG decode callback → writes into s_decodebuffer (4bpp, epdiy layout)
// ─────────────────────────────────────────────────────────────────────────────

static int jpeg_draw_cb(JPEGDRAW *pDraw)
{
    int ep_w  = epd_rotated_display_width();
    int pitch = (ep_w + 1) / 2;

    for (int row = 0; row < pDraw->iHeight; row++) {
        int y = pDraw->y + row;
        if (y < 0 || y >= epd_rotated_display_height()) continue;

        for (int col = 0; col < pDraw->iWidth; col++) {
            int x = pDraw->x + col;
            if (x < 0 || x >= ep_w) continue;

            // Pixel from JPEGDEC in RGB565 little-endian
            uint16_t rgb = pDraw->pPixels[row * pDraw->iWidth + col];
            uint8_t r = (rgb >> 11) & 0x1F;
            uint8_t g = (rgb >>  5) & 0x3F;
            uint8_t b =  rgb        & 0x1F;

            // Luminance (BT.601 coefficients, scaled to avoid floats)
            uint8_t luma8 = (uint8_t)(((uint32_t)r * 77  * 8 +
                                       (uint32_t)g * 75  * 4 +
                                       (uint32_t)b * 30  * 8) >> 11);
            uint8_t gray4 = luma8 >> 4;   // 0 = black, 15 = white

            uint8_t *dst = &s_decodebuffer[y * pitch + (x >> 1)];
            if (x & 1) {
                *dst = (uint8_t)((*dst & 0x0F) | (gray4 << 4));
            } else {
                *dst = (uint8_t)((*dst & 0xF0) | gray4);
            }
        }
    }
    return 1;
}

static bool decode_jpeg(uint8_t *buf, size_t len)
{
    if (!s_jpeg.openRAM(buf, (int)len, jpeg_draw_cb)) {
        ESP_LOGE(TAG, "JPEGDEC openRAM failed, err=%d", s_jpeg.getLastError());
        return false;
    }
    s_jpeg.setPixelType(RGB565_LITTLE_ENDIAN);
    bool ok = (s_jpeg.decode(0, 0, 0) == 1);
    if (!ok) {
        ESP_LOGE(TAG, "JPEGDEC decode failed, err=%d", s_jpeg.getLastError());
    }
    s_jpeg.close();
    return ok;
}

// ─────────────────────────────────────────────────────────────────────────────
// WiFi
// ─────────────────────────────────────────────────────────────────────────────

static bool wifi_connect()
{
    WiFi.mode(WIFI_STA);
    WiFi.begin(TRMNL_WIFI_SSID, TRMNL_WIFI_PASSWORD);

    unsigned long deadline = millis() + TRMNL_WIFI_TIMEOUT_MS;
    while (WiFi.status() != WL_CONNECTED && millis() < deadline) {
        delay(250);
    }
    bool ok = (WiFi.status() == WL_CONNECTED);
    if (ok) {
        ESP_LOGI(TAG, "WiFi connected, RSSI=%d", WiFi.RSSI());
    } else {
        ESP_LOGW(TAG, "WiFi connect timeout");
    }
    return ok;
}

// ─────────────────────────────────────────────────────────────────────────────
// TRMNL API – GET /api/display
// ─────────────────────────────────────────────────────────────────────────────

struct TrmnlApiResponse {
    int      status      = -1;
    uint32_t refresh_rate = TRMNL_DEFAULT_REFRESH_SEC;
    char     image_url[512]  = {};
    char     filename[128]   = {};
};

// Minimal JSON field extractor – avoids a JSON library dependency.
// Finds the first occurrence of "key": <value> and copies value into out.
static bool json_str(const char *json, const char *key, char *out, size_t out_sz)
{
    // Build the pattern: "key":"
    char pattern[128];
    snprintf(pattern, sizeof(pattern), "\"%s\":", key);
    const char *p = strstr(json, pattern);
    if (!p) return false;
    p += strlen(pattern);
    while (*p == ' ') p++;
    if (*p != '"') return false;
    p++;
    size_t i = 0;
    while (*p && *p != '"' && i < out_sz - 1) {
        out[i++] = *p++;
    }
    out[i] = '\0';
    return (i > 0);
}

static bool json_int(const char *json, const char *key, int *out)
{
    char pattern[128];
    snprintf(pattern, sizeof(pattern), "\"%s\":", key);
    const char *p = strstr(json, pattern);
    if (!p) return false;
    p += strlen(pattern);
    while (*p == ' ') p++;
    *out = atoi(p);
    return true;
}

static bool fetch_api(TrmnlApiResponse &resp)
{
    WiFiClientSecure secure;
    secure.setInsecure();   // replace with CA cert for production

    HTTPClient http;
    String url = String(TRMNL_SERVER) + TRMNL_API_ENDPOINT;
    if (!http.begin(secure, url)) {
        ESP_LOGE(TAG, "http.begin failed");
        return false;
    }

    // Required device identification headers
    uint8_t mac[6];
    WiFi.macAddress(mac);
    char mac_str[18];
    snprintf(mac_str, sizeof(mac_str), "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    http.addHeader("ID",           mac_str);
    http.addHeader("access-token", TRMNL_API_KEY);
    http.addHeader("FW_VERSION",   TRMNL_FW_VERSION);
    http.addHeader("RSSI",         String(WiFi.RSSI()));
    http.addHeader("WIDTH",        String(epd_rotated_display_width()));
    http.addHeader("HEIGHT",       String(epd_rotated_display_height()));
    http.setTimeout(15000);

    int code = http.GET();
    if (code != 200) {
        ESP_LOGW(TAG, "API HTTP %d", code);
        http.end();
        return false;
    }

    String body = http.getString();
    http.end();

    const char *js = body.c_str();

    int status_val = -1;
    json_int(js, "status", &status_val);
    resp.status = status_val;

    int rate_val = TRMNL_DEFAULT_REFRESH_SEC;
    json_int(js, "refresh_rate", &rate_val);
    resp.refresh_rate = (uint32_t)rate_val;

    json_str(js, "image_url", resp.image_url, sizeof(resp.image_url));
    json_str(js, "filename",  resp.filename,  sizeof(resp.filename));

    ESP_LOGI(TAG, "API status=%d rate=%lu url=%.80s",
             resp.status, (unsigned long)resp.refresh_rate, resp.image_url);

    return (resp.status == 0 && resp.image_url[0] != '\0');
}

// ─────────────────────────────────────────────────────────────────────────────
// Image download into PSRAM
// ─────────────────────────────────────────────────────────────────────────────

static bool download_image(const char *url)
{
    WiFiClientSecure secure;
    secure.setInsecure();

    HTTPClient http;
    if (!http.begin(secure, url)) {
        ESP_LOGE(TAG, "http.begin(image) failed");
        return false;
    }
    http.setTimeout(30000);

    int code = http.GET();
    if (code != 200) {
        ESP_LOGW(TAG, "Image HTTP %d", code);
        http.end();
        return false;
    }

    int content_len = http.getSize();
    if (content_len <= 0 || content_len > TRMNL_MAX_IMAGE_BYTES) {
        ESP_LOGE(TAG, "Bad content-length: %d", content_len);
        http.end();
        return false;
    }

    if (s_img_buf) {
        heap_caps_free(s_img_buf);
        s_img_buf = nullptr;
    }
    s_img_buf = (uint8_t *)heap_caps_malloc((size_t)content_len, MALLOC_CAP_SPIRAM);
    if (!s_img_buf) {
        ESP_LOGE(TAG, "PSRAM alloc %d B failed", content_len);
        http.end();
        return false;
    }
    s_img_buf_len = 0;

    WiFiClient *stream = http.getStreamPtr();
    unsigned long deadline = millis() + 30000;
    while (http.connected() && s_img_buf_len < (size_t)content_len && millis() < deadline) {
        size_t avail = stream->available();
        if (avail == 0) {
            delay(1);
            continue;
        }
        size_t chunk = avail < (size_t)(content_len - s_img_buf_len)
                       ? avail : (size_t)(content_len - s_img_buf_len);
        stream->readBytes(&s_img_buf[s_img_buf_len], chunk);
        s_img_buf_len += chunk;
    }
    http.end();

    if (s_img_buf_len != (size_t)content_len) {
        ESP_LOGE(TAG, "Download incomplete: got %zu / %d", s_img_buf_len, content_len);
        heap_caps_free(s_img_buf);
        s_img_buf = nullptr;
        return false;
    }

    ESP_LOGI(TAG, "Downloaded %zu bytes", s_img_buf_len);
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Entry point
// ─────────────────────────────────────────────────────────────────────────────

void setup()
{
    Serial.begin(115200);
    ESP_LOGI(TAG, "Wake-up, retry_count=%d", s_retry_count);

    display_init();

    // ── 1. WiFi ───────────────────────────────────────────────────────────────
    if (!wifi_connect()) {
        fail_and_sleep("WiFi connect failed");
        return;
    }

    // ── 2. Fetch API metadata ─────────────────────────────────────────────────
    TrmnlApiResponse api;
    if (!fetch_api(api)) {
        fail_and_sleep("API request failed");
        return;
    }

    // ── 3. Cache check: skip download+render if content unchanged ─────────────
    if (api.filename[0] != '\0' && strcmp(api.filename, s_prev_filename) == 0) {
        ESP_LOGI(TAG, "Content unchanged (%s), skipping render", api.filename);
        s_retry_count = 0;
        sleep_for(api.refresh_rate);
        return;
    }

    // ── 4. Download image into PSRAM buffer (display NOT touched yet) ──────────
    if (!download_image(api.image_url)) {
        fail_and_sleep("Image download failed");
        return;
    }

    // ── 5. Decode image into decodebuffer (still no EPD activity) ─────────────
    //   Clear the decode buffer to white before writing new pixels.
    {
        int w = epd_rotated_display_width();
        int h = epd_rotated_display_height();
        memset(s_decodebuffer, 0xFF, ((size_t)(w + 1) / 2) * (size_t)h);
    }

    bool decoded = decode_jpeg(s_img_buf, s_img_buf_len);
    heap_caps_free(s_img_buf);
    s_img_buf = nullptr;

    if (!decoded) {
        fail_and_sleep("JPEG decode failed");
        return;
    }

    // ── 6. Single EPD refresh with new content ────────────────────────────────
    //   This is the ONLY point where the physical display changes.
    display_update_once();

    // ── 7. Update persistent state and sleep until next refresh ───────────────
    strlcpy(s_prev_filename, api.filename, sizeof(s_prev_filename));
    s_retry_count = 0;
    s_next_sleep_sec = api.refresh_rate;

    ESP_LOGI(TAG, "OK – sleeping %lu s", (unsigned long)api.refresh_rate);
    sleep_for(api.refresh_rate);
}

void loop()
{
    // Never reached: the device deep-sleeps in setup() and wakes fresh each cycle.
}
