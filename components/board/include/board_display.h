// board_display.h - Brings up the ST7701S round panel with two PSRAM frame buffers and vsync-safe page flipping.
#pragma once

#include <stdint.h>
#include "esp_err.h"
#include "driver/i2c_master.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t boardDisplayInit(void);               // I2C + expander, ST7701S init over SPI, RGB panel, backlight
uint16_t *boardDisplayBackBuffer(void);         // Frame buffer not on screen. Draw here, then flip
uint16_t *boardDisplayFrontBuffer(void);        // Frame buffer on screen now (read only, for debugging)
void boardDisplayFlip(void);                    // Show the back buffer. Returns once the old front buffer is free
i2c_master_bus_handle_t boardI2cBus(void);      // Shared I2C bus (touch, IMU, RTC) for later drivers

#ifdef __cplusplus
}
#endif
