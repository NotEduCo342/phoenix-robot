#ifndef ST7735_H
#define ST7735_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"

#ifdef __cplusplus
extern "C" {
#endif

// 1.8" TFT in Landscape orientation
#define ST7735_WIDTH  160
#define ST7735_HEIGHT 128
#define ST7735_PIXELS (ST7735_WIDTH * ST7735_HEIGHT)

// 16-bit RGB565 Color Constants
#define COLOR_BLACK       0x0000
#define COLOR_WHITE       0xFFFF
#define COLOR_RED         0xF800
#define COLOR_GREEN       0x07E0
#define COLOR_BLUE        0x001F
#define COLOR_CYAN        0x07FF
#define COLOR_MAGENTA     0xF81F
#define COLOR_YELLOW      0xFFE0
#define COLOR_ORANGE      0xFD20
#define COLOR_PINK        0xF814
#define COLOR_SOFT_BLUE   0x03EF
#define COLOR_EMERALD     0x066A

typedef struct {
    spi_host_device_t spi_host;
    gpio_num_t mosi_pin;
    gpio_num_t sclk_pin;
    gpio_num_t cs_pin;
    gpio_num_t dc_pin;
    gpio_num_t rst_pin;
    gpio_num_t bl_pin;
    int col_offset;
    int row_offset;
    bool bgr_order;
} st7735_config_t;

typedef struct {
    st7735_config_t config;
    spi_device_handle_t spi_dev;
    uint16_t *buffer; // DMA-capable framebuffer: 160 * 128 * 2 bytes = 40 KB
} st7735_t;

esp_err_t st7735_init(st7735_t *dev, const st7735_config_t *cfg);
void st7735_set_backlight(st7735_t *dev, bool enable);
void st7735_clear(st7735_t *dev, uint16_t color);
esp_err_t st7735_display(st7735_t *dev);

void st7735_draw_pixel(st7735_t *dev, int16_t x, int16_t y, uint16_t color);
void st7735_draw_fast_hline(st7735_t *dev, int16_t x, int16_t y, int16_t w, uint16_t color);
void st7735_draw_fast_vline(st7735_t *dev, int16_t x, int16_t y, int16_t h, uint16_t color);
void st7735_draw_rect(st7735_t *dev, int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color);
void st7735_fill_rect(st7735_t *dev, int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color);
void st7735_draw_round_rect(st7735_t *dev, int16_t x, int16_t y, int16_t w, int16_t h, int16_t r, uint16_t color);
void st7735_fill_round_rect(st7735_t *dev, int16_t x, int16_t y, int16_t w, int16_t h, int16_t r, uint16_t color);
void st7735_draw_circle(st7735_t *dev, int16_t x0, int16_t y0, int16_t r, uint16_t color);
void st7735_fill_circle(st7735_t *dev, int16_t x0, int16_t y0, int16_t r, uint16_t color);
void st7735_fill_triangle(st7735_t *dev, int16_t x0, int16_t y0, int16_t x1, int16_t y1, int16_t x2, int16_t y2, uint16_t color);

void st7735_draw_char(st7735_t *dev, int16_t x, int16_t y, char c, uint16_t color, uint16_t bg, uint8_t size);
void st7735_draw_string(st7735_t *dev, int16_t x, int16_t y, const char *str, uint16_t color, uint16_t bg, uint8_t size);
void st7735_draw_bitmap(st7735_t *dev, int16_t x, int16_t y, const uint8_t *bitmap, int16_t w, int16_t h, uint16_t color, uint16_t bg);

#ifdef __cplusplus
}
#endif

#endif // ST7735_H

