#include "trmnl_service.h"

#include <Arduino.h>
#include <cstring>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <esp_heap_caps.h>

#include "display/image_renderer.h"
#include "../../lib/epdiy/examples/weather/main/ArduinoJson.h"

namespace {

constexpr char kDefaultBaseUrl[] = "https://trmnl.com";
constexpr char kFirmwareVersion[] = "PaperScreen-0.1.0";
constexpr uint32_t kMinimumRefreshSeconds = 300;
constexpr size_t kMaxImageBytes = 1024 * 1024;
const char* kCollectedHeaders[] = {
    "etag",
    "ETag",
};

bool is_png_payload(const uint8_t* data, size_t size)
{
    static const uint8_t kPngSignature[8] = {
        0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A
    };
    return data != nullptr && size >= sizeof(kPngSignature) &&
           std::memcmp(data, kPngSignature, sizeof(kPngSignature)) == 0;
}

bool is_jpeg_payload(const uint8_t* data, size_t size)
{
    return data != nullptr && size >= 2 && data[0] == 0xFF && data[1] == 0xD8;
}

bool is_bmp_payload(const uint8_t* data, size_t size)
{
    return data != nullptr && size >= 2 && data[0] == 'B' && data[1] == 'M';
}

void log_image_head(const uint8_t* data, size_t size)
{
    if (data == nullptr || size == 0) {
        Serial.println("[trmnl] image buffer empty");
        return;
    }

    Serial.printf(
        "[trmnl] image head=%02X %02X %02X %02X %02X %02X %02X %02X size=%u\n",
        size > 0 ? data[0] : 0, size > 1 ? data[1] : 0, size > 2 ? data[2] : 0, size > 3 ? data[3] : 0,
        size > 4 ? data[4] : 0, size > 5 ? data[5] : 0, size > 6 ? data[6] : 0, size > 7 ? data[7] : 0,
        static_cast<unsigned>(size));
}

const char* payload_format_name(const uint8_t* data, size_t size)
{
    if (is_png_payload(data, size)) return "png";
    if (is_jpeg_payload(data, size)) return "jpeg";
    if (is_bmp_payload(data, size)) return "bmp";
    return "unknown";
}

uint32_t fnv1a_hash(const char* text)
{
    uint32_t hash = 2166136261UL;
    const unsigned char* cursor = reinterpret_cast<const unsigned char*>(text == nullptr ? "" : text);
    while (*cursor != '\0') {
        hash ^= *cursor++;
        hash *= 16777619UL;
    }
    return hash == 0 ? 1 : hash;
}

void copy_text(char* destination, size_t destination_size, const char* source)
{
    if (destination_size == 0) {
        return;
    }

    std::strncpy(destination, source == nullptr ? "" : source, destination_size - 1);
    destination[destination_size - 1] = '\0';
}

const char* redacted_url_label(const char* url, char* out, size_t out_size)
{
    if (out == nullptr || out_size == 0) {
        return "";
    }

    std::snprintf(out, out_size, "hash=%lu", static_cast<unsigned long>(fnv1a_hash(url)));
    return out;
}

void copy_etag(char* destination, size_t destination_size, const String& source)
{
    if (destination_size == 0) {
        return;
    }

    size_t written = 0;
    for (size_t i = 0; i < source.length() && written + 1 < destination_size; ++i) {
        const char value = source.charAt(i);
        if (value == '"' || value == '\\') {
            continue;
        }
        destination[written++] = value;
    }
    destination[written] = '\0';
}

void copy_collected_etag(HTTPClient& http, char* destination, size_t destination_size)
{
    const String lower = http.header("etag");
    if (lower.length() > 0) {
        copy_etag(destination, destination_size, lower);
        return;
    }

    const String upper = http.header("ETag");
    if (upper.length() > 0) {
        copy_etag(destination, destination_size, upper);
        return;
    }

    if (destination_size > 0) {
        destination[0] = '\0';
    }
}

const char* base_url_for(const paper_screen::TrmnlSettings& settings)
{
    return settings.base_url != nullptr && settings.base_url[0] != '\0'
        ? settings.base_url
        : kDefaultBaseUrl;
}

String endpoint_for(const paper_screen::TrmnlSettings& settings)
{
    if (settings.force_current_screen) {
        return String(base_url_for(settings)) + "/api/current_screen";
    }
    if (settings.special_function
        || settings.force_playlist_advance
        || settings.mode == paper_screen::TrmnlMode::Playlist) {
        return String(base_url_for(settings)) + "/api/display";
    }
    return String(base_url_for(settings)) + "/api/current_screen";
}

bool begin_insecure_request(
    HTTPClient& http,
    WiFiClient& plain_client,
    WiFiClientSecure& secure_client,
    const char* url,
    bool allow_insecure_https
)
{
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);

