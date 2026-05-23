#pragma once

#include "display/display.h"

namespace paper_screen {

class SettingsService {
public:
    void begin();
    RefreshPolicy refresh_policy() const;

private:
    RefreshPolicy refresh_policy_ = RefreshPolicy::Conservative;
};

}  // namespace paper_screen
