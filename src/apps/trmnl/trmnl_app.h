#pragma once

#include <stdint.h>

#include "sdk/app.h"

namespace paper_screen {

enum class TrmnlAppCommand : uint32_t {
    None = 0,
    RenderScreen = 1,
    ShowMenu = 2,
    Refresh = 3,
    SpecialFunction = 4,
    ToggleLight = 5,
    ReturnHome = 6,
    NextScreen = 7,
    RefetchCurrent = 8,
};

enum class TrmnlAppMenuAction : uint32_t {
    None = 0,
    NextScreen = 1,
    RefetchCurrent = 2,
    SpecialFunction = 3,
    ToggleLight = 4,
    ReturnHome = 5,
    Cancel = 6,
};

struct TrmnlAppState {
    bool image_rendered = false;
    uint32_t last_image_url_hash = 0;

    bool home_press_active = false;
    bool home_menu_visible = false;
    unsigned long home_last_seen_ms = 0;
    bool home_ignore_until_release = false;

    bool autonomous_start_pending = false;

    bool refresh_scheduled = false;
    unsigned long next_refresh_due_ms = 0;
    uint32_t next_refresh_seconds = 0;
};

class TrmnlApp final : public PaperApp {
public:
    const AppDescriptor& descriptor() const override;

    void on_open(AppContext& context) override;
    void on_close(AppContext& context) override;
    AppEventResult on_event(AppContext& context, const AppEvent& event) override;
    AppRenderResult render(AppContext& context, DisplayRenderContext& render_context) override;

    TrmnlAppState& state();
    const TrmnlAppState& state() const;

    void reset_session();
    void clear_home_menu_state();
    void mark_image_result(bool rendered, uint32_t image_url_hash);
    void schedule_refresh(unsigned long now_ms, uint32_t refresh_seconds);
    void clear_refresh_schedule();
    bool refresh_due(unsigned long now_ms) const;

private:
    TrmnlAppState state_;
};

}  // namespace paper_screen