    if (std::strncmp(url, "http://", 7) == 0) {
        return http.begin(plain_client, url);
    }

    if (std::strncmp(url, "https://", 8) == 0) {
        if (allow_insecure_https) {
            secure_client.setInsecure();
        }
        return http.begin(secure_client, url);
    }

    return false;
}

uint32_t normalized_refresh_seconds(uint32_t parsed, uint32_t fallback)
{
    if (parsed < kMinimumRefreshSeconds) {
        return fallback < kMinimumRefreshSeconds ? kMinimumRefreshSeconds : fallback;
    }
    return parsed;
}

uint32_t json_refresh_seconds(JsonVariant value, uint32_t fallback)
{
    if (value.is<uint32_t>()) {
        return normalized_refresh_seconds(value.as<uint32_t>(), fallback);
    }

    if (value.is<const char*>()) {
        const char* text = value.as<const char*>();
        char* end = nullptr;
        const unsigned long parsed = std::strtoul(text == nullptr ? "" : text, &end, 10);
        if (end != text) {
            return normalized_refresh_seconds(static_cast<uint32_t>(parsed), fallback);
        }
    }

    return normalized_refresh_seconds(0, fallback);
}

size_t previous_image_count(const paper_screen::TrmnlSettings& settings)
{
    return settings.previous_image_count > paper_screen::kTrmnlPreviousImageCapacity
        ? paper_screen::kTrmnlPreviousImageCapacity
        : settings.previous_image_count;
}

bool text_matches(const char* lhs, const char* rhs)
{
    return lhs != nullptr
        && lhs[0] != '\0'
        && rhs != nullptr
        && rhs[0] != '\0'
        && std::strcmp(lhs, rhs) == 0;
}

bool cached_image_matches_metadata(const paper_screen::TrmnlSettings& settings,
                                   const paper_screen::TrmnlSnapshot& snapshot,
                                   const char** reason)
{
    if (settings.previous_image_url_hash != 0
        && settings.previous_image_url_hash == snapshot.image_url_hash) {
        if (reason != nullptr) *reason = "url-hash";
        return true;
    }

    const size_t count = previous_image_count(settings);
    for (size_t i = 0; i < count; ++i) {
        if (settings.previous_image_url_hashes[i] != 0
            && settings.previous_image_url_hashes[i] == snapshot.image_url_hash) {
            if (reason != nullptr) *reason = "url-hash";
            return true;
        }
    }

    return false;
}

bool cached_image_matches_image_etag(const paper_screen::TrmnlSettings& settings,
                                     const char* image_etag)
{
    if (text_matches(settings.previous_image_etag, image_etag)) {
        return true;
    }

    const size_t count = previous_image_count(settings);
    for (size_t i = 0; i < count; ++i) {
        if (text_matches(settings.previous_image_etags[i], image_etag)) {
            return true;
        }
    }
    return false;
}

