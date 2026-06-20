#include <cstdint>
#include <cstdio>
#include <cstring>

#include "apps/trmnl/trmnl_app.h"
#include "apps/trmnl/trmnl_cache.h"
#include "apps/trmnl/trmnl_settings_adapter.h"
#include "apps/trmnl/trmnl_view.h"
#include "sdk/app.h"
#include "sdk/app_registry.h"

namespace {

int g_failures = 0;

#define EXPECT_TRUE(condition)                                                                  \
    do {                                                                                        \
        if (!(condition)) {                                                                      \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition);           \
            ++g_failures;                                                                        \
        }                                                                                       \
    } while (0)

#define EXPECT_EQ(expected, actual)                                                             \
    do {                                                                                        \
        const auto expected_value = (expected);                                                   \
        const auto actual_value = (actual);                                                       \
        if (!(expected_value == actual_value)) {                                                  \
            std::fprintf(stderr, "FAIL %s:%d: expected %lld got %lld\n", __FILE__, __LINE__,     \
                         static_cast<long long>(expected_value),                                  \
                         static_cast<long long>(actual_value));                                   \
            ++g_failures;                                                                        \
        }                                                                                       \
    } while (0)

class FakeStorage final : public paper_screen::AppStorageApi {
public:
    bool available() const override
    {
        return available_;
    }

    bool ensure_directory() override
    {
        ensured_ = true;
        return available_;
    }

    bool path(const char* leaf, char* out, size_t out_size) const override
    {
        if (leaf == nullptr || out == nullptr || out_size == 0) {
            return false;
        }
        const int written = std::snprintf(out, out_size, "/paperscreen/apps/test/%s", leaf);
        return written > 0 && static_cast<size_t>(written) < out_size;
    }

    bool write_text_atomic(const char* leaf, const char* text) override
    {
        last_write_leaf_ = leaf;
        if (text != nullptr) {
            std::strncpy(last_write_text_buffer_, text, sizeof(last_write_text_buffer_) - 1);
            last_write_text_buffer_[sizeof(last_write_text_buffer_) - 1] = '\0';
            last_write_text_ = last_write_text_buffer_;
        } else {
            last_write_text_ = nullptr;
        }
        if (available_ && leaf != nullptr && text != nullptr
            && (std::strcmp(leaf, "last-metadata.json") == 0 || std::strcmp(leaf, "cache-index.jsonl") == 0)) {
            std::strncpy(stored_metadata_, text, sizeof(stored_metadata_) - 1);
            stored_metadata_[sizeof(stored_metadata_) - 1] = '\0';
            has_stored_metadata_ = true;
            stored_metadata_leaf_ = leaf;
        }
        return available_ && leaf != nullptr && text != nullptr;
    }

    bool write_file_atomic(const char* leaf, const uint8_t* data, size_t size) override
    {
        last_file_leaf_ = leaf;
        last_file_data_ = data;
        last_file_size_ = size;
        if (available_ && leaf != nullptr && data != nullptr) {
            std::strncpy(stored_file_leaf_buffer_, leaf, sizeof(stored_file_leaf_buffer_) - 1);
            stored_file_leaf_buffer_[sizeof(stored_file_leaf_buffer_) - 1] = '\0';
            stored_file_leaf_ = stored_file_leaf_buffer_;
            stored_file_size_ = size <= sizeof(stored_file_) ? size : 0;
            if (stored_file_size_ > 0) {
                std::memcpy(stored_file_, data, stored_file_size_);
            }
        }
        return available_ && leaf != nullptr && data != nullptr;
    }

    bool file_size(const char* leaf, size_t* out_size) const override
    {
        if (out_size == nullptr || !available_ || leaf == nullptr || std::strcmp(leaf, stored_file_leaf_) != 0) {
            return false;
        }
        *out_size = stored_file_size_;
        return true;
    }

