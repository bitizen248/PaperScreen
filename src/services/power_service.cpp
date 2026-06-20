#include "power_service.h"

#include <Arduino.h>
#include <esp_sleep.h>
#include <driver/rtc_io.h>

namespace {

constexpr gpio_num_t kBootWakeGpio = GPIO_NUM_0;

void configure_boot_wake()
{
    Serial.println("[power] configure BOOT GPIO0 wake low");
    pinMode(static_cast<uint8_t>(kBootWakeGpio), INPUT_PULLUP);
    rtc_gpio_pullup_en(kBootWakeGpio);
    rtc_gpio_pulldown_dis(kBootWakeGpio);
    esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON);

    const esp_err_t err = esp_sleep_enable_ext0_wakeup(kBootWakeGpio, 0);
    Serial.printf("[power] ext0 wake result=%d\n", static_cast<int>(err));
}

}  // namespace

namespace paper_screen {

bool PowerService::woke_from_timer() const
{
    return esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_TIMER;
}

void PowerService::enter_deep_sleep()
{
    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
    configure_boot_wake();

    Serial.println("[power] entering deep sleep");
    Serial.flush();
    delay(50);
    esp_deep_sleep_start();
}

void PowerService::enter_deep_sleep_for(uint32_t seconds)
{
    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
    configure_boot_wake();

    const uint64_t sleep_us = static_cast<uint64_t>(seconds) * 1000000ULL;
    const esp_err_t err = esp_sleep_enable_timer_wakeup(sleep_us);
    Serial.printf("[power] timer wake seconds=%lu result=%d\n",
                  static_cast<unsigned long>(seconds),
                  static_cast<int>(err));

    Serial.println("[power] entering scheduled deep sleep");
    Serial.flush();
    delay(50);
    esp_deep_sleep_start();
}

}  // namespace paper_screen