bool snapshot_matches_metadata(const paper_screen::TrmnlSnapshot& cached,
                               const paper_screen::TrmnlSnapshot& probe,
                               const char** reason)
{
    if (cached.image_url_hash != 0 && cached.image_url_hash == probe.image_url_hash) {
        if (reason != nullptr) *reason = "url-hash";
        return true;
    }
    return false;
}

void add_trmnl_headers(HTTPClient& http, const paper_screen::TrmnlSettings& settings, const String& device_id)
{
    char value[24] = {};

    http.addHeader("Access-Token", settings.api_key);
    http.addHeader("ID", device_id);
    http.addHeader("User-Agent", kFirmwareVersion);
    http.addHeader("FW-Version", kFirmwareVersion);
    if (settings.special_function) {
        http.addHeader("special_function", "true");
    }

    std::snprintf(value, sizeof(value), "%lu", static_cast<unsigned long>(settings.current_refresh_seconds));
    http.addHeader("Refresh-Rate", value);

    std::snprintf(value, sizeof(value), "%u", static_cast<unsigned>(settings.display_width));
    http.addHeader("Width", value);

    std::snprintf(value, sizeof(value), "%u", static_cast<unsigned>(settings.display_height));
    http.addHeader("Height", value);

    std::snprintf(value, sizeof(value), "%ld", static_cast<long>(settings.wifi_rssi));
    http.addHeader("RSSI", value);

    if (settings.battery_voltage_mv > 0) {
        std::snprintf(value, sizeof(value), "%u.%03u",
                      static_cast<unsigned>(settings.battery_voltage_mv / 1000U),
                      static_cast<unsigned>(settings.battery_voltage_mv % 1000U));
        http.addHeader("Battery-Voltage", value);
    }
}

bool parse_metadata_response(const String& body, uint32_t fallback_refresh_seconds, paper_screen::TrmnlDisplayResponse* response)
{
    DynamicJsonDocument doc(4096);
    const DeserializationError error = deserializeJson(doc, body);
    if (error) {
        Serial.printf("[trmnl] metadata json error=%s\n", error.c_str());
        return false;
    }

    response->status = doc["status"] | 0;

    const char* error_text = doc["error"] | "";
    copy_text(response->error, sizeof(response->error), error_text);

    const char* image_url = doc["image_url"] | "";
    if (image_url[0] == '\0') {
        return false;
    }
    copy_text(response->image_url, sizeof(response->image_url), image_url);
    if (std::strlen(image_url) >= sizeof(response->image_url)) {
        Serial.printf("[trmnl] metadata image url truncated original=%u capacity=%u\n",
                      static_cast<unsigned>(std::strlen(image_url)),
                      static_cast<unsigned>(sizeof(response->image_url)));
        return false;
    }

    const char* image_name = doc["image_name"] | doc["filename"] | "";
    copy_text(response->image_name, sizeof(response->image_name), image_name);
    response->refresh_seconds = json_refresh_seconds(doc["refresh_rate"], fallback_refresh_seconds);

    response->reset_firmware  = doc["reset_firmware"]  | false;
    response->maximum_compatibility = doc["maximum_compatibility"] | false;
    const char* special_fn = doc["special_function"] | "";
    copy_text(response->special_fn, sizeof(response->special_fn), special_fn);
    return true;
}

}  // namespace