    bool read_file(const char* leaf, uint8_t* out, size_t capacity, size_t* out_size) const override
    {
        if (out_size != nullptr) {
            *out_size = 0;
        }
        if (!available_ || leaf == nullptr || out == nullptr) {
            return false;
        }
        if (std::strcmp(leaf, stored_file_leaf_) == 0) {
            if (stored_file_size_ > capacity) {
                return false;
            }
            std::memcpy(out, stored_file_, stored_file_size_);
            if (out_size != nullptr) {
                *out_size = stored_file_size_;
            }
            return true;
        }
        if (std::strcmp(leaf, stored_metadata_leaf_) == 0 && has_stored_metadata_) {
            const size_t len = std::strlen(stored_metadata_);
            if (len > capacity) {
                return false;
            }
            std::memcpy(out, stored_metadata_, len);
            if (out_size != nullptr) {
                *out_size = len;
            }
            return true;
        }
        return false;
    }

    bool remove_file(const char* leaf) override
    {
        last_remove_leaf_ = leaf;
        if (!available_ || leaf == nullptr) {
            return false;
        }
        if (std::strcmp(leaf, stored_file_leaf_) == 0) {
            stored_file_leaf_buffer_[0] = '\0';
            stored_file_leaf_ = stored_file_leaf_buffer_;
            stored_file_size_ = 0;
        }
        if (std::strcmp(leaf, stored_metadata_leaf_) == 0) {
            has_stored_metadata_ = false;
            stored_metadata_[0] = '\0';
            stored_metadata_leaf_ = "";
        }
        return true;
    }

    bool append_text(const char* leaf, const char* text) override
    {
        last_append_leaf_ = leaf;
        last_append_text_ = text;
        return available_ && leaf != nullptr && text != nullptr;
    }

    bool exists(const char* leaf) const override
    {
        return available_ && leaf != nullptr
            && (std::strcmp(leaf, "state.json") == 0
                || std::strcmp(leaf, stored_file_leaf_) == 0
                || (has_stored_metadata_ && std::strcmp(leaf, stored_metadata_leaf_) == 0));
    }

    bool available_ = true;
    bool ensured_ = false;
    const char* last_write_leaf_ = nullptr;
    const char* last_write_text_ = nullptr;
    char last_write_text_buffer_[4096] = {};
    const char* last_file_leaf_ = nullptr;
    const uint8_t* last_file_data_ = nullptr;
    size_t last_file_size_ = 0;
    const char* stored_file_leaf_ = "";
    char stored_file_leaf_buffer_[64] = {};
    uint8_t stored_file_[64] = {};
    size_t stored_file_size_ = 0;
    char stored_metadata_[640] = {};
    const char* stored_metadata_leaf_ = "";
    bool has_stored_metadata_ = false;
    const char* last_remove_leaf_ = nullptr;
    const char* last_append_leaf_ = nullptr;
    const char* last_append_text_ = nullptr;
};

class FakeNetwork final : public paper_screen::AppNetworkApi {
public:
    paper_screen::AppNetworkStatus status() const override
    {
        paper_screen::AppNetworkStatus status;
        status.state = connected_ ? paper_screen::AppNetworkState::Connected : paper_screen::AppNetworkState::Idle;
        status.connected = connected_;
        status.internet_access = connected_;
        status.rssi = -42;
        return status;
    }

    bool connect() override
    {
        connected_ = true;
        ++connect_count_;
        return true;
    }

    bool connect(uint32_t timeout_ms) override
    {
        last_timeout_ms_ = timeout_ms;
        return connect();
    }

    void disconnect() override
    {
        connected_ = false;
    }

    bool confirm_internet_access(uint32_t timeout_ms = 5000) override
    {
        last_timeout_ms_ = timeout_ms;
        return connected_;
    }

    bool is_connected() const override
    {
        return connected_;
    }

    int32_t rssi() const override
    {
        return -42;
    }

    bool connected_ = false;
    int connect_count_ = 0;
    uint32_t last_timeout_ms_ = 0;
};

