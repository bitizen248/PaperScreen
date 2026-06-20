#include "board_battery.h"

#include <Arduino.h>
#include <Wire.h>

#include "bq27220.h"

namespace {

constexpr uint8_t kBq27220Address = BQ27220_I2C_ADDRESS;
constexpr uint16_t kBq27220DeviceId = BQ27220_DEVICE_ID;
constexpr uint8_t kMaxConsecutiveReadFailures = 3;
BQ27220 g_gauge;

bool i2c_device_present(uint8_t address)
{
    Wire.beginTransmission(address);
    return Wire.endTransmission() == 0;
}

bool read_u16(uint8_t reg, uint16_t* out)
{
    if (out == nullptr) {
        return false;
    }

    Wire.beginTransmission(kBq27220Address);
    Wire.write(reg);
    const uint8_t tx_error = Wire.endTransmission(false);
    if (tx_error != 0) {
        Serial.printf("[battery] read reg=0x%02x tx_error=%u\n", reg, tx_error);
        return false;
    }

    const uint8_t received = Wire.requestFrom(kBq27220Address, static_cast<uint8_t>(2));
    if (received != 2) {
        Serial.printf("[battery] read reg=0x%02x short_read=%u\n", reg, received);
        while (Wire.available() > 0) {
            Wire.read();
        }
        return false;
    }

    const int lo = Wire.read();
    const int hi = Wire.read();
    if (lo < 0 || hi < 0) {
        Serial.printf("[battery] read reg=0x%02x invalid byte lo=%d hi=%d\n", reg, lo, hi);
        return false;
    }

    *out = static_cast<uint16_t>((static_cast<uint16_t>(hi) << 8) | static_cast<uint16_t>(lo));
    return true;
}

bool note_read_failure(uint8_t* consecutive_failures)
{
    if (consecutive_failures == nullptr) {
        return false;
    }
    ++(*consecutive_failures);
    Serial.printf("[battery] read failed failures=%u\n", *consecutive_failures);
    if (*consecutive_failures >= kMaxConsecutiveReadFailures) {
        Serial.println("[battery] disabling fuel gauge after repeated read failures");
        return true;
    }
    return false;
}

}  // namespace

namespace paper_screen {

bool BoardBattery::begin()
{
    Serial.println("[battery] begin");

    if (!i2c_device_present(kBq27220Address)) {
        initialized_ = false;
        consecutive_failures_ = 0;
        Serial.println("[battery] fuel gauge not found");
        return false;
    }

    const uint16_t device_id = g_gauge.getDeviceNumber();
    if (device_id != kBq27220DeviceId) {
        initialized_ = false;
        consecutive_failures_ = 0;
        Serial.printf("[battery] unexpected fuel gauge id=0x%04x\n", device_id);
        return false;
    }

    initialized_ = true;
    consecutive_failures_ = 0;
    Serial.println("[battery] ready");
    return true;
}

BoardBatteryStatus BoardBattery::read()
{
    BoardBatteryStatus status;
    status.initialized = initialized_;
    if (!initialized_) {
        return status;
    }

    if (!i2c_device_present(kBq27220Address)) {
        ++consecutive_failures_;
        Serial.printf("[battery] fuel gauge not responding failures=%u\n", consecutive_failures_);
        if (consecutive_failures_ >= kMaxConsecutiveReadFailures) {
            initialized_ = false;
            Serial.println("[battery] disabling fuel gauge after repeated read failures");
        }
        return status;
    }

    uint16_t battery_status_raw = 0;
    uint16_t soc = 0;
    uint16_t voltage = 0;
    uint16_t current_raw = 0;
    if (!read_u16(CommandBatteryStatus, &battery_status_raw)
        || !read_u16(CommandStateOfCharge, &soc)
        || !read_u16(CommandVoltage, &voltage)
        || !read_u16(CommandCurrent, &current_raw)) {
        if (note_read_failure(&consecutive_failures_)) {
            initialized_ = false;
        }
        return status;
    }

    BQ27220BatteryStatus battery_status;
    battery_status.full = battery_status_raw;
    const int16_t current = static_cast<int16_t>(current_raw);

    if (soc > 100 || voltage == 0 || voltage > 5000) {
        Serial.printf("[battery] invalid raw percent soc=%u voltage=%u current=%d status=0x%04x\n",
                      soc,
                      voltage,
                      current,
                      battery_status.full);
        if (note_read_failure(&consecutive_failures_)) {
            initialized_ = false;
        }
        return status;
    }

    status.present = battery_status.reg.BATTPRES || soc > 0 || voltage > 0;
    status.valid = status.present;
    status.percentage = static_cast<uint8_t>(soc > 100 ? 100 : soc);
    status.voltage_mv = voltage;
    status.current_ma = current;
    consecutive_failures_ = 0;

    if (battery_status.reg.FC || (soc >= 100 && current == 0)) {
        status.state = BoardBatteryState::Full;
    } else if (battery_status.reg.CHGING || !battery_status.reg.DSG) {
        status.state = BoardBatteryState::Charging;
    } else if (status.present) {
        status.state = BoardBatteryState::Discharging;
    }

    Serial.printf("[battery] raw status=0x%04x flags DSG=%u CHGING=%u FC=%u BATTPRES=%u\n",
                  battery_status.full,
                  static_cast<unsigned>(battery_status.reg.DSG),
                  static_cast<unsigned>(battery_status.reg.CHGING),
                  static_cast<unsigned>(battery_status.reg.FC),
                  static_cast<unsigned>(battery_status.reg.BATTPRES));
    Serial.printf("[battery] soc=%u voltage=%u current=%d state=%d\n",
                  status.percentage,
                  status.voltage_mv,
                  status.current_ma,
                  static_cast<int>(status.state));
    return status;
}

}  // namespace paper_screen
