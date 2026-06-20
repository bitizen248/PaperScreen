#pragma once

#include <stdint.h>

#include "sdk/app_capability.h"
#include "sdk/app_icon.h"

namespace paper_screen {

struct AppDescriptor {
    const char* id;
    const char* name;
    AppIcon icon;
    uint32_t capabilities;
    bool show_on_home;
};

}  // namespace paper_screen
