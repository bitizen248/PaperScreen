#include "board.h"

#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>

extern "C" {
#include "board/pca9555.h"
}

namespace {

constexpr int kBoardSda = 39;
constexpr int kBoardScl = 40;
constexpr int kSpiSclk = 14;
constexpr int kSpiMiso = 21;
constexpr int kSpiMosi = 13;
constexpr int kSdCs = 12;
constexpr int kLoraCs = 46;
constexpr int kBoardBacklightEnable = 11;
constexpr int kBootButton = 0;
constexpr int kRtcInterrupt = 2;
constexpr int kTouchInterrupt = 3;
constexpr int kTouchReset = 9;
constexpr int kPcaInterrupt = 38;
constexpr int kI2cPort = 0;
constexpr uint8_t kPcaPort1FunctionButtonMask = 0x04;
constexpr uint8_t kGt911Address = GT911_SLAVE_ADDRESS_L;

void log_level_change(const char* name, int& previous, int current)
{
    if (previous == current) {
        return;
    }

    Serial.printf("[pwr-diag] %s=%d\n", name, current);
    previous = current;
}

void log_hex_change(const char* name, int& previous, int current)
{
    if (previous == current) {
        return;
    }

    Serial.printf("[pwr-diag] %s=0x%02x\n", name, current);
    previous = current;
}

}  // namespace

