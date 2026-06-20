#pragma once

#include <stdint.h>

namespace paper_screen {

enum class RefreshPolicy : uint8_t {
    Conservative,
    PartialWhenSafe,
};

}  // namespace paper_screen
