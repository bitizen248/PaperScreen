#pragma once

#include <cstdint>

namespace paper_screen {

enum class WifiConnectionState {
    Disabled,
    Idle,
    Connecting,
    Connected,
    Failed,
};

enum class WifiError {
    None,
    Disabled,
    MissingCredentials,
    Timeout,
    Disconnected,
    Unknown,
};

struct WifiSettings {
    bool enabled = false;
    char ssid[64] = {};
    char password[96] = {};
    bool auto_connect_for_desk = true;
    bool disconnect_after_network_task = true;
    uint32_t connect_timeout_ms = 15000;
};

struct WifiStatus {
    WifiConnectionState state = WifiConnectionState::Idle;
    WifiError last_error = WifiError::None;
    bool connected = false;
    bool internet_access = false;
    int32_t rssi = 0;
    char ssid[64] = {};
};

class WifiService {
public:
    void begin();
    void set_enabled(bool enabled);

    WifiSettings settings() const;
    WifiStatus status() const;

    bool connect();
    bool connect(uint32_t timeout_ms);
    void disconnect();
    bool confirm_internet_access(uint32_t timeout_ms = 5000);

    bool is_connected() const;
    int32_t rssi() const;

private:
    bool enabled_ = true;
    WifiStatus status_;
};

}  // namespace paper_screen
