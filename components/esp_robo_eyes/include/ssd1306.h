#ifndef SSD1306_H
#define SSD1306_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "driver/i2c.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SSD1306_WIDTH  128
#define SSD1306_HEIGHT 64
#define SSD1306_BUFFER_SIZE (SSD1306_WIDTH * SSD1306_HEIGHT / 8)

typedef enum {
    OLED_TYPE_SSD1306 = 0,
    OLED_TYPE_SH1106  = 1  // 1.3" OLEDs often use SH1106 with 2-pixel column offset
} oled_controller_type_t;

typedef struct {
    i2c_port_t i2c_port;
    gpio_num_t sda_pin;
    gpio_num_t scl_pin;
    uint32_t clk_speed_hz;
    uint8_t i2c_addr;
    oled_controller_type_t controller;
} ssd1306_config_t;

typedef struct {
    ssd1306_config_t config;
    uint8_t buffer[SSD1306_BUFFER_SIZE];
} ssd1306_t;

esp_err_t ssd1306_init(ssd1306_t *dev, const ssd1306_config_t *cfg);
void ssd1306_clear(ssd1306_t *dev);
esp_err_t ssd1306_display(ssd1306_t *dev);

void ssd1306_draw_pixel(ssd1306_t *dev, int16_t x, int16_t y, uint8_t color);
void ssd1306_draw_fast_hline(ssd1306_t *dev, int16_t x, int16_t y, int16_t w, uint8_t color);
void ssd1306_draw_fast_vline(ssd1306_t *dev, int16_t x, int16_t y, int16_t h, uint8_t color);
void ssd1306_fill_rect(ssd1306_t *dev, int16_t x, int16_t y, int16_t w, int16_t h, uint8_t color);
void ssd1306_fill_round_rect(ssd1306_t *dev, int16_t x, int16_t y, int16_t w, int16_t h, int16_t r, uint8_t color);
void ssd1306_fill_triangle(ssd1306_t *dev, int16_t x0, int16_t y0, int16_t x1, int16_t y1, int16_t x2, int16_t y2, uint8_t color);

#ifdef __cplusplus
}
#endif

#endif // SSD1306_H

