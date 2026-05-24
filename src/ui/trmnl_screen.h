#pragma once

#include <cstddef>
#include <cstdint>

#include "services/trmnl_service.h"
#include "ui/home_screen.h"

namespace paper_screen {

struct TrmnlViewModel {
    StatusBarViewModel status_bar;
    TrmnlFetchStatus status = TrmnlFetchStatus::Idle;
    const char* title = "TRMNL";
    const char* message = "";
    const char* detail = "";
    const uint8_t* image_data = nullptr;
    size_t image_size = 0;
    bool show_exit_prompt = false;
};

class TrmnlScreen {
public:
    void set_snapshot(TrmnlSnapshot snapshot, const uint8_t* image_data, size_t image_size);
    void set_exit_prompt(bool show);
    TrmnlViewModel view_model() const;

private:
    TrmnlSnapshot snapshot_;
    const uint8_t* image_data_ = nullptr;
    size_t image_size_ = 0;
    bool show_exit_prompt_ = false;
    char detail_[96] = {};
};

}  // namespace paper_screen
