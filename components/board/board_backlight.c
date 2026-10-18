// board_backlight.c - LCD backlight PWM. 5 kHz, 13-bit LEDC, active high (see board_config.h).
#include "board_backlight.h"

#include "board_config.h"
#include "driver/ledc.h"
#include "esp_check.h"
#include "esp_log.h"

#define BL_SPEED_MODE   LEDC_LOW_SPEED_MODE
#define BL_TIMER        LEDC_TIMER_0
#define BL_CHANNEL      LEDC_CHANNEL_0
#define BL_RESOLUTION   LEDC_TIMER_13_BIT
#define BL_DUTY_MAX     ((1 << 13) - 1)     // 8191 = always on

static const char *TAG = "backlight";

void boardBacklightSet(uint8_t percent) {
    if (percent > BOARD_BACKLIGHT_MAX_PERCENT) percent = BOARD_BACKLIGHT_MAX_PERCENT;  // Respect the cap
    uint32_t duty = (uint32_t)BL_DUTY_MAX * percent / 100;                             // Linear in percent
    ledc_set_duty(BL_SPEED_MODE, BL_CHANNEL, duty);
    ledc_update_duty(BL_SPEED_MODE, BL_CHANNEL);
}

esp_err_t boardBacklightInit(uint8_t percent) {
    ledc_timer_config_t timerConfig = {
        .speed_mode = BL_SPEED_MODE,
        .duty_resolution = BL_RESOLUTION,
        .timer_num = BL_TIMER,
        .freq_hz = BOARD_BACKLIGHT_PWM_HZ,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ESP_RETURN_ON_ERROR(ledc_timer_config(&timerConfig), TAG, "timer");
    ledc_channel_config_t channelConfig = {
        .gpio_num = BOARD_PIN_BACKLIGHT,
        .speed_mode = BL_SPEED_MODE,
        .channel = BL_CHANNEL,
        .timer_sel = BL_TIMER,
        .duty = 0,                          // Start dark, set below
    };
    ESP_RETURN_ON_ERROR(ledc_channel_config(&channelConfig), TAG, "channel");
    boardBacklightSet(percent);
    ESP_LOGI(TAG, "Backlight %u %%", percent);
    return ESP_OK;
}
