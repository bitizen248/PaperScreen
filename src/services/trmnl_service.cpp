#include "trmnl_service.h"

#include <Arduino.h>
#include <cstring>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>
#include <cstdlib>
#include <cstring>
#include <esp_heap_caps.h>

#include "../../lib/epdiy/examples/weather/main/ArduinoJson.h"

namespace {

constexpr char kBaseUrl[] = "http://192.168.50.147:2300";
constexpr uint32_t kMinimumRefreshSeconds = 300;
constexpr size_t kMaxImageBytes = 1024 * 1024;

bool is_png_payload(const uint8_t* data, size_t size)
{
    static const uint8_t kPngSignature[8] = {
        0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A
    };
    return data != nullptr && size >= sizeof(kPngSignature) &&
           std::memcmp(data, kPngSignature, sizeof(kPngSignature)) == 0;
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
    if (is_bmp_payload(data, size)) return "bmp";
    return "unknown";
}

void copy_text(char* destination, size_t destination_size, const char* source)
{
    if (destination_size == 0) {
        return;
    }

    std::strncpy(destination, source == nullptr ? "" : source, destination_size - 1);
    destination[destination_size - 1] = '\0';
}

String endpoint_for(paper_screen::TrmnlMode mode)
{
    if (mode == paper_screen::TrmnlMode::Playlist) {
        return String(kBaseUrl) + "/api/display";
    }
    return String(kBaseUrl) + "/api/current_screen";
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

bool parse_metadata_response(const String& body, uint32_t fallback_refresh_seconds, paper_screen::TrmnlDisplayResponse* response)
{
    DynamicJsonDocument doc(2048);
    const DeserializationError error = deserializeJson(doc, body);
    if (error) {
        Serial.printf("[trmnl] metadata json error=%s\n", error.c_str());
        return false;
    }

    response->status = doc["status"] | 0;

    const char* image_url = doc["image_url"] | "";
    if (image_url[0] == '\0') {
        return false;
    }
    copy_text(response->image_url, sizeof(response->image_url), image_url);

    const char* image_name = doc["image_name"] | doc["filename"] | "";
    copy_text(response->image_name, sizeof(response->image_name), image_name);
    response->refresh_seconds = json_refresh_seconds(doc["refresh_rate"], fallback_refresh_seconds);
    return true;
}

}  // namespace

namespace paper_screen {

TrmnlService::~TrmnlService()
{
    clear_image();
}

TrmnlSnapshot TrmnlService::snapshot() const
{
    return snapshot_;
}

TrmnlSnapshot TrmnlService::fetch(const TrmnlSettings& settings, WifiService& wifi)
{
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

    if (settings.skip_unchanged_image
        && settings.previous_image_url != nullptr
        && settings.previous_image_url[0] != '\0'
        && std::strcmp(settings.previous_image_url, snapshot_.response.image_url) == 0
        && image_data_ != nullptr
        && image_size_ > 0) {
        Serial.println("[trmnl] image unchanged; reusing cached image");
        snapshot_.image_bytes = static_cast<uint32_t>(image_size_);
        if (settings.disconnect_wifi_after_fetch) {
            wifi.disconnect();
        }
        set_status(TrmnlFetchStatus::Ready);
        return snapshot_;
    }

    clear_image();
    if (!download_image()) {
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
    const String url = endpoint_for(settings.mode);
    const String device_id = WiFi.macAddress();
    Serial.printf("[trmnl] endpoint=%s\n", url.c_str());
    Serial.printf("[trmnl] device id=%s\n", device_id.c_str());
    if (!begin_insecure_request(http, plain_client, secure_client, url.c_str(), settings.allow_insecure_https)) {
        set_status(TrmnlFetchStatus::ServerError);
        return false;
    }

    http.addHeader("Access-Token", settings.api_key);
    http.addHeader("ID", device_id);
    http.addHeader("User-Agent", "PaperScreen/dev");

    const int http_code = http.GET();
    Serial.printf("[trmnl] metadata http=%d\n", http_code);
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
        set_status(TrmnlFetchStatus::NoContent);
        return false;
    }

    Serial.printf("[trmnl] metadata ok refresh=%lu image=%s\n",
                  static_cast<unsigned long>(snapshot_.response.refresh_seconds),
                  snapshot_.response.image_name);
    return true;
}

bool TrmnlService::download_image()
{
    set_status(TrmnlFetchStatus::DownloadingImage);

    WiFiClient plain_client;
    WiFiClientSecure secure_client;
    HTTPClient http;
    if (!begin_insecure_request(http, plain_client, secure_client, snapshot_.response.image_url, true)) {
        Serial.printf("[trmnl] image begin failed url=%s\n",
                      snapshot_.response.image_url);
        set_status(TrmnlFetchStatus::ServerError);
        return false;
    }

    const int http_code = http.GET();
    if (http_code < 200 || http_code >= 300) {
        Serial.printf("[trmnl] image http=%d\n", http_code);
        const String body = http.getString();
        if (body.length() > 0) Serial.printf("[trmnl] image error body=%s\n", body.c_str());
        http.end();
        set_status(TrmnlFetchStatus::ServerError);
        return false;
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
    Serial.printf("[trmnl] image format=%s url=%s\n",
                  payload_format_name(image_data_, image_size_),
                  snapshot_.response.image_url);

    snapshot_.image_bytes = static_cast<uint32_t>(image_size_);
    Serial.printf("[trmnl] image downloaded bytes=%lu\n", static_cast<unsigned long>(image_size_));

    if (!is_png_payload(image_data_, image_size_)) {
        if (is_bmp_payload(image_data_, image_size_)) {
            Serial.println("[trmnl] downloaded image is BMP; current renderer only supports PNG");
        } else {
            Serial.println("[trmnl] downloaded image format is unknown; current renderer only supports PNG");
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
    }
    return "Unknown";
}

}  // namespace paper_screen
