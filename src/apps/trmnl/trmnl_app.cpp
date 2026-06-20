#include "apps/trmnl/trmnl_app.h"

#include "sdk/app_registry.h"

namespace paper_screen {

namespace {

constexpr AppDescriptor kFallbackTrmnlDescriptor = {
    .id = "trmnl",
    .name = "TRMNL",
    .icon = AppIcon::Trmnl,
    .capabilities = AppCapabilityNetwork | AppCapabilityStorage | AppCapabilityBackgroundSchedule |
                    AppCapabilityWakeTimer | AppCapabilityBatteryState | AppCapabilityFullscreen |
                    AppCapabilityBacklight,
    .show_on_home = true,
};

}  // namespace

const AppDescriptor& TrmnlApp::descriptor() const
{
    const AppDescriptor* descriptor = find_app_by_id("trmnl");
    return descriptor != nullptr ? *descriptor : kFallbackTrmnlDescriptor;
}

void TrmnlApp::on_open(AppContext& context)
{
    reset_session();
    context.storage.ensure_directory();
    context.log.info("opened");
}

void TrmnlApp::on_close(AppContext& context)
{
    reset_session();
    context.log.info("closed");
}

AppEventResult TrmnlApp::on_event(AppContext&, const AppEvent& event)
{
    AppEventResult result;

    switch (event.type) {
    case AppEventType::HomeButton:
        result.handled = true;
        if (state_.home_menu_visible) {
            state_.home_menu_visible = false;
            state_.home_press_active = false;
            state_.home_ignore_until_release = true;
            state_.home_last_seen_ms = event.timestamp_ms;
            result.action.command = static_cast<uint32_t>(TrmnlAppCommand::RenderScreen);
            result.refresh_hint = AppRefreshHint::Full;
            return result;
        }

        if (state_.home_ignore_until_release) {
            state_.home_last_seen_ms = event.timestamp_ms;
            return result;
        }

        if (!state_.home_press_active) {
            state_.home_press_active = true;
            state_.home_menu_visible = true;
            state_.home_ignore_until_release = true;
            state_.home_last_seen_ms = event.timestamp_ms;
            result.action.command = static_cast<uint32_t>(TrmnlAppCommand::ShowMenu);
            result.refresh_hint = AppRefreshHint::Partial;
            return result;
        }

        state_.home_last_seen_ms = event.timestamp_ms;
        return result;

    case AppEventType::Tick:
        result.handled = true;
        if (state_.home_ignore_until_release) {
            if (event.timestamp_ms - state_.home_last_seen_ms >= event.value) {
                state_.home_ignore_until_release = false;
                state_.home_last_seen_ms = 0;
            }
            return result;
        }

        if (state_.home_press_active) {
            state_.home_press_active = false;
            state_.home_last_seen_ms = 0;
        }
        return result;

    case AppEventType::BackgroundRefresh:
        if (state_.home_menu_visible || !state_.refresh_scheduled || !refresh_due(event.timestamp_ms)) {
            return result;
        }

        result.handled = true;
        result.refresh_hint = AppRefreshHint::Full;
        result.action.command = static_cast<uint32_t>(TrmnlAppCommand::Refresh);
        clear_refresh_schedule();
        return result;

    case AppEventType::ScheduledWake:
        result.handled = true;
        if (event.value == 0) {
            clear_refresh_schedule();
            result.action.clear_scheduled_wake = true;
            return result;
        }

        schedule_refresh(event.timestamp_ms, event.value);
        result.action.schedule_wake_seconds = event.value;
        return result;

    case AppEventType::TouchUp:
        if (!state_.home_menu_visible) {
            return result;
        }

        clear_home_menu_state();
        result.handled = true;
        result.refresh_hint = AppRefreshHint::Full;
        switch (static_cast<TrmnlAppMenuAction>(event.value)) {
        case TrmnlAppMenuAction::None:
            result.handled = false;
            result.refresh_hint = AppRefreshHint::None;
            return result;
        case TrmnlAppMenuAction::NextScreen:
            result.action.command = static_cast<uint32_t>(TrmnlAppCommand::NextScreen);
            return result;
        case TrmnlAppMenuAction::RefetchCurrent:
            result.action.command = static_cast<uint32_t>(TrmnlAppCommand::RefetchCurrent);
            return result;
        case TrmnlAppMenuAction::SpecialFunction:
            result.action.command = static_cast<uint32_t>(TrmnlAppCommand::SpecialFunction);
            return result;
        case TrmnlAppMenuAction::ToggleLight:
            result.action.command = static_cast<uint32_t>(TrmnlAppCommand::ToggleLight);
            return result;
        case TrmnlAppMenuAction::ReturnHome:
            result.action.return_home = true;
            result.action.command = static_cast<uint32_t>(TrmnlAppCommand::ReturnHome);
            return result;
        case TrmnlAppMenuAction::Cancel:
            result.action.command = static_cast<uint32_t>(TrmnlAppCommand::RenderScreen);
            return result;
        }
        return result;

    default:
        return result;
    }
}

AppRenderResult TrmnlApp::render(AppContext&, DisplayRenderContext&)
{
    return {};
}

TrmnlAppState& TrmnlApp::state()
{
    return state_;
}

const TrmnlAppState& TrmnlApp::state() const
{
    return state_;
}

void TrmnlApp::reset_session()
{
    state_.image_rendered = false;
    state_.last_image_url_hash = 0;
    clear_home_menu_state();
    clear_refresh_schedule();
}

void TrmnlApp::clear_home_menu_state()
{
    state_.home_press_active = false;
    state_.home_menu_visible = false;
    state_.home_last_seen_ms = 0;
    state_.home_ignore_until_release = false;
}

void TrmnlApp::mark_image_result(bool rendered, uint32_t image_url_hash)
{
    state_.image_rendered = rendered;
    state_.last_image_url_hash = rendered ? image_url_hash : 0;
}

void TrmnlApp::schedule_refresh(unsigned long now_ms, uint32_t refresh_seconds)
{
    if (refresh_seconds == 0) {
        clear_refresh_schedule();
        return;
    }

    const unsigned long max_seconds = 0xFFFFFFFFUL / 1000UL;
    const unsigned long delay_ms = refresh_seconds > max_seconds
        ? 0xFFFFFFFFUL
        : static_cast<unsigned long>(refresh_seconds) * 1000UL;
    state_.next_refresh_due_ms = now_ms + delay_ms;
    state_.next_refresh_seconds = refresh_seconds;
    state_.refresh_scheduled = true;
}

void TrmnlApp::clear_refresh_schedule()
{
    state_.refresh_scheduled = false;
    state_.next_refresh_due_ms = 0;
    state_.next_refresh_seconds = 0;
}

bool TrmnlApp::refresh_due(unsigned long now_ms) const
{
    return state_.refresh_scheduled && static_cast<long>(now_ms - state_.next_refresh_due_ms) >= 0;
}

}  // namespace paper_screen
