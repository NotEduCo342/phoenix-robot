#include "ssd1306.h"
#include <string.h>
#include "esp_log.h"

static const char *TAG = "ssd1306";

#define SWAP_INT16(a, b) do { int16_t t = a; a = b; b = t; } while (0)

static esp_err_t ssd1306_send_cmd(ssd1306_t *dev, uint8_t cmd) {
    i2c_cmd_handle_t handle = i2c_cmd_link_create();
    i2c_master_start(handle);
    i2c_master_write_byte(handle, (dev->config.i2c_addr << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(handle, 0x00, true); // Control byte: Command
    i2c_master_write_byte(handle, cmd, true);
    i2c_master_stop(handle);
    esp_err_t ret = i2c_master_cmd_begin(dev->config.i2c_port, handle, pdMS_TO_TICKS(50));
    i2c_cmd_link_delete(handle);
    return ret;
}

static esp_err_t ssd1306_send_cmds(ssd1306_t *dev, const uint8_t *cmds, size_t count) {
    for (size_t i = 0; i < count; i++) {
        esp_err_t err = ssd1306_send_cmd(dev, cmds[i]);
        if (err != ESP_OK) return err;
    }
    return ESP_OK;
}

esp_err_t ssd1306_init(ssd1306_t *dev, const ssd1306_config_t *cfg) {
    if (!dev || !cfg) return ESP_ERR_INVALID_ARG;
    dev->config = *cfg;
    if (dev->config.i2c_addr == 0) dev->config.i2c_addr = 0x3C;
    if (dev->config.clk_speed_hz == 0) dev->config.clk_speed_hz = 400000;

    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = dev->config.sda_pin,
        .scl_io_num = dev->config.scl_pin,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = dev->config.clk_speed_hz,
        .clk_flags = 0
    };

    esp_err_t ret = i2c_param_config(dev->config.i2c_port, &conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2C param config failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = i2c_driver_install(dev->config.i2c_port, conf.mode, 0, 0, 0);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) { // Already installed is ok
        ESP_LOGE(TAG, "I2C driver install failed: %s", esp_err_to_name(ret));
        return ret;
    }

    // Standard initialization commands for SSD1306 and SH1106
    const uint8_t init_cmds[] = {
        0xAE,         // Display OFF
        0xD5, 0x80,   // Set Display Clock Divide Ratio
        0xA8, 0x3F,   // Set Multiplex Ratio (64 lines)
        0xD3, 0x00,   // Set Display Offset
        0x40,         // Set Start Line (0)
        0x8D, 0x14,   // Charge pump enable
        0x20, 0x02,   // Memory addressing mode: Page addressing mode (compatible with both SSD1306 and SH1106)
        0xA1,         // Segment re-map (col 127 = SEG0)
        0xC8,         // COM Output Scan Direction
        0xDA, 0x12,   // COM pins config
        0x81, 0xCF,   // Set Contrast
        0xD9, 0xF1,   // Pre-charge Period
        0xDB, 0x40,   // VCOMH Deselect Level
        0xA4,         // Output follows RAM
        0xA6,         // Normal display (non-inverted)
        0xAF          // Display ON
    };

    ret = ssd1306_send_cmds(dev, init_cmds, sizeof(init_cmds));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Display init command failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ssd1306_clear(dev);
    return ssd1306_display(dev);
}

void ssd1306_clear(ssd1306_t *dev) {
    if (!dev) return;
    memset(dev->buffer, 0, SSD1306_BUFFER_SIZE);
}

esp_err_t ssd1306_display(ssd1306_t *dev) {
    if (!dev) return ESP_ERR_INVALID_ARG;

    // SH1106 has a 2-pixel column offset (132x64 total internal RAM)
    uint8_t col_offset = (dev->config.controller == OLED_TYPE_SH1106) ? 0x02 : 0x00;

    for (uint8_t page = 0; page < 8; page++) {
        // Set page and column address
        uint8_t page_cmds[] = {
            (uint8_t)(0xB0 + page),                      // Set Page Address
            (uint8_t)(0x00 | (col_offset & 0x0F)),        // Set Lower Column Address
            (uint8_t)(0x10 | ((col_offset >> 4) & 0x0F)) // Set Higher Column Address
        };
        esp_err_t err = ssd1306_send_cmds(dev, page_cmds, sizeof(page_cmds));
        if (err != ESP_OK) return err;

        // Send 128 bytes of pixel data for this page
        i2c_cmd_handle_t handle = i2c_cmd_link_create();
        i2c_master_start(handle);
        i2c_master_write_byte(handle, (dev->config.i2c_addr << 1) | I2C_MASTER_WRITE, true);
        i2c_master_write_byte(handle, 0x40, true); // Control byte: Data
        i2c_master_write(handle, &dev->buffer[page * SSD1306_WIDTH], SSD1306_WIDTH, true);
        i2c_master_stop(handle);
        err = i2c_master_cmd_begin(dev->config.i2c_port, handle, pdMS_TO_TICKS(50));
        i2c_cmd_link_delete(handle);
        if (err != ESP_OK) return err;
    }
    return ESP_OK;
}

void ssd1306_draw_pixel(ssd1306_t *dev, int16_t x, int16_t y, uint8_t color) {
    if (!dev || x < 0 || x >= SSD1306_WIDTH || y < 0 || y >= SSD1306_HEIGHT) return;
    size_t idx = (size_t)x + ((size_t)(y / 8) * SSD1306_WIDTH);
    uint8_t bit = y % 8;
    if (color) {
        dev->buffer[idx] |= (1 << bit);
    } else {
        dev->buffer[idx] &= ~(1 << bit);
    }
}

void ssd1306_draw_fast_hline(ssd1306_t *dev, int16_t x, int16_t y, int16_t w, uint8_t color) {
    if (!dev || y < 0 || y >= SSD1306_HEIGHT || w <= 0) return;
    if (x < 0) { w += x; x = 0; }
    if (x + w > SSD1306_WIDTH) { w = SSD1306_WIDTH - x; }
    if (w <= 0) return;

    uint8_t *p = &dev->buffer[x + (y / 8) * SSD1306_WIDTH];
    uint8_t mask = 1 << (y & 7);
    if (color) {
        while (w--) { *p++ |= mask; }
    } else {
        mask = ~mask;
        while (w--) { *p++ &= mask; }
    }
}

void ssd1306_draw_fast_vline(ssd1306_t *dev, int16_t x, int16_t y, int16_t h, uint8_t color) {
    if (!dev || x < 0 || x >= SSD1306_WIDTH || h <= 0) return;
    if (y < 0) { h += y; y = 0; }
    if (y + h > SSD1306_HEIGHT) { h = SSD1306_HEIGHT - y; }
    if (h <= 0) return;

    while (h--) {
        ssd1306_draw_pixel(dev, x, y++, color);
    }
}

void ssd1306_fill_rect(ssd1306_t *dev, int16_t x, int16_t y, int16_t w, int16_t h, uint8_t color) {
    if (!dev || w <= 0 || h <= 0) return;
    for (int16_t i = y; i < y + h; i++) {
        ssd1306_draw_fast_hline(dev, x, i, w, color);
    }
}

static void fill_circle_helper(ssd1306_t *dev, int16_t x0, int16_t y0, int16_t r, uint8_t corners, int16_t delta, uint8_t color) {
    int16_t f = 1 - r;
    int16_t ddF_x = 1;
    int16_t ddF_y = -2 * r;
    int16_t x = 0;
    int16_t y = r;
    int16_t px = x;
    int16_t py = y;

    delta++;

    while (x < y) {
        if (f >= 0) {
            y--;
            ddF_y += 2;
            f += ddF_y;
        }
        x++;
        ddF_x += 2;
        f += ddF_x;

        if (x < (y + 1)) {
            if (corners & 1) ssd1306_draw_fast_vline(dev, x0 + x, y0 - y, 2 * y + delta, color);
            if (corners & 2) ssd1306_draw_fast_vline(dev, x0 - x, y0 - y, 2 * y + delta, color);
        }
        if (y != py) {
            if (corners & 1) ssd1306_draw_fast_vline(dev, x0 + py, y0 - px, 2 * px + delta, color);
            if (corners & 2) ssd1306_draw_fast_vline(dev, x0 - py, y0 - px, 2 * px + delta, color);
            py = y;
        }
        px = x;
    }
}

void ssd1306_fill_round_rect(ssd1306_t *dev, int16_t x, int16_t y, int16_t w, int16_t h, int16_t r, uint8_t color) {
    if (!dev || w <= 0 || h <= 0) return;
    int16_t max_r = ((w < h) ? w : h) / 2;
    if (r > max_r) r = max_r;
    if (r <= 0) {
        ssd1306_fill_rect(dev, x, y, w, h, color);
        return;
    }

    ssd1306_fill_rect(dev, x + r, y, w - 2 * r, h, color);
    fill_circle_helper(dev, x + w - r - 1, y + r, r, 1, h - 2 * r - 1, color);
    fill_circle_helper(dev, x + r, y + r, r, 2, h - 2 * r - 1, color);
}

void ssd1306_fill_triangle(ssd1306_t *dev, int16_t x0, int16_t y0, int16_t x1, int16_t y1, int16_t x2, int16_t y2, uint8_t color) {
    if (!dev) return;
    int16_t a, b, y, last;

    if (y0 > y1) { SWAP_INT16(y0, y1); SWAP_INT16(x0, x1); }
    if (y1 > y2) { SWAP_INT16(y2, y1); SWAP_INT16(x2, x1); }
    if (y0 > y1) { SWAP_INT16(y0, y1); SWAP_INT16(x0, x1); }

    if (y0 == y2) {
        a = b = x0;
        if (x1 < a) a = x1; else if (x1 > b) b = x1;
        if (x2 < a) a = x2; else if (x2 > b) b = x2;
        ssd1306_draw_fast_hline(dev, a, y0, b - a + 1, color);
        return;
    }

    int32_t dx01 = x1 - x0, dy01 = y1 - y0;
    int32_t dx02 = x2 - x0, dy02 = y2 - y0;
    int32_t dx12 = x2 - x1, dy12 = y2 - y1;
    int32_t sa = 0, sb = 0;

    if (y1 == y2) last = y1;
    else         last = y1 - 1;

    for (y = y0; y <= last; y++) {
        a = x0 + sa / dy01;
        b = x0 + sb / dy02;
        sa += dx01;
        sb += dx02;
        if (a > b) SWAP_INT16(a, b);
        ssd1306_draw_fast_hline(dev, a, y, b - a + 1, color);
    }

    sa = (int32_t)dx12 * (y - y1);
    sb = (int32_t)dx02 * (y - y0);
    for (; y <= y2; y++) {
        a = x1 + sa / dy12;
        b = x0 + sb / dy02;
        sa += dx12;
        sb += dx02;
        if (a > b) SWAP_INT16(a, b);
        ssd1306_draw_fast_hline(dev, a, y, b - a + 1, color);
    }
}

