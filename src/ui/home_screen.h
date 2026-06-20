#pragma once

#include "board/board.h"
#include "sdk/app_descriptor.h"
#include "services/time_service.h"
#include "ui/status_bar_view_model.h"

namespace paper_screen {

struct HomeViewModel {
    const char* title = "Home";
    const char* status_line = "Board shell ready";
    StatusBarViewModel status_bar;
    const AppDescriptor* apps = nullptr;
    size_t app_count = 0;
};

struct AppScreenViewModel {
    StatusBarViewModel status_bar;
    AppIcon icon = AppIcon::Tasks;
    const char* app_name = "";
    const char* headline = "";
    const char* detail = "";
    const char* footer = "";
};

class HomeScreen {
public:
    void set_board_status(BoardStatus status);
    void set_time_status(TimeStatus status);
    HomeViewModel view_model() const;

private:
    BoardStatus board_status_;
    TimeStatus time_status_;
};

}  // namespace paper_screen
