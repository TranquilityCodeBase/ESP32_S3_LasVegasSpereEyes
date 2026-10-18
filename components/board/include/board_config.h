// board_config.h - Pin map and panel settings for the Waveshare ESP32-S3-Touch-LCD-2.8C.
// Source: Waveshare ESP-IDF demo (ESP32-S3-Touch-LCD-2.8C-Test, LCD_Driver/ST7701S.h) and the board schematic.
#pragma once

// ---------- Display: 2.8" round 480x480 IPS, ST7701S, 16-bit RGB565 parallel ----------
#define BOARD_LCD_H_RES          480        // Pixels per line
#define BOARD_LCD_V_RES          480        // Lines per frame
#define BOARD_LCD_PCLK_HZ        (18 * 1000 * 1000)   // 18 MHz pixel clock (demo value), about 64 Hz refresh

// RGB timing (demo values)
#define BOARD_LCD_HSYNC_PULSE    8
#define BOARD_LCD_HSYNC_BACK     10
#define BOARD_LCD_HSYNC_FRONT    50
#define BOARD_LCD_VSYNC_PULSE    2
#define BOARD_LCD_VSYNC_BACK     18
#define BOARD_LCD_VSYNC_FRONT    8

// RGB control pins
#define BOARD_PIN_LCD_HSYNC      38
#define BOARD_PIN_LCD_VSYNC      39
#define BOARD_PIN_LCD_DE         40
#define BOARD_PIN_LCD_PCLK       41

// RGB data pins, D0..D15 = B0..B4, G0..G5, R0..R4
#define BOARD_PIN_LCD_DATA { 5, 45, 48, 47, 21,  14, 13, 12, 11, 10, 9,  46, 3, 8, 18, 17 }

// ST7701S 3-wire SPI (init commands only). Shared with the SD card, released after init.
#define BOARD_PIN_LCD_SPI_MOSI   1
#define BOARD_PIN_LCD_SPI_SCLK   2

// ---------- Backlight: GPIO6 -> SI2302 N-MOSFET on the LED cathode (10 ohm series). Active high ----------
#define BOARD_PIN_BACKLIGHT      6
#define BOARD_BACKLIGHT_PWM_HZ   5000       // Demo uses 5 kHz, 13-bit LEDC
// Default brightness in percent. The Waveshare demo boots at 70 %. 50 % is a conservative level
// for running all day: lower LED current and heat, longer LED life, still bright indoors.
#define BOARD_BACKLIGHT_DEFAULT_PERCENT  50
#define BOARD_BACKLIGHT_MAX_PERCENT      100

// ---------- Shared I2C bus: TCA9554 expander (0x20), GT911 touch (0x5D), QMI8658 IMU, PCF85063 RTC ----------
#define BOARD_PIN_I2C_SDA        15
#define BOARD_PIN_I2C_SCL        7
#define BOARD_I2C_HZ             400000
#define BOARD_TCA9554_ADDR       0x20

// TCA9554 expander bits (EXIO1..8 = bit 0..7)
#define BOARD_EXIO_LCD_RST       0          // EXIO1: LCD reset, active low
#define BOARD_EXIO_TP_RST        1          // EXIO2: touch reset, active low
#define BOARD_EXIO_LCD_CS        2          // EXIO3: ST7701S SPI chip select, active low
#define BOARD_EXIO_SD_CS         3          // EXIO4: SD card D3/CS
#define BOARD_EXIO_IMU_INT2      4          // EXIO5: IMU interrupt (input)
#define BOARD_EXIO_IMU_INT1      5          // EXIO6: IMU interrupt (input)
#define BOARD_EXIO_RTC_INT       6          // EXIO7: RTC interrupt (input)
#define BOARD_EXIO_BUZZER        7          // EXIO8: buzzer, active high

// ---------- Free pins on the 12-pin SH1.0 header (J9) ----------
// UART0 TXD/RXD (GPIO43/44) reach the header only when the USB-C UART port is unplugged (FSUSB42 switch).
// GPIO19/20 (native USB D-/D+) are also on J9 and are free while flashing over the CH343 port.
#define BOARD_PIN_HEADER_IO19    19
#define BOARD_PIN_HEADER_IO20    20
