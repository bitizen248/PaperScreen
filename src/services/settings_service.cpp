#include "settings_service.h"

#include <Arduino.h>

namespace paper_screen {

void SettingsService::begin()
{
    Serial.println("[settings] begin");
    refresh_policy_ = RefreshPolicy::Conservative;
    Serial.println("[settings] using default settings");
    Serial.println("[settings] ready");
}

RefreshPolicy SettingsService::refresh_policy() const
{
    return refresh_policy_;
}

}  // namespace paper_screen
