#pragma once

#include <stddef.h>

#include "sdk/app_descriptor.h"

namespace paper_screen {

const AppDescriptor* app_registry(size_t* count);
const AppDescriptor* home_app_registry(size_t* count);
const AppDescriptor* find_app_by_id(const char* id);
const AppDescriptor* find_app_by_icon(AppIcon icon);

}  // namespace paper_screen
