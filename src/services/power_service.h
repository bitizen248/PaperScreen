#pragma once

namespace paper_screen {

enum class SleepWakeReason {
    BootButton,
    Timeout,
    Other,
};

class PowerService {
public:
    void enter_deep_sleep();
};

}  // namespace paper_screen
