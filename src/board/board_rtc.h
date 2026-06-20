#pragma once

#include <stdint.h>

#include <SensorPCF8563.hpp>

namespace paper_screen {

struct RtcDateTime {
    int year = 0;
    int month = 0;
    int day = 0;
    int hour = 0;
    int minute = 0;
    int second = 0;
};

class BoardRtc {
public:
    bool begin();
    bool initialized() const;
    bool is_valid() const;
    bool read_utc(RtcDateTime* out) const;
    bool write_utc(const RtcDateTime& value);

private:
    static bool valid_datetime(const RtcDateTime& value);

    SensorPCF8563 rtc_;
    bool initialized_ = false;
};

}  // namespace paper_screen
