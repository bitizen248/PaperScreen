#include "battery_service.h"

#include <Arduino.h>
#include <cstdio>
#include <cstring>

namespace paper_screen {

namespace {

constexpr uint8_t kLowBatteryPercent = 15;

void format_percentage_value(bool valid, uint8_t percentage, char* out, size_t out_size)
{
    if (out == nullptr || out_size == 0) {
        return;
    }

    if (!valid) {
        std::snprintf(out, out_size, "--%%");
        return;
    }

    std::snprintf(out, out_size, "%u%%", static_cast<unsigned>(percentage));
}

}  // namespace

void BatteryService::begin(BoardBattery& battery)
{
    battery_ = &battery;
    status_ = {};
    has_cached_percentage_ = false;
    std::snprintf(formatted_percentage_, sizeof(formatted_percentage_), "--%%");
}

BatteryStatus BatteryService::status() const
{
    return status_;
}

bool BatteryService::update(int64_t now_epoch)
{
    char previous[sizeof(formatted_percentage_)];
    std::strncpy(previous, formatted_percentage_, sizeof(previous) - 1);
    previous[sizeof(previous) - 1] = '\0';

    if (battery_ == nullptr) {
        status_.initialized = false;
        status_.valid = has_cached_percentage_;
        format_percentage_value(status_.valid, status_.percentage, formatted_percentage_, sizeof(formatted_percentage_));
        Serial.printf("[battery-service] percent source=%s display=%s cached=%s changed=%s\n",
                      status_.valid ? "cache" : "none",
                      formatted_percentage_,
                      has_cached_percentage_ ? "yes" : "no",
                      std::strcmp(previous, formatted_percentage_) != 0 ? "yes" : "no");
        return std::strcmp(previous, formatted_percentage_) != 0;
    }

    const BoardBatteryStatus board_status = battery_->read();
    status_.initialized = board_status.initialized;
    status_.last_update_epoch = now_epoch;

    if (board_status.valid) {
        status_.valid = true;
        status_.percentage = board_status.percentage;
        status_.voltage_mv = board_status.voltage_mv;
        status_.charging = board_status.state == BoardBatteryState::Charging;
        status_.full = board_status.state == BoardBatteryState::Full;
        status_.low = board_status.percentage <= kLowBatteryPercent;
        has_cached_percentage_ = true;
    } else {
        status_.valid = has_cached_percentage_;
        if (!has_cached_percentage_) {
            status_.percentage = 0;
            status_.voltage_mv = 0;
            status_.charging = false;
            status_.full = false;
            status_.low = false;
        }
    }

    format_percentage_value(status_.valid, status_.percentage, formatted_percentage_, sizeof(formatted_percentage_));
    Serial.printf("[battery-service] percent source=%s display=%s raw_valid=%s initialized=%s voltage=%u charging=%s full=%s low=%s cached=%s changed=%s\n",
                  board_status.valid ? "gauge" : (status_.valid ? "cache" : "none"),
                  formatted_percentage_,
                  board_status.valid ? "yes" : "no",
                  status_.initialized ? "yes" : "no",
                  static_cast<unsigned>(status_.voltage_mv),
                  status_.charging ? "yes" : "no",
                  status_.full ? "yes" : "no",
                  status_.low ? "yes" : "no",
                  has_cached_percentage_ ? "yes" : "no",
                  std::strcmp(previous, formatted_percentage_) != 0 ? "yes" : "no");
    return std::strcmp(previous, formatted_percentage_) != 0;
}

bool BatteryService::format_percentage(char* out, size_t out_size) const
{
    if (out == nullptr || out_size == 0) {
        return false;
    }

    std::snprintf(out, out_size, "%s", formatted_percentage_);
    return status_.valid;
}

}  // namespace paper_screen
