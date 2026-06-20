#include "board_rtc.h"

#include <Arduino.h>
#include <Wire.h>

namespace {

constexpr int kBoardSda = 39;
constexpr int kBoardScl = 40;
constexpr int kRtcInterrupt = 2;

}  // namespace

namespace paper_screen {

bool BoardRtc::begin()
{
    Serial.println("[rtc] begin");
    pinMode(kRtcInterrupt, INPUT_PULLUP);

    initialized_ = rtc_.begin(Wire, PCF8563_SLAVE_ADDRESS, kBoardSda, kBoardScl);
    if (!initialized_) {
        Serial.println("[rtc] init failed");
        return false;
    }

    Serial.println("[rtc] ready");
    return true;
}

bool BoardRtc::initialized() const
{
    return initialized_;
}

bool BoardRtc::is_valid() const
{
    if (!initialized_) {
        return false;
    }

    RtcDateTime current;
    return read_utc(&current);
}

bool BoardRtc::read_utc(RtcDateTime* out) const
{
    if (!initialized_ || out == nullptr) {
        return false;
    }

    RTC_DateTime current = const_cast<SensorPCF8563&>(rtc_).getDateTime();
    RtcDateTime value;
    value.year = current.year;
    value.month = current.month;
    value.day = current.day;
    value.hour = current.hour;
    value.minute = current.minute;
    value.second = current.second;

    if (!current.available || !valid_datetime(value)) {
        return false;
    }

    *out = value;
    return true;
}

bool BoardRtc::write_utc(const RtcDateTime& value)
{
    if (!initialized_ || !valid_datetime(value)) {
        return false;
    }

    rtc_.setDateTime(static_cast<uint16_t>(value.year),
                     static_cast<uint8_t>(value.month),
                     static_cast<uint8_t>(value.day),
                     static_cast<uint8_t>(value.hour),
                     static_cast<uint8_t>(value.minute),
                     static_cast<uint8_t>(value.second));
    return true;
}

bool BoardRtc::valid_datetime(const RtcDateTime& value)
{
    if (value.year < 2024 || value.year > 2099) {
        return false;
    }
    if (value.month < 1 || value.month > 12) {
        return false;
    }
    if (value.day < 1 || value.day > 31) {
        return false;
    }
    if (value.hour < 0 || value.hour > 23) {
        return false;
    }
    if (value.minute < 0 || value.minute > 59) {
        return false;
    }
    if (value.second < 0 || value.second > 59) {
        return false;
    }
    return true;
}

}  // namespace paper_screen
