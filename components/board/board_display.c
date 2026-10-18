// board_display.c - ST7701S panel bring-up and double-buffered RGB output for the ESP32-S3-Touch-LCD-2.8C.
// Init order follows the Waveshare demo: expander, panel reset, SPI init commands, RGB panel, backlight.
#include "board_display.h"

#include <string.h>

#include "board_backlight.h"
#include "board_config.h"
#include "st7701_init.h"

#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_rgb.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#define LCD_SPI_HOST        SPI2_HOST
#define LCD_SPI_HZ          4000000         // Demo uses 4 MHz for the 9-bit command interface
#define BOUNCE_LINES        40              // Bounce buffer height in lines. Must divide 480. Bigger = more slack
                                            // for the ISR when PSRAM is busy (10, the demo value, drifted)

static const char *TAG = "display";

static i2c_master_bus_handle_t i2cBus;          // Shared I2C bus
static i2c_master_dev_handle_t expander;        // TCA9554 IO expander
static uint8_t expanderOut;                     // Shadow of the expander output register
static esp_lcd_panel_handle_t panel;            // RGB panel driver
static uint16_t *frameBuffers[2];               // Two full-screen buffers in PSRAM
static int backIndex = 1;                       // Buffer that is not on screen
static SemaphoreHandle_t frameDoneSem;          // Given when the panel finished reading a frame

// ---------- TCA9554 expander ----------

static esp_err_t expanderWrite(uint8_t reg, uint8_t value) {
    uint8_t packet[2] = {reg, value};                                   // Register, value
    return i2c_master_transmit(expander, packet, sizeof(packet), 100);
}

static void expanderSetBit(int bit, bool level) {
    if (level) expanderOut |= (1 << bit);                               // Update the shadow
    else expanderOut &= ~(1 << bit);
    expanderWrite(0x01, expanderOut);                                   // Output port register
}

static esp_err_t expanderInit(void) {
    i2c_master_bus_config_t busConfig = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = BOARD_PIN_I2C_SDA,
        .scl_io_num = BOARD_PIN_I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,                          // Board has 10k pull-ups too
    };
    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&busConfig, &i2cBus), TAG, "i2c bus");
    i2c_device_config_t devConfig = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = BOARD_TCA9554_ADDR,
        .scl_speed_hz = BOARD_I2C_HZ,
    };
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(i2cBus, &devConfig, &expander), TAG, "expander");
    // Outputs first (power-on default is all high, which would sound the buzzer): resets and CS high, buzzer off
    expanderOut = (1 << BOARD_EXIO_LCD_RST) | (1 << BOARD_EXIO_TP_RST) | (1 << BOARD_EXIO_LCD_CS) | (1 << BOARD_EXIO_SD_CS);
    ESP_RETURN_ON_ERROR(expanderWrite(0x01, expanderOut), TAG, "expander out");
    // Direction: 1 = input. Interrupt lines are inputs, the rest outputs
    uint8_t inputs = (1 << BOARD_EXIO_IMU_INT2) | (1 << BOARD_EXIO_IMU_INT1) | (1 << BOARD_EXIO_RTC_INT);
    return expanderWrite(0x03, inputs);                                 // Configuration register
}

// ---------- ST7701S command interface (3-wire SPI, 9 bits: D/C bit + byte) ----------

static void lcdSpiSend(spi_device_handle_t spi, bool isData, uint8_t value) {
    spi_transaction_t t = {
        .cmd = isData ? 1 : 0,          // 1-bit command phase carries D/C
        .addr = value,                  // 8-bit address phase carries the byte
    };
    spi_device_polling_transmit(spi, &t);
}

static esp_err_t panelSendInitCommands(void) {
    spi_bus_config_t busConfig = {
        .mosi_io_num = BOARD_PIN_LCD_SPI_MOSI,
        .miso_io_num = -1,
        .sclk_io_num = BOARD_PIN_LCD_SPI_SCLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
    };
    ESP_RETURN_ON_ERROR(spi_bus_initialize(LCD_SPI_HOST, &busConfig, SPI_DMA_DISABLED), TAG, "spi bus");
    spi_device_interface_config_t devConfig = {
        .command_bits = 1,
        .address_bits = 8,
        .clock_speed_hz = LCD_SPI_HZ,
        .mode = 0,
        .spics_io_num = -1,             // CS is on the expander
        .queue_size = 1,
    };
    spi_device_handle_t spi;
    ESP_RETURN_ON_ERROR(spi_bus_add_device(LCD_SPI_HOST, &devConfig, &spi), TAG, "spi dev");

    expanderSetBit(BOARD_EXIO_LCD_RST, false);                         // Hardware reset pulse
    vTaskDelay(pdMS_TO_TICKS(10));
    expanderSetBit(BOARD_EXIO_LCD_RST, true);
    vTaskDelay(pdMS_TO_TICKS(120));                                    // Wait for the controller to boot
    expanderSetBit(BOARD_EXIO_LCD_CS, false);                          // Select the panel

    for (size_t i = 0; i < sizeof(st7701InitCmds) / sizeof(st7701InitCmds[0]); i++) {
        const st7701_cmd_t *c = &st7701InitCmds[i];
        lcdSpiSend(spi, false, c->cmd);                                // Register
        for (int b = 0; b < c->len; b++) lcdSpiSend(spi, true, c->data[b]);   // Parameters
        if (c->delayMs) vTaskDelay(pdMS_TO_TICKS(c->delayMs));
    }

    expanderSetBit(BOARD_EXIO_LCD_CS, true);                           // Deselect
    spi_bus_remove_device(spi);                                        // Free GPIO1/2 for the SD card later
    spi_bus_free(LCD_SPI_HOST);
    return ESP_OK;
}

