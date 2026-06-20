#pragma once

#include <stdint.h>

namespace paper_screen {

enum class SleepWakeReason {
    BootButton,
    Timeout,
    Other,
};

class PowerService {
public:
    bool woke_from_timer() const;
    void enter_deep_sleep();
    void enter_deep_sleep_for(uint32_t seconds);
};

}  // namespace paper_screen
