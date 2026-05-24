#pragma once

#include <cstddef>
#include <cstdint>

#include "services/settings_service.h"
#include "services/wifi_service.h"

namespace paper_screen {

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
};

struct TrmnlSettings {
    bool enabled = false;
    TrmnlMode mode = TrmnlMode::Playlist;
    const char* api_key = "";
    uint32_t fallback_refresh_seconds = 1800;
    bool disconnect_wifi_after_fetch = true;
    bool allow_insecure_https = true;
    const char* previous_image_url = "";
    bool skip_unchanged_image = false;
};

struct TrmnlDisplayResponse {
    int status = 0;
    char image_url[384] = {};
    char image_name[128] = {};
    uint32_t refresh_seconds = 1800;
};

struct TrmnlSnapshot {
    TrmnlFetchStatus status = TrmnlFetchStatus::Idle;
    TrmnlDisplayResponse response;
    uint32_t image_bytes = 0;
};

class TrmnlService {
public:
    ~TrmnlService();

    TrmnlSnapshot snapshot() const;
    TrmnlSnapshot fetch(const TrmnlSettings& settings, WifiService& wifi);

    const uint8_t* image_data() const;
    size_t image_size() const;

private:
    void clear_image();
    bool request_metadata(const TrmnlSettings& settings);
    bool download_image();
    void set_status(TrmnlFetchStatus status);

    TrmnlSnapshot snapshot_;
    uint8_t* image_data_ = nullptr;
    size_t image_size_ = 0;
};

const char* trmnl_status_label(TrmnlFetchStatus status);

}  // namespace paper_screen