class FakeClock final : public paper_screen::AppClockApi {
public:
    paper_screen::AppTimeStatus status() const override
    {
        paper_screen::AppTimeStatus status;
        status.rtc_initialized = true;
        status.rtc_valid = true;
        status.time_configured = true;
        status.current_epoch = now_;
        return status;
    }

    int64_t now_epoch() const override
    {
        return now_;
    }

    bool format_local_clock(int64_t, char* out, size_t out_size) const override
    {
        return write_text(out, out_size, "12:34");
    }

    bool format_local_time(int64_t, char* out, size_t out_size) const override
    {
        return write_text(out, out_size, "2026-05-31 12:34");
    }

    static bool write_text(char* out, size_t out_size, const char* text)
    {
        if (out == nullptr || out_size == 0 || std::strlen(text) >= out_size) {
            return false;
        }
        std::strcpy(out, text);
        return true;
    }

    int64_t now_ = 1780223640;
};

class FakeBattery final : public paper_screen::AppBatteryApi {
public:
    paper_screen::AppBatteryStatus status() const override
    {
        paper_screen::AppBatteryStatus status;
        status.initialized = true;
        status.valid = true;
        status.percentage = 81;
        status.voltage_mv = 4100;
        status.charging = true;
        return status;
    }

    bool update() override
    {
        ++update_count_;
        return true;
    }

    bool format_percentage(char* out, size_t out_size) const override
    {
        return FakeClock::write_text(out, out_size, "81%");
    }

    int update_count_ = 0;
};

class FakePower final : public paper_screen::AppPowerApi {
public:
    bool woke_from_timer() const override
    {
        return woke_from_timer_;
    }

    void request_deep_sleep_for(uint32_t seconds) override
    {
        sleep_requested_ = true;
        sleep_seconds_ = seconds;
    }

    bool consume_deep_sleep_request(uint32_t* seconds) override
    {
        if (!sleep_requested_) {
            return false;
        }
        if (seconds != nullptr) {
            *seconds = sleep_seconds_;
        }
        sleep_requested_ = false;
        sleep_seconds_ = 0;
        return true;
    }

    bool woke_from_timer_ = false;
    bool sleep_requested_ = false;
    uint32_t sleep_seconds_ = 0;
};

class FakeLog final : public paper_screen::AppLogApi {
public:
    void info(const char* message) const override
    {
        last_info_ = message;
    }

    void warn(const char* message) const override
    {
        last_warn_ = message;
    }

    void error(const char* message) const override
    {
        last_error_ = message;
    }

    mutable const char* last_info_ = nullptr;
    mutable const char* last_warn_ = nullptr;
    mutable const char* last_error_ = nullptr;
};

class CountingApp final : public paper_screen::PaperApp {
public:
    const paper_screen::AppDescriptor& descriptor() const override
    {
        return descriptor_;
    }

    void on_open(paper_screen::AppContext& context) override
    {
        ++open_count_;
        context.storage.ensure_directory();
        context.log.info("opened");
    }

    void on_close(paper_screen::AppContext&) override
    {
        ++close_count_;
    }

    paper_screen::AppEventResult on_event(paper_screen::AppContext& context,
                                          const paper_screen::AppEvent& event) override
    {
        last_event_type_ = event.type;
        context.network.connect(1234);
        context.storage.write_text_atomic("state.json", "{\"ok\":true}");

        paper_screen::AppEventResult result;
        result.handled = true;
        result.refresh_hint = paper_screen::AppRefreshHint::Partial;
        result.status_bar_changed = event.type == paper_screen::AppEventType::Tick;
        result.action.schedule_wake_seconds = 60;
        context.power.request_deep_sleep_for(60);
        return result;
    }

    paper_screen::AppRenderResult render(paper_screen::AppContext& context,
                                         paper_screen::DisplayRenderContext& render_context) override
    {
        ++render_count_;
        EXPECT_TRUE(render_context.width == 960);
        EXPECT_TRUE(render_context.height == 540);
        context.battery.update();

        paper_screen::AppRenderResult result;
        result.refresh_hint = paper_screen::AppRefreshHint::Full;
        return result;
    }