namespace paper_screen {

TrmnlService::~TrmnlService()
{
    clear_image();
}

const TrmnlSnapshot& TrmnlService::snapshot() const
{
    return snapshot_;
}

const TrmnlSnapshot& TrmnlService::fetch(const TrmnlSettings& settings, WifiService& wifi)
{
    const TrmnlSnapshot previous_memory_snapshot = snapshot_;
    const bool previous_memory_image_available = image_data_ != nullptr && image_size_ > 0;
    snapshot_ = {};

    if (!settings.enabled) {
        clear_image();
        set_status(TrmnlFetchStatus::Disabled);
        return snapshot_;
    }

    if (settings.api_key == nullptr || settings.api_key[0] == '\0') {
        clear_image();
        set_status(TrmnlFetchStatus::MissingApiKey);
        return snapshot_;
    }

    set_status(TrmnlFetchStatus::ConnectingWifi);
    if (!wifi.connect()) {
        set_status(TrmnlFetchStatus::Offline);
        return snapshot_;
    }

    if (!request_metadata(settings)) {
        if (settings.disconnect_wifi_after_fetch) {
            wifi.disconnect();
        }
        return snapshot_;
    }

    snapshot_.image_url_hash = fnv1a_hash(snapshot_.response.image_url);

    const char* cache_match_reason = "";
    const bool unchanged_by_cached_identity = cached_image_matches_metadata(settings, snapshot_, &cache_match_reason);
    if (settings.skip_unchanged_image && unchanged_by_cached_identity) {
        const char* memory_match_reason = "";
        if (previous_memory_image_available
            && snapshot_matches_metadata(previous_memory_snapshot, snapshot_, &memory_match_reason)) {
            Serial.printf("[trmnl] cache hit: %s; reusing memory image\n", memory_match_reason);
            snapshot_.image_bytes = static_cast<uint32_t>(image_size_);
        } else if (settings.cached_image_available) {
            Serial.printf("[trmnl] cache hit: %s; skipping download for stored cache\n", cache_match_reason);
            snapshot_.image_bytes = 0;
        } else {
            Serial.printf("[trmnl] cache hit: %s but no reusable image is available\n", cache_match_reason);
            clear_image();
            if (!download_image(settings)) {
                if (settings.disconnect_wifi_after_fetch) {
                    wifi.disconnect();
                }
                return snapshot_;
            }
            if (settings.disconnect_wifi_after_fetch) {
                wifi.disconnect();
            }
            set_status(TrmnlFetchStatus::Ready);
            return snapshot_;
        }
        if (settings.disconnect_wifi_after_fetch) {
            wifi.disconnect();
        }
        set_status(TrmnlFetchStatus::Ready);
        return snapshot_;
    }

    clear_image();
    if (!download_image(settings)) {
        if (settings.disconnect_wifi_after_fetch) {
            wifi.disconnect();
        }
        return snapshot_;
    }

    if (settings.disconnect_wifi_after_fetch) {
        wifi.disconnect();
    }

    set_status(TrmnlFetchStatus::Ready);
    return snapshot_;
}

const uint8_t* TrmnlService::image_data() const
{
    return image_data_;
}

size_t TrmnlService::image_size() const
{
    return image_size_;
}

void TrmnlService::clear_image()
{
    if (image_data_ != nullptr) {
        heap_caps_free(image_data_);
        image_data_ = nullptr;
    }
    image_size_ = 0;
}

bool TrmnlService::request_metadata(const TrmnlSettings& settings)
{
    set_status(TrmnlFetchStatus::RequestingMetadata);

    WiFiClient plain_client;
    WiFiClientSecure secure_client;
    HTTPClient http;
    const String url = endpoint_for(settings);
    const String device_id = WiFi.macAddress();
    Serial.printf("[trmnl] endpoint=%s\n", url.c_str());
    Serial.printf("[trmnl] device id=%s\n", device_id.c_str());
    if (!begin_insecure_request(http, plain_client, secure_client, url.c_str(), settings.allow_insecure_https)) {
        set_status(TrmnlFetchStatus::ServerError);
        return false;
    }

    http.collectHeaders(kCollectedHeaders, sizeof(kCollectedHeaders) / sizeof(kCollectedHeaders[0]));
    add_trmnl_headers(http, settings, device_id);

    const int http_code = http.GET();
    Serial.printf("[trmnl] metadata http=%d\n", http_code);
    copy_collected_etag(http,
                        snapshot_.response.metadata_etag,
                        sizeof(snapshot_.response.metadata_etag));
    if (snapshot_.response.metadata_etag[0] != '\0') {
        Serial.printf("[trmnl] metadata etag=%s\n", snapshot_.response.metadata_etag);
    }
    const String body = http.getString();
    if (http_code == HTTP_CODE_UNAUTHORIZED || http_code == HTTP_CODE_FORBIDDEN) {
        Serial.printf("[trmnl] metadata body=%s\n", body.c_str());
        http.end();
        set_status(TrmnlFetchStatus::Unauthorized);
        return false;
    }
    if (http_code < 200 || http_code >= 300) {
        Serial.printf("[trmnl] metadata body=%s\n", body.c_str());
        http.end();
        set_status(TrmnlFetchStatus::ServerError);
        return false;
    }

    http.end();

    if (!parse_metadata_response(body, settings.fallback_refresh_seconds, &snapshot_.response)) {
        Serial.printf("[trmnl] metadata no image status=%d error=%s\n",
                      snapshot_.response.status,
                      snapshot_.response.error);
        if (std::strcmp(snapshot_.response.error, "Device not found") == 0) {
            set_status(TrmnlFetchStatus::DeviceNotFound);
        } else {
            set_status(TrmnlFetchStatus::NoContent);
        }
        return false;
    }

    Serial.printf("[trmnl] metadata ok refresh=%lu image=%s\n",
                  static_cast<unsigned long>(snapshot_.response.refresh_seconds),
                  snapshot_.response.image_name);
    return true;
}

bool TrmnlService::download_image(const TrmnlSettings& settings)
{
    set_status(TrmnlFetchStatus::DownloadingImage);

    WiFiClient plain_client;
    WiFiClientSecure secure_client;
    HTTPClient http;
    if (!begin_insecure_request(http, plain_client, secure_client, snapshot_.response.image_url, settings.allow_insecure_https)) {
        char label[24] = {};
        Serial.printf("[trmnl] image begin failed url_%s\n",
                      redacted_url_label(snapshot_.response.image_url, label, sizeof(label)));
        set_status(TrmnlFetchStatus::ServerError);
        return false;
    }

    http.collectHeaders(kCollectedHeaders, sizeof(kCollectedHeaders) / sizeof(kCollectedHeaders[0]));
    const String device_id = WiFi.macAddress();
    add_trmnl_headers(http, settings, device_id);

    const int http_code = http.GET();
    copy_collected_etag(http,
                        snapshot_.response.image_etag,
                        sizeof(snapshot_.response.image_etag));
    if (snapshot_.response.image_etag[0] != '\0') {
        Serial.printf("[trmnl] image etag=%s\n", snapshot_.response.image_etag);
    }
    if (http_code < 200 || http_code >= 300) {
        Serial.printf("[trmnl] image http=%d\n", http_code);
        const String body = http.getString();
        if (body.length() > 0) Serial.printf("[trmnl] image error body=%s\n", body.c_str());
        http.end();
        set_status(TrmnlFetchStatus::ServerError);
        return false;
    }

    if (settings.skip_unchanged_image
        && settings.cached_image_available
        && snapshot_.response.image_etag[0] != '\0'
        && cached_image_matches_image_etag(settings, snapshot_.response.image_etag)) {
        Serial.println("[trmnl] cache hit: image-etag; skipping image body for stored cache");
        http.end();
        snapshot_.image_bytes = 0;
        return true;
    }

    const int content_length = http.getSize();
    size_t capacity = content_length > 0 ? static_cast<size_t>(content_length) : 64 * 1024;
    if (capacity == 0 || capacity > kMaxImageBytes) {
        http.end();
        set_status(TrmnlFetchStatus::ServerError);
        return false;
    }

    image_data_ = static_cast<uint8_t*>(heap_caps_malloc(capacity, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (image_data_ == nullptr) {
        image_data_ = static_cast<uint8_t*>(heap_caps_malloc(capacity, MALLOC_CAP_8BIT));
    }
    if (image_data_ == nullptr) {
        http.end();
        set_status(TrmnlFetchStatus::ServerError);
        return false;
    }

    WiFiClient* stream = http.getStreamPtr();
    while (http.connected()) {
        const size_t available = stream->available();
        if (available == 0) {
            if (content_length > 0 && image_size_ >= static_cast<size_t>(content_length)) {
                break;
            }
            delay(10);
            continue;
        }

        if (image_size_ + available > capacity) {
            size_t next_capacity = capacity * 2;
            while (next_capacity < image_size_ + available) {
                next_capacity *= 2;
            }
            if (next_capacity > kMaxImageBytes) {
                clear_image();
                http.end();
                set_status(TrmnlFetchStatus::ServerError);
                return false;
            }
            uint8_t* next = static_cast<uint8_t*>(heap_caps_realloc(image_data_, next_capacity, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
            if (next == nullptr) {
                next = static_cast<uint8_t*>(heap_caps_realloc(image_data_, next_capacity, MALLOC_CAP_8BIT));
            }
            if (next == nullptr) {
                clear_image();
                http.end();
                set_status(TrmnlFetchStatus::ServerError);
                return false;
            }
            image_data_ = next;
            capacity = next_capacity;
        }

        const int read = stream->readBytes(image_data_ + image_size_, available);
        if (read <= 0) {
            break;
        }
        image_size_ += static_cast<size_t>(read);
    }

    http.end();

    if (image_size_ == 0) {
        set_status(TrmnlFetchStatus::NoContent);
        return false;
    }

    log_image_head(image_data_, image_size_);
    char label[24] = {};
    Serial.printf("[trmnl] image format=%s url_%s\n",
                  payload_format_name(image_data_, image_size_),
                  redacted_url_label(snapshot_.response.image_url, label, sizeof(label)));

    snapshot_.image_bytes = static_cast<uint32_t>(image_size_);
    snapshot_.image_visual_hash = paper_screen::png_visual_hash(image_data_, image_size_);
    snapshot_.image_downloaded = true;
    Serial.printf("[trmnl] image downloaded bytes=%lu\n", static_cast<unsigned long>(image_size_));
    Serial.printf("[trmnl] image visual hash=%lu\n", static_cast<unsigned long>(snapshot_.image_visual_hash));

    if (!is_png_payload(image_data_, image_size_) && !is_jpeg_payload(image_data_, image_size_)) {
        if (is_bmp_payload(image_data_, image_size_)) {
            Serial.println("[trmnl] downloaded image is BMP; renderer supports PNG and JPEG");
        } else {
            Serial.println("[trmnl] downloaded image format unknown; renderer supports PNG and JPEG");
        }
        set_status(TrmnlFetchStatus::DecodeError);
        return false;
    }

    return true;
}

void TrmnlService::set_status(TrmnlFetchStatus status)
{
    snapshot_.status = status;
}

const char* trmnl_status_label(TrmnlFetchStatus status)
{
    switch (status) {
    case TrmnlFetchStatus::Idle:
        return "Idle";
    case TrmnlFetchStatus::ConnectingWifi:
        return "Connecting";
    case TrmnlFetchStatus::RequestingMetadata:
        return "Requesting";
    case TrmnlFetchStatus::DownloadingImage:
        return "Downloading";
    case TrmnlFetchStatus::Ready:
        return "Ready";
    case TrmnlFetchStatus::Disabled:
        return "TRMNL not configured";
    case TrmnlFetchStatus::Offline:
        return "Offline";
    case TrmnlFetchStatus::Unauthorized:
        return "TRMNL key rejected";
    case TrmnlFetchStatus::ServerError:
        return "Server error";
    case TrmnlFetchStatus::DecodeError:
        return "Image unsupported";
    case TrmnlFetchStatus::NoContent:
        return "No TRMNL screen";
    case TrmnlFetchStatus::MissingApiKey:
        return "TRMNL key missing";
    case TrmnlFetchStatus::DeviceNotFound:
        return "TRMNL device not found";
    }
    return "Unknown";
}

}  // namespace paper_screen
