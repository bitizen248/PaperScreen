#pragma once

#include <stdint.h>

#include <epdiy.h>

#include "display/render_context.h"
#include "ui/home_screen.h"
#include "ui/lock_screen.h"

namespace paper_screen {

enum class RefreshPolicy : uint8_t {
    Conservative,
    PartialWhenSafe,
};

class Display {
public:
    void begin(RefreshPolicy refresh_policy);
    void full_refresh();
    void render_boot_screen(const char* message);
    void render_lock_screen(const LockScreenViewModel& view_model);
    void render(const HomeViewModel& view_model);
    void render(const AppScreenViewModel& view_model);
    void render_content(const HomeViewModel& view_model);
    void render_content(const AppScreenViewModel& view_model);
    void render_dropdown();
    int hit_test_home_app(const HomeViewModel& view_model, int x, int y) const;
    int width() const;
    int height() const;
    int status_bar_height() const;

private:
    void update_screen(enum EpdDrawMode mode);
    void update_area(EpdRect area, enum EpdDrawMode mode);
    void clear_content_area(DisplayRenderContext ctx);

    RefreshPolicy refresh_policy_ = RefreshPolicy::Conservative;
    bool initialized_ = false;
    int width_ = 0;
    int height_ = 0;
};

}  // namespace paper_screen