    paper_screen::AppDescriptor descriptor_ = {
        .id = "counting",
        .name = "Counting",
        .icon = paper_screen::AppIcon::Timer,
        .capabilities = paper_screen::AppCapabilityStorage | paper_screen::AppCapabilityTime,
        .show_on_home = true,
    };
    int open_count_ = 0;
    int close_count_ = 0;
    int render_count_ = 0;
    paper_screen::AppEventType last_event_type_ = paper_screen::AppEventType::None;
};

paper_screen::AppContext make_context(FakeStorage& storage,
                                      FakeNetwork& network,
                                      FakeClock& clock,
                                      FakeBattery& battery,
                                      FakePower& power,
                                      FakeLog& log)
{
    return paper_screen::AppContext("test", storage, network, clock, battery, power, log);
}

void test_registry()
{
    size_t count = 0;
    const paper_screen::AppDescriptor* apps = paper_screen::app_registry(&count);
    EXPECT_TRUE(apps != nullptr);
    EXPECT_TRUE(count >= 6);

    const paper_screen::AppDescriptor* trmnl = paper_screen::find_app_by_id("trmnl");
    EXPECT_TRUE(trmnl != nullptr);
    EXPECT_TRUE(std::strcmp(trmnl->name, "TRMNL") == 0);
    EXPECT_TRUE(paper_screen::app_has_capability(trmnl->capabilities, paper_screen::AppCapabilityNetwork));
    EXPECT_TRUE(paper_screen::app_has_capability(trmnl->capabilities, paper_screen::AppCapabilityStorage));
    EXPECT_TRUE(paper_screen::app_has_capability(trmnl->capabilities,
                                                paper_screen::AppCapabilityBackgroundSchedule));
    EXPECT_TRUE(paper_screen::app_has_capability(trmnl->capabilities, paper_screen::AppCapabilityWakeTimer));
    EXPECT_TRUE(paper_screen::app_has_capability(trmnl->capabilities, paper_screen::AppCapabilityBatteryState));
    EXPECT_TRUE(paper_screen::app_has_capability(trmnl->capabilities, paper_screen::AppCapabilityFullscreen));
    EXPECT_TRUE(paper_screen::app_has_capability(trmnl->capabilities, paper_screen::AppCapabilityBacklight));

    const paper_screen::AppDescriptor* settings = paper_screen::find_app_by_icon(paper_screen::AppIcon::Settings);
    EXPECT_TRUE(settings != nullptr);
    EXPECT_TRUE(std::strcmp(settings->id, "settings") == 0);
    EXPECT_TRUE(!paper_screen::app_has_capability(settings->capabilities, paper_screen::AppCapabilityNetwork));

    EXPECT_TRUE(paper_screen::find_app_by_id("missing") == nullptr);
}

void test_context_and_lifecycle()
{
    FakeStorage storage;
    FakeNetwork network;
    FakeClock clock;
    FakeBattery battery;
    FakePower power;
    FakeLog log;
    paper_screen::AppContext context = make_context(storage, network, clock, battery, power, log);

    CountingApp app;
    app.on_open(context);
    EXPECT_EQ(1, app.open_count_);
    EXPECT_TRUE(storage.ensured_);
    EXPECT_TRUE(std::strcmp(log.last_info_, "opened") == 0);

    paper_screen::AppEvent event;
    event.type = paper_screen::AppEventType::Tick;
    event.timestamp_ms = 100;
    const paper_screen::AppEventResult event_result = app.on_event(context, event);
    EXPECT_TRUE(event_result.handled);
    EXPECT_TRUE(event_result.refresh_hint == paper_screen::AppRefreshHint::Partial);
    EXPECT_TRUE(event_result.status_bar_changed);
    EXPECT_EQ(60, event_result.action.schedule_wake_seconds);
    EXPECT_TRUE(network.is_connected());
    EXPECT_EQ(1234, network.last_timeout_ms_);
    EXPECT_TRUE(std::strcmp(storage.last_write_leaf_, "state.json") == 0);

    uint32_t sleep_seconds = 0;
    EXPECT_TRUE(power.consume_deep_sleep_request(&sleep_seconds));
    EXPECT_EQ(60, sleep_seconds);
    EXPECT_TRUE(!power.consume_deep_sleep_request(nullptr));

    paper_screen::DisplayRenderContext render_context;
    render_context.width = 960;
    render_context.height = 540;
    const paper_screen::AppRenderResult render_result = app.render(context, render_context);
    EXPECT_TRUE(render_result.refresh_hint == paper_screen::AppRefreshHint::Full);
    EXPECT_EQ(1, app.render_count_);
    EXPECT_EQ(1, battery.update_count_);

    app.on_close(context);
    EXPECT_EQ(1, app.close_count_);
}