namespace paper_screen {

BoardStatus Board::begin()
{
    Serial.println("[board] begin");

    Serial.println("[board] serial ready");

    Serial.printf("[board] i2c begin sda=%d scl=%d\n", kBoardSda, kBoardScl);
    Wire.begin(kBoardSda, kBoardScl);

    log_i2c_devices();

    Serial.printf("[board] shared spi cs idle sd=%d lora=%d\n", kSdCs, kLoraCs);
    pinMode(kSdCs, OUTPUT);
    digitalWrite(kSdCs, HIGH);
    pinMode(kLoraCs, OUTPUT);
    digitalWrite(kLoraCs, HIGH);

    Serial.printf("[board] spi begin sclk=%d miso=%d mosi=%d\n", kSpiSclk, kSpiMiso, kSpiMosi);
    SPI.begin(kSpiSclk, kSpiMiso, kSpiMosi);

    Serial.printf("[board] backlight pin=%d off\n", kBoardBacklightEnable);
    pinMode(kBoardBacklightEnable, OUTPUT);
    digitalWrite(kBoardBacklightEnable, LOW);

    pinMode(kBootButton, INPUT_PULLUP);
    pinMode(kRtcInterrupt, INPUT_PULLUP);
    pinMode(kTouchInterrupt, INPUT_PULLUP);
    pinMode(kPcaInterrupt, INPUT_PULLUP);

    Serial.printf("[board] touch begin gt911 rst=%d int=%d addr=0x%02x\n", kTouchReset, kTouchInterrupt, kGt911Address);
    touch_.setPins(kTouchReset, kTouchInterrupt);
    status_.touch_initialized = touch_.begin(Wire, kGt911Address, kBoardSda, kBoardScl);
    if (status_.touch_initialized) {
        touch_.setHomeButtonCallback(handle_touch_home_button, this);
        touch_.setInterruptMode(LOW_LEVEL_QUERY);
        Serial.println("[board] touch ready");
    } else {
        Serial.println("[board] touch init failed");
    }

    Serial.println("[pwr-diag] watching BOOT=GPIO0 RTC_INT=GPIO2 TOUCH_INT=GPIO3 PCA_INT=GPIO38");
    Serial.println("[pwr-diag] watching PCA9535 port0/port1; press PWR and check for changed lines");

    status_.state = BoardInitState::Ready;
    status_.hardware_initialized = true;

    Serial.println("[board] ready");
    return status_;
}

BoardStatus Board::status() const
{
    return status_;
}

bool Board::boot_button_pressed() const
{
    return digitalRead(kBootButton) == LOW;
}

bool Board::consume_home_button_pressed()
{
    if (!home_button_pressed_) {
        return false;
    }

    home_button_pressed_ = false;
    return true;
}

bool Board::function_button_pressed() const
{
    const uint8_t pca_port1 = pca9555_read_input(kI2cPort, 1);
    return (pca_port1 & kPcaPort1FunctionButtonMask) == 0;
}

void Board::clear_function_button_interrupt() const
{
    pca9555_read_input(kI2cPort, 0);
    pca9555_read_input(kI2cPort, 1);
}

void Board::poll_input()
{
    const TouchPoint point = read_touch_point();
    if (point.pressed) {
        if (!touch_pressed_) {
            touch_pressed_ = true;
            last_touch_ = point;
            push_input_event(BoardInputEventType::TouchDown, point);
            return;
        }

        if (point.x != last_touch_.x || point.y != last_touch_.y) {
            last_touch_ = point;
            push_input_event(BoardInputEventType::TouchMove, point);
        }
        return;
    }

    if (!touch_pressed_) {
        return;
    }

    touch_pressed_ = false;
    TouchPoint released = last_touch_;
    released.pressed = false;
    push_input_event(BoardInputEventType::TouchUp, released);
}

bool Board::next_input_event(BoardInputEvent& event)
{
    if (input_queue_head_ == input_queue_tail_) {
        event = {};
        return false;
    }

    event = input_queue_[input_queue_tail_];
    input_queue_tail_ = (input_queue_tail_ + 1) % kInputQueueSize;
    return true;
}

void Board::push_input_event(BoardInputEventType type, TouchPoint touch)
{
    const uint8_t next_head = (input_queue_head_ + 1) % kInputQueueSize;
    if (next_head == input_queue_tail_) {
        Serial.println("[board] input queue full; dropping oldest event");
        input_queue_tail_ = (input_queue_tail_ + 1) % kInputQueueSize;
    }

    input_queue_[input_queue_head_].type = type;
    input_queue_[input_queue_head_].touch = touch;
    input_queue_head_ = next_head;
}

TouchPoint Board::read_touch_point()
{
    TouchPoint point;
    if (!status_.touch_initialized || !touch_.isPressed()) {
        return point;
    }

    int16_t x = 0;
    int16_t y = 0;
    if (touch_.getPoint(&x, &y, 1) == 0) {
        return point;
    }

    point.x = x;
    point.y = y;
    point.pressed = true;
    return point;
}

void Board::set_backlight_enabled(bool enabled)
{
    digitalWrite(kBoardBacklightEnable, enabled ? HIGH : LOW);
    Serial.printf("[board] backlight %s\n", enabled ? "on" : "off");
}

void Board::poll_power_button_diagnostics()
{
    const unsigned long now = millis();
    if (now - last_power_diag_poll_ms_ < 200) {
        return;
    }
    last_power_diag_poll_ms_ = now;

    log_level_change("BOOT_GPIO0", last_boot_level_, digitalRead(kBootButton));
    log_level_change("RTC_INT_GPIO2", last_rtc_int_level_, digitalRead(kRtcInterrupt));
    log_level_change("TOUCH_INT_GPIO3", last_touch_int_level_, digitalRead(kTouchInterrupt));
    log_level_change("PCA_INT_GPIO38", last_pca_int_level_, digitalRead(kPcaInterrupt));

    const int pca_port0 = pca9555_read_input(kI2cPort, 0);
    const int pca_port1 = pca9555_read_input(kI2cPort, 1);
    log_hex_change("PCA9535_PORT0", last_pca_port0_, pca_port0);
    log_hex_change("PCA9535_PORT1", last_pca_port1_, pca_port1);
}

void Board::log_i2c_devices()
{
    Serial.println("[pwr-diag] i2c scan begin");
    for (uint8_t address = 1; address < 127; ++address) {
        Wire.beginTransmission(address);
        const uint8_t error = Wire.endTransmission();
        if (error == 0) {
            Serial.printf("[pwr-diag] i2c device 0x%02x\n", address);
        }
    }
    Serial.println("[pwr-diag] i2c scan done");
}

void Board::handle_touch_home_button(void* user_data)
{
    auto* board = static_cast<Board*>(user_data);
    if (board == nullptr) {
        return;
    }

    Serial.println("[board] gt911 home button pressed");
    board->home_button_pressed_ = true;
}

}  // namespace paper_screen
