#pragma once

namespace paper_screen {

struct LockScreenViewModel {
    const char* message = "Sleeping";
    const char* time = "--:--";
    const char* battery = "--%";
    const char* wake_hint = "Press to wake up";
};

}  // namespace paper_screen