void test_trmnl_app_state()
{
    FakeStorage storage;
    FakeNetwork network;
    FakeClock clock;
    FakeBattery battery;
    FakePower power;
    FakeLog log;
    paper_screen::AppContext context = make_context(storage, network, clock, battery, power, log);

    paper_screen::TrmnlApp app;
    EXPECT_TRUE(std::strcmp(app.descriptor().id, "trmnl") == 0);
    EXPECT_TRUE(paper_screen::app_has_capability(app.descriptor().capabilities, paper_screen::AppCapabilityNetwork));
    EXPECT_TRUE(paper_screen::app_has_capability(app.descriptor().capabilities, paper_screen::AppCapabilityWakeTimer));

    app.state().image_rendered = true;
    app.state().last_image_url_hash = 42;
    app.state().home_menu_visible = true;
    app.state().refresh_scheduled = true;
    app.on_open(context);
    EXPECT_TRUE(storage.ensured_);
    EXPECT_TRUE(!app.state().image_rendered);
    EXPECT_EQ(0, app.state().last_image_url_hash);
    EXPECT_TRUE(!app.state().home_menu_visible);
    EXPECT_TRUE(!app.state().refresh_scheduled);

    app.mark_image_result(true, 99);
    EXPECT_TRUE(app.state().image_rendered);
    EXPECT_EQ(99, app.state().last_image_url_hash);
    app.mark_image_result(false, 99);
    EXPECT_TRUE(!app.state().image_rendered);
    EXPECT_EQ(0, app.state().last_image_url_hash);

    app.schedule_refresh(1000, 30);
    EXPECT_TRUE(app.state().refresh_scheduled);
    EXPECT_EQ(30, app.state().next_refresh_seconds);
    EXPECT_TRUE(!app.refresh_due(30999));
    EXPECT_TRUE(app.refresh_due(31000));

    app.clear_refresh_schedule();
    EXPECT_TRUE(!app.state().refresh_scheduled);

    paper_screen::AppEvent schedule_event;
    schedule_event.type = paper_screen::AppEventType::ScheduledWake;
    schedule_event.timestamp_ms = 2000;
    schedule_event.value = 45;
    paper_screen::AppEventResult schedule_result = app.on_event(context, schedule_event);
    EXPECT_TRUE(schedule_result.handled);
    EXPECT_EQ(45, schedule_result.action.schedule_wake_seconds);
    EXPECT_TRUE(app.state().refresh_scheduled);
    EXPECT_TRUE(!app.refresh_due(46999));

    paper_screen::AppEvent background_event;
    background_event.type = paper_screen::AppEventType::BackgroundRefresh;
    background_event.timestamp_ms = 47000;
    paper_screen::AppEventResult background_result = app.on_event(context, background_event);
    EXPECT_TRUE(background_result.handled);
    EXPECT_EQ(static_cast<uint32_t>(paper_screen::TrmnlAppCommand::Refresh), background_result.action.command);
    EXPECT_TRUE(!app.state().refresh_scheduled);

    schedule_event.value = 60;
    app.on_event(context, schedule_event);
    app.state().home_menu_visible = true;
    background_result = app.on_event(context, background_event);
    EXPECT_TRUE(!background_result.handled);
    EXPECT_TRUE(app.state().refresh_scheduled);
    app.clear_home_menu_state();

    app.state().home_menu_visible = true;
    app.state().home_press_active = true;
    app.state().home_ignore_until_release = true;
    app.clear_home_menu_state();
    EXPECT_TRUE(!app.state().home_menu_visible);
    EXPECT_TRUE(!app.state().home_press_active);
    EXPECT_TRUE(!app.state().home_ignore_until_release);

    paper_screen::AppEvent home_event;
    home_event.type = paper_screen::AppEventType::HomeButton;
    home_event.timestamp_ms = 5000;
    paper_screen::AppEventResult home_result = app.on_event(context, home_event);
    EXPECT_TRUE(home_result.handled);
    EXPECT_TRUE(app.state().home_menu_visible);
    EXPECT_EQ(static_cast<uint32_t>(paper_screen::TrmnlAppCommand::ShowMenu), home_result.action.command);

    home_result = app.on_event(context, home_event);
    EXPECT_TRUE(home_result.handled);
    EXPECT_TRUE(!app.state().home_menu_visible);
    EXPECT_EQ(static_cast<uint32_t>(paper_screen::TrmnlAppCommand::RenderScreen), home_result.action.command);

    paper_screen::AppEvent tick_event;
    tick_event.type = paper_screen::AppEventType::Tick;
    tick_event.timestamp_ms = 5200;
    tick_event.value = 140;
    app.on_event(context, tick_event);
    EXPECT_TRUE(!app.state().home_ignore_until_release);

    home_result = app.on_event(context, home_event);
    EXPECT_TRUE(app.state().home_menu_visible);

    paper_screen::AppEvent menu_event;
    menu_event.type = paper_screen::AppEventType::TouchUp;
    menu_event.timestamp_ms = 5300;
    menu_event.value = static_cast<uint32_t>(paper_screen::TrmnlAppMenuAction::NextScreen);
    paper_screen::AppEventResult menu_result = app.on_event(context, menu_event);
    EXPECT_TRUE(menu_result.handled);
    EXPECT_TRUE(!app.state().home_menu_visible);
    EXPECT_EQ(static_cast<uint32_t>(paper_screen::TrmnlAppCommand::NextScreen), menu_result.action.command);

    home_result = app.on_event(context, home_event);
    EXPECT_TRUE(app.state().home_menu_visible);
    menu_event.value = static_cast<uint32_t>(paper_screen::TrmnlAppMenuAction::RefetchCurrent);
    menu_result = app.on_event(context, menu_event);
    EXPECT_TRUE(menu_result.handled);
    EXPECT_TRUE(!app.state().home_menu_visible);
    EXPECT_EQ(static_cast<uint32_t>(paper_screen::TrmnlAppCommand::RefetchCurrent), menu_result.action.command);

    home_result = app.on_event(context, home_event);
    EXPECT_TRUE(app.state().home_menu_visible);
    menu_event.value = static_cast<uint32_t>(paper_screen::TrmnlAppMenuAction::SpecialFunction);
    menu_result = app.on_event(context, menu_event);
    EXPECT_TRUE(menu_result.handled);
    EXPECT_TRUE(!app.state().home_menu_visible);
    EXPECT_EQ(static_cast<uint32_t>(paper_screen::TrmnlAppCommand::SpecialFunction), menu_result.action.command);
}

