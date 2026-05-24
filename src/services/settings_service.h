#pragma once

#include "display/refresh_policy.h"

namespace paper_screen {

enum class TrmnlMode {
    Mirror,
    Playlist,
};

class SettingsService {
public:
    void begin();
    RefreshPolicy refresh_policy() const;

    bool wifi_enabled() const;
    void set_wifi_enabled(bool enabled);

    bool trmnl_enabled() const;
    void set_trmnl_enabled(bool enabled);

    TrmnlMode trmnl_mode() const;
    void set_trmnl_mode(TrmnlMode mode);

    bool trmnl_api_key_configured() const;
    const char* trmnl_api_key() const;

private:
    RefreshPolicy refresh_policy_ = RefreshPolicy::Conservative;
    bool wifi_enabled_ = true;
    bool trmnl_enabled_ = false;
    TrmnlMode trmnl_mode_ = TrmnlMode::Playlist;
    bool trmnl_api_key_configured_ = false;
    char trmnl_api_key_[96] = {};
};

}  // namespace paper_screen