// ---------- RGB panel ----------

// Panel finished reading a frame: the buffer it switched away from is free
static IRAM_ATTR bool onFrameDone(esp_lcd_panel_handle_t p, const esp_lcd_rgb_panel_event_data_t *e, void *ctx) {
    BaseType_t woken = pdFALSE;
    xSemaphoreGiveFromISR(frameDoneSem, &woken);
    return woken == pdTRUE;
}

static esp_err_t rgbPanelInit(void) {
    esp_lcd_rgb_panel_config_t config = {
        .clk_src = LCD_CLK_SRC_DEFAULT,
        .data_width = 16,                                               // RGB565
        .num_fbs = 2,                                                   // Page flipping, no tearing
        .bounce_buffer_size_px = BOUNCE_LINES * BOARD_LCD_H_RES,        // SRAM bounce buffers: steady output from PSRAM
        .dma_burst_size = 64,
        .hsync_gpio_num = BOARD_PIN_LCD_HSYNC,
        .vsync_gpio_num = BOARD_PIN_LCD_VSYNC,
        .de_gpio_num = BOARD_PIN_LCD_DE,
        .pclk_gpio_num = BOARD_PIN_LCD_PCLK,
        .disp_gpio_num = -1,
        .data_gpio_nums = BOARD_PIN_LCD_DATA,
        .timings = {
            .pclk_hz = BOARD_LCD_PCLK_HZ,
            .h_res = BOARD_LCD_H_RES,
            .v_res = BOARD_LCD_V_RES,
            .hsync_pulse_width = BOARD_LCD_HSYNC_PULSE,
            .hsync_back_porch = BOARD_LCD_HSYNC_BACK,
            .hsync_front_porch = BOARD_LCD_HSYNC_FRONT,
            .vsync_pulse_width = BOARD_LCD_VSYNC_PULSE,
            .vsync_back_porch = BOARD_LCD_VSYNC_BACK,
            .vsync_front_porch = BOARD_LCD_VSYNC_FRONT,
        },
        .flags.fb_in_psram = true,
        .flags.bb_invalidate_cache = true,                              // Panel reads don't evict the renderer's cache lines
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_rgb_panel(&config, &panel), TAG, "rgb panel");

    frameDoneSem = xSemaphoreCreateBinary();
    esp_lcd_rgb_panel_event_callbacks_t callbacks = {
        .on_frame_buf_complete = onFrameDone,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_rgb_panel_register_event_callbacks(panel, &callbacks, NULL), TAG, "callbacks");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(panel), TAG, "reset");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(panel), TAG, "init");
    void *fb0, *fb1;
    ESP_RETURN_ON_ERROR(esp_lcd_rgb_panel_get_frame_buffer(panel, 2, &fb0, &fb1), TAG, "fbs");
    frameBuffers[0] = fb0;
    frameBuffers[1] = fb1;
    return ESP_OK;
}

// ---------- Public API ----------

esp_err_t boardDisplayInit(void) {
    ESP_RETURN_ON_ERROR(expanderInit(), TAG, "expander init");
    ESP_RETURN_ON_ERROR(panelSendInitCommands(), TAG, "panel init");
    ESP_RETURN_ON_ERROR(rgbPanelInit(), TAG, "rgb init");
    // Both buffers start black so nothing random shows before the first frame
    memset(frameBuffers[0], 0, BOARD_LCD_H_RES * BOARD_LCD_V_RES * sizeof(uint16_t));
    memset(frameBuffers[1], 0, BOARD_LCD_H_RES * BOARD_LCD_V_RES * sizeof(uint16_t));
    ESP_RETURN_ON_ERROR(boardBacklightInit(BOARD_BACKLIGHT_DEFAULT_PERCENT), TAG, "backlight");
    ESP_LOGI(TAG, "Panel %dx%d ready", BOARD_LCD_H_RES, BOARD_LCD_V_RES);
    return ESP_OK;
}

uint16_t *boardDisplayBackBuffer(void) {
    return frameBuffers[backIndex];
}

uint16_t *boardDisplayFrontBuffer(void) {
    return frameBuffers[backIndex ^ 1];
}

void boardDisplayFlip(void) {
    esp_lcd_panel_draw_bitmap(panel, 0, 0, BOARD_LCD_H_RES, BOARD_LCD_V_RES, frameBuffers[backIndex]);  // Queue the switch
    xSemaphoreTake(frameDoneSem, 0);                                   // Drop a signal from before the switch was queued
    xSemaphoreTake(frameDoneSem, pdMS_TO_TICKS(100));                  // Next frame end: switch done, old front is free
    backIndex ^= 1;                                                    // Old front becomes the new back
}

i2c_master_bus_handle_t boardI2cBus(void) {
    return i2cBus;
}