void test_trmnl_settings_builder()
{
    paper_screen::TrmnlAppState state;
    state.image_rendered = true;
    state.last_image_url_hash = 12345;
    state.next_refresh_seconds = 900;

    paper_screen::TrmnlSettingsInputs inputs;
    inputs.enabled = true;
    inputs.mode = paper_screen::TrmnlMode::Mirror;
    inputs.api_key = "test-key";
    inputs.fallback_refresh_seconds = 1800;
    inputs.battery_voltage_mv = 3980;
    inputs.wifi_rssi = -55;
    inputs.display_width = 960;
    inputs.display_height = 540;
    inputs.disconnect_wifi_after_fetch = false;

    const paper_screen::TrmnlSettings settings = paper_screen::build_trmnl_settings(inputs, state);
    EXPECT_TRUE(settings.enabled);
    EXPECT_TRUE(settings.mode == paper_screen::TrmnlMode::Mirror);
    EXPECT_TRUE(std::strcmp(settings.api_key, "test-key") == 0);
    EXPECT_EQ(1800, settings.fallback_refresh_seconds);
    EXPECT_EQ(900, settings.current_refresh_seconds);
    EXPECT_EQ(3980, settings.battery_voltage_mv);
    EXPECT_EQ(-55, settings.wifi_rssi);
    EXPECT_EQ(960, settings.display_width);
    EXPECT_EQ(540, settings.display_height);
    EXPECT_TRUE(!settings.disconnect_wifi_after_fetch);
    EXPECT_TRUE(settings.allow_insecure_https);
    EXPECT_EQ(12345, settings.previous_image_url_hash);
    EXPECT_TRUE(settings.skip_unchanged_image);

    state.next_refresh_seconds = 0;
    const paper_screen::TrmnlSettings fallback_settings = paper_screen::build_trmnl_settings(inputs, state);
    EXPECT_EQ(1800, fallback_settings.current_refresh_seconds);
}

