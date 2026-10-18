// board_backlight.h - LCD backlight PWM (LEDC on GPIO6).
#pragma once

#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t boardBacklightInit(uint8_t percent);  // Start PWM at this brightness (clamped to the max in board_config.h)
void boardBacklightSet(uint8_t percent);        // 0 = off .. 100 = full

#ifdef __cplusplus
}
#endif
