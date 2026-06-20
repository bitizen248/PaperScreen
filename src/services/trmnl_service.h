#pragma once

#include <cstddef>
#include <cstdint>

#include "services/settings_service.h"
#include "services/wifi_service.h"

namespace paper_screen {

constexpr size_t kTrmnlImageUrlCapacity = 1536;
constexpr size_t kTrmnlPreviousImageCapacity = 6;

enum class TrmnlFetchStatus {
    Idle,
    ConnectingWifi,
    RequestingMetadata,
    DownloadingImage,
    Ready,
    Disabled,
    Offline,
    Unauthorized,
    ServerError,
    DecodeError,
    NoContent,
    MissingApiKey,
    DeviceNotFound,
};

struct TrmnlSettings {
    bool enabled = false;
    TrmnlMode mode = TrmnlMode::Playlist;
    const char* base_url = "https://trmnl.com";
    const char* api_key = "";
    uint32_t fallback_refresh_seconds = 1800;
    uint32_t current_refresh_seconds = 1800;
    uint16_t battery_voltage_mv = 0;
    int32_t wifi_rssi = 0;
    uint16_t display_width = 960;
    uint16_t display_height = 540;
    bool disconnect_wifi_after_fetch = true;
    bool allow_insecure_https = true;
    uint32_t previous_image_url_hash = 0;
    const char* previous_image_name = "";
    const char* previous_metadata_etag = "";
    const char* previous_image_etag = "";
    size_t previous_image_count = 0;
    uint32_t previous_image_url_hashes[kTrmnlPreviousImageCapacity] = {};
    const char* previous_image_names[kTrmnlPreviousImageCapacity] = {};
    const char* previous_metadata_etags[kTrmnlPreviousImageCapacity] = {};
    const char* previous_image_etags[kTrmnlPreviousImageCapacity] = {};
    bool skip_unchanged_image = false;
    bool cached_image_available = false;
    bool special_function = false;
    bool force_playlist_advance = false;
    bool force_current_screen = false;
};

struct TrmnlDisplayResponse {
    int status = 0;
    char image_url[kTrmnlImageUrlCapacity] = {};
    char image_name[128] = {};
    char error[128] = {};
    char metadata_etag[96] = {};
    char image_etag[96] = {};
    uint32_t refresh_seconds = 1800;
    bool reset_firmware = false;
    bool maximum_compatibility = false;
    char special_fn[32] = {};
};

struct TrmnlSnapshot {
    TrmnlFetchStatus status = TrmnlFetchStatus::Idle;
    TrmnlDisplayResponse response;
    uint32_t image_bytes = 0;
    uint32_t image_url_hash = 0;
    uint32_t image_visual_hash = 0;
    bool image_downloaded = false;
};

class TrmnlService {
public:
    ~TrmnlService();

    const TrmnlSnapshot& snapshot() const;
    const TrmnlSnapshot& fetch(const TrmnlSettings& settings, WifiService& wifi);

    const uint8_t* image_data() const;
    size_t image_size() const;

private:
    void clear_image();
    bool request_metadata(const TrmnlSettings& settings);
    bool download_image(const TrmnlSettings& settings);
    void set_status(TrmnlFetchStatus status);

    TrmnlSnapshot snapshot_;
    uint8_t* image_data_ = nullptr;
    size_t image_size_ = 0;
};

const char* trmnl_status_label(TrmnlFetchStatus status);

}  // namespace paper_screen