void test_trmnl_view_model()
{
    paper_screen::TrmnlView view;

    paper_screen::TrmnlSnapshot loading;
    loading.status = paper_screen::TrmnlFetchStatus::RequestingMetadata;
    loading.response.refresh_seconds = 120;
    view.set_snapshot(loading, nullptr, 0);
    paper_screen::TrmnlViewModel model = view.view_model();
    EXPECT_TRUE(std::strcmp(model.status_bar.title, "TRMNL") == 0);
    EXPECT_TRUE(model.status == paper_screen::TrmnlFetchStatus::RequestingMetadata);
    EXPECT_TRUE(std::strcmp(model.detail, "Refresh 120 sec") == 0);

    paper_screen::TrmnlSnapshot ready;
    ready.status = paper_screen::TrmnlFetchStatus::Ready;
    ready.image_bytes = 32;
    const uint8_t image_data[4] = {1, 2, 3, 4};
    view.set_snapshot(ready, image_data, sizeof(image_data));
    view.set_exit_prompt(true);
    model = view.view_model();
    EXPECT_TRUE(model.status == paper_screen::TrmnlFetchStatus::Ready);
    EXPECT_TRUE(model.image_data == image_data);
    EXPECT_EQ(4, model.image_size);
    EXPECT_TRUE(model.show_exit_prompt);
    EXPECT_TRUE(std::strcmp(model.detail, "32 bytes") == 0);
}

void test_trmnl_cache()
{
    FakeStorage storage;
    FakeNetwork network;
    FakeClock clock;
    FakeBattery battery;
    FakePower power;
    FakeLog log;
    paper_screen::AppContext context = make_context(storage, network, clock, battery, power, log);

    paper_screen::TrmnlSnapshot snapshot;
    snapshot.status = paper_screen::TrmnlFetchStatus::Ready;
    snapshot.image_url_hash = 77;
    snapshot.image_visual_hash = 88;
    snapshot.response.status = 200;
    snapshot.response.refresh_seconds = 300;
    std::strcpy(snapshot.response.image_name, "screen.png");
    std::strcpy(snapshot.response.metadata_etag, "W/metadata-test");
    std::strcpy(snapshot.response.image_etag, "W/image-test");
    const uint8_t image_data[3] = {7, 8, 9};

    paper_screen::TrmnlCache cache;
    EXPECT_TRUE(cache.save_last_good(context, snapshot, image_data, sizeof(image_data)));
    EXPECT_TRUE(storage.ensured_);
    EXPECT_TRUE(std::strcmp(storage.last_file_leaf_, "image-0000004d.png") == 0);
    EXPECT_TRUE(storage.last_file_data_ == image_data);
    EXPECT_EQ(3, storage.last_file_size_);
    EXPECT_TRUE(std::strcmp(storage.last_write_leaf_, "cache-index.jsonl") == 0);
    EXPECT_TRUE(std::strstr(storage.last_write_text_, "\"image_url_hash\":77") != nullptr);
    EXPECT_TRUE(std::strstr(storage.last_write_text_, "\"image_visual_hash\":88") != nullptr);
    EXPECT_TRUE(std::strstr(storage.last_write_text_, "\"expires_epoch\":1780223940") != nullptr);
    EXPECT_TRUE(std::strstr(storage.last_write_text_, "\"metadata_etag\":\"W/metadata-test\"") != nullptr);
    EXPECT_TRUE(std::strstr(storage.last_write_text_, "\"image_etag\":\"W/image-test\"") != nullptr);

    paper_screen::TrmnlSnapshot cached_snapshot;
    uint8_t* cached_image = nullptr;
    size_t cached_image_size = 0;
    EXPECT_TRUE(cache.load_last_good(context, &cached_snapshot, &cached_image, &cached_image_size));
    EXPECT_TRUE(cached_image != nullptr);
    EXPECT_EQ(static_cast<int>(paper_screen::TrmnlFetchStatus::Ready), static_cast<int>(cached_snapshot.status));
    EXPECT_EQ(3, cached_snapshot.image_bytes);
    EXPECT_EQ(300, cached_snapshot.response.refresh_seconds);
    EXPECT_EQ(77, cached_snapshot.image_url_hash);
    EXPECT_EQ(88, cached_snapshot.image_visual_hash);
    EXPECT_TRUE(std::strcmp(cached_snapshot.response.image_name, "screen.png") == 0);
    EXPECT_TRUE(std::strcmp(cached_snapshot.response.metadata_etag, "W/metadata-test") == 0);
    EXPECT_TRUE(std::strcmp(cached_snapshot.response.image_etag, "W/image-test") == 0);
    EXPECT_EQ(3, cached_image_size);
    EXPECT_TRUE(cached_image[0] == 7 && cached_image[1] == 8 && cached_image[2] == 9);
    cache.release_image(cached_image);

    EXPECT_TRUE(cache.clear(context));
    EXPECT_TRUE(!storage.exists("image-0000004d.png"));
    EXPECT_TRUE(!storage.exists("cache-index.jsonl"));
    EXPECT_TRUE(!cache.load_metadata(context, &cached_snapshot));

    snapshot.status = paper_screen::TrmnlFetchStatus::Offline;
    EXPECT_TRUE(!cache.save_last_good(context, snapshot, image_data, sizeof(image_data)));
}

void test_formatting_helpers()
{
    FakeStorage storage;
    FakeNetwork network;
    FakeClock clock;
    FakeBattery battery;
    FakePower power;
    FakeLog log;
    paper_screen::AppContext context = make_context(storage, network, clock, battery, power, log);

    char buffer[24] = {};
    EXPECT_TRUE(context.clock.format_local_clock(context.clock.now_epoch(), buffer, sizeof(buffer)));
    EXPECT_TRUE(std::strcmp(buffer, "12:34") == 0);

    EXPECT_TRUE(context.battery.format_percentage(buffer, sizeof(buffer)));
    EXPECT_TRUE(std::strcmp(buffer, "81%") == 0);

    EXPECT_TRUE(context.storage.exists("state.json"));
    EXPECT_TRUE(!context.storage.exists("missing.json"));
}

}  // namespace

int main()
{
    test_registry();
    test_context_and_lifecycle();
    test_trmnl_app_state();
    test_trmnl_settings_builder();
    test_trmnl_view_model();
    test_trmnl_cache();
    test_formatting_helpers();

    if (g_failures != 0) {
        std::fprintf(stderr, "%d SDK test failure(s)\n", g_failures);
        return 1;
    }

    std::printf("SDK tests passed\n");
    return 0;
}
