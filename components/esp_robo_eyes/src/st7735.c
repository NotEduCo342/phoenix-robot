#include "st7735.h"
#include <string.h>
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "st7735";

#define SWAP_INT16(a, b) do { int16_t t = a; a = b; b = t; } while (0)

// Chunk size for DMA SPI transfers (8 lines = 160 * 8 * 2 = 2560 bytes, well under 4092-byte hardware limit)
#define CHUNK_LINES  8
#define CHUNK_PIXELS (ST7735_WIDTH * CHUNK_LINES)
#define CHUNK_BYTES  (CHUNK_PIXELS * sizeof(uint16_t))

static inline uint16_t to_be16(uint16_t color) {
    return (uint16_t)((color >> 8) | (color << 8));
}

static void st7735_send_cmd(st7735_t *dev, uint8_t cmd) {
    gpio_set_level(dev->config.dc_pin, 0); // D/C LOW for command
    spi_transaction_t t;
    memset(&t, 0, sizeof(t));
    t.length = 8;
    t.tx_buffer = &cmd;
    spi_device_polling_transmit(dev->spi_dev, &t);
}

static void st7735_send_data(st7735_t *dev, const uint8_t *data, size_t len) {
    if (len == 0) return;
    gpio_set_level(dev->config.dc_pin, 1); // D/C HIGH for data
    spi_transaction_t t;
    memset(&t, 0, sizeof(t));
    t.length = len * 8;
    t.tx_buffer = data;
    spi_device_polling_transmit(dev->spi_dev, &t);
}

static void st7735_set_window(st7735_t *dev, uint8_t x0, uint8_t y0, uint8_t x1, uint8_t y1) {
    x0 += (uint8_t)dev->config.col_offset;
    x1 += (uint8_t)dev->config.col_offset;
    y0 += (uint8_t)dev->config.row_offset;
    y1 += (uint8_t)dev->config.row_offset;

    st7735_send_cmd(dev, 0x2A); // CASET (Column Address Set)
    uint8_t data_x[] = {0x00, x0, 0x00, x1};
    st7735_send_data(dev, data_x, 4);

    st7735_send_cmd(dev, 0x2B); // RASET (Row Address Set)
    uint8_t data_y[] = {0x00, y0, 0x00, y1};
    st7735_send_data(dev, data_y, 4);

    st7735_send_cmd(dev, 0x2C); // RAMWR (RAM Write)
}

void st7735_set_backlight(st7735_t *dev, bool enable) {
    if (dev && dev->config.bl_pin != GPIO_NUM_NC && dev->config.bl_pin >= 0) {
        gpio_set_level(dev->config.bl_pin, enable ? 1 : 0);
    }
}

esp_err_t st7735_init(st7735_t *dev, const st7735_config_t *cfg) {
    if (!dev || !cfg) return ESP_ERR_INVALID_ARG;
    dev->config = *cfg;

    // 1. Allocate 40KB DMA-capable framebuffer in internal SRAM
    dev->buffer = (uint16_t *)heap_caps_malloc(ST7735_PIXELS * sizeof(uint16_t), MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    if (!dev->buffer) {
        ESP_LOGE(TAG, "Failed to allocate 40KB DMA framebuffer!");
        return ESP_ERR_NO_MEM;
    }
    memset(dev->buffer, 0, ST7735_PIXELS * sizeof(uint16_t));

    // 2. Configure control GPIOs (D/C, RST, BL)
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << dev->config.dc_pin),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    if (dev->config.rst_pin != GPIO_NUM_NC && dev->config.rst_pin >= 0) {
        io_conf.pin_bit_mask |= (1ULL << dev->config.rst_pin);
    }
    if (dev->config.bl_pin != GPIO_NUM_NC && dev->config.bl_pin >= 0) {
        io_conf.pin_bit_mask |= (1ULL << dev->config.bl_pin);
    }
    gpio_config(&io_conf);

    // Turn Backlight ON
    st7735_set_backlight(dev, true);

    // 3. Hardware Reset
    if (dev->config.rst_pin != GPIO_NUM_NC && dev->config.rst_pin >= 0) {
        gpio_set_level(dev->config.rst_pin, 0);
        vTaskDelay(pdMS_TO_TICKS(50));
        gpio_set_level(dev->config.rst_pin, 1);
        vTaskDelay(pdMS_TO_TICKS(50));
    }

    // 4. Initialize SPI Bus with DMA
    spi_bus_config_t buscfg = {
        .mosi_io_num = dev->config.mosi_pin,
        .miso_io_num = -1,
        .sclk_io_num = dev->config.sclk_pin,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = CHUNK_BYTES + 16
    };

    esp_err_t ret = spi_bus_initialize(dev->config.spi_host, &buscfg, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "Failed to initialize SPI bus: %s", esp_err_to_name(ret));
        return ret;
    }

    spi_device_interface_config_t devcfg = {
        .command_bits = 0,
        .address_bits = 0,
        .dummy_bits = 0,
        .mode = 0,                          // SPI mode 0
        .clock_source = SPI_CLK_SRC_DEFAULT,
        .duty_cycle_pos = 0,
        .cs_ena_pretrans = 0,
        .cs_ena_posttrans = 0,
        .clock_speed_hz = 40 * 1000 * 1000, // 40 MHz high-speed SPI
        .input_delay_ns = 0,
        .spics_io_num = dev->config.cs_pin,
        .flags = 0,
        .queue_size = 7,
        .pre_cb = NULL,
        .post_cb = NULL
    };

    ret = spi_bus_add_device(dev->config.spi_host, &devcfg, &dev->spi_dev);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add SPI device: %s", esp_err_to_name(ret));
        return ret;
    }

    // 5. ST7735R Initialization Command Sequence
    st7735_send_cmd(dev, 0x01); // Software Reset
    vTaskDelay(pdMS_TO_TICKS(150));

    st7735_send_cmd(dev, 0x11); // Sleep Out
    vTaskDelay(pdMS_TO_TICKS(200));

    // Frame rate control - normal mode
    st7735_send_cmd(dev, 0xB1);
    uint8_t frmctr1[] = {0x01, 0x2C, 0x2D};
    st7735_send_data(dev, frmctr1, 3);

    // Frame rate control - idle mode
    st7735_send_cmd(dev, 0xB2);
    uint8_t frmctr2[] = {0x01, 0x2C, 0x2D};
    st7735_send_data(dev, frmctr2, 3);

    // Frame rate control - partial mode
    st7735_send_cmd(dev, 0xB3);
    uint8_t frmctr3[] = {0x01, 0x2C, 0x2D, 0x01, 0x2C, 0x2D};
    st7735_send_data(dev, frmctr3, 6);

    // Display inversion control
    st7735_send_cmd(dev, 0xB4);
    uint8_t invctr[] = {0x07};
    st7735_send_data(dev, invctr, 1);

    // Power control 1
    st7735_send_cmd(dev, 0xC0);
    uint8_t pwctr1[] = {0xA2, 0x02, 0x84};
    st7735_send_data(dev, pwctr1, 3);

    // Power control 2
    st7735_send_cmd(dev, 0xC1);
    uint8_t pwctr2[] = {0xC5};
    st7735_send_data(dev, pwctr2, 1);

    // Power control 3
    st7735_send_cmd(dev, 0xC2);
    uint8_t pwctr3[] = {0x0A, 0x00};
    st7735_send_data(dev, pwctr3, 2);

    // Power control 4
    st7735_send_cmd(dev, 0xC3);
    uint8_t pwctr4[] = {0x8A, 0x2A};
    st7735_send_data(dev, pwctr4, 2);

    // Power control 5
    st7735_send_cmd(dev, 0xC4);
    uint8_t pwctr5[] = {0x8A, 0xEE};
    st7735_send_data(dev, pwctr5, 2);

    // VCOM control 1
    st7735_send_cmd(dev, 0xC5);
    uint8_t vmctr1[] = {0x0E};
    st7735_send_data(dev, vmctr1, 1);

    st7735_send_cmd(dev, 0x20); // Display Inversion OFF

    // MADCTL - Landscape mode (160x128): Row/Col exchange MV=1, MY=1 (0xA0)
    st7735_send_cmd(dev, 0x36);
    uint8_t madctl = 0xA0;
    if (dev->config.bgr_order) {
        madctl |= 0x08; // Set BGR bit
    }
    st7735_send_data(dev, &madctl, 1);

    // Color mode: 16-bit RGB565 (0x05)
    st7735_send_cmd(dev, 0x3A);
    uint8_t colmod[] = {0x05};
    st7735_send_data(dev, colmod, 1);

    // Gamma positive polarity
    st7735_send_cmd(dev, 0xE0);
    uint8_t gmctrp[] = {0x02, 0x1C, 0x07, 0x12, 0x37, 0x32, 0x29, 0x2D, 0x29, 0x25, 0x2B, 0x39, 0x00, 0x01, 0x03, 0x10};
    st7735_send_data(dev, gmctrp, 16);

    // Gamma negative polarity
    st7735_send_cmd(dev, 0xE1);
    uint8_t gmctrn[] = {0x03, 0x1D, 0x07, 0x06, 0x2E, 0x2C, 0x29, 0x2D, 0x2E, 0x2E, 0x37, 0x3F, 0x00, 0x00, 0x02, 0x10};
    st7735_send_data(dev, gmctrn, 16);

    st7735_send_cmd(dev, 0x29); // Display ON
    vTaskDelay(pdMS_TO_TICKS(100));

    st7735_clear(dev, COLOR_BLACK);
    return st7735_display(dev);
}

void st7735_clear(st7735_t *dev, uint16_t color) {
    if (!dev || !dev->buffer) return;
    uint16_t be_color = to_be16(color);
    for (size_t i = 0; i < ST7735_PIXELS; i++) {
        dev->buffer[i] = be_color;
    }
}

esp_err_t st7735_display(st7735_t *dev) {
    if (!dev || !dev->buffer) return ESP_ERR_INVALID_ARG;

    st7735_set_window(dev, 0, 0, ST7735_WIDTH - 1, ST7735_HEIGHT - 1);

    gpio_set_level(dev->config.dc_pin, 1); // D/C HIGH for RAM data

    size_t remaining_pixels = ST7735_PIXELS;
    uint16_t *p = dev->buffer;

    while (remaining_pixels > 0) {
        size_t current_pixels = (remaining_pixels > CHUNK_PIXELS) ? CHUNK_PIXELS : remaining_pixels;
        spi_transaction_t t;
        memset(&t, 0, sizeof(t));
        t.length = current_pixels * 16; // in bits
        t.tx_buffer = p;
        esp_err_t err = spi_device_polling_transmit(dev->spi_dev, &t);
        if (err != ESP_OK) return err;
        p += current_pixels;
        remaining_pixels -= current_pixels;
    }
    return ESP_OK;
}

void st7735_draw_pixel(st7735_t *dev, int16_t x, int16_t y, uint16_t color) {
    if (!dev || !dev->buffer || x < 0 || x >= ST7735_WIDTH || y < 0 || y >= ST7735_HEIGHT) return;
    dev->buffer[(size_t)x + (size_t)y * ST7735_WIDTH] = to_be16(color);
}

void st7735_draw_fast_hline(st7735_t *dev, int16_t x, int16_t y, int16_t w, uint16_t color) {
    if (!dev || !dev->buffer || y < 0 || y >= ST7735_HEIGHT || w <= 0) return;
    if (x < 0) { w += x; x = 0; }
    if (x + w > ST7735_WIDTH) { w = ST7735_WIDTH - x; }
    if (w <= 0) return;

    uint16_t be_color = to_be16(color);
    uint16_t *p = &dev->buffer[(size_t)x + (size_t)y * ST7735_WIDTH];
    while (w--) {
        *p++ = be_color;
    }
}

void st7735_draw_fast_vline(st7735_t *dev, int16_t x, int16_t y, int16_t h, uint16_t color) {
    if (!dev || !dev->buffer || x < 0 || x >= ST7735_WIDTH || h <= 0) return;
    if (y < 0) { h += y; y = 0; }
    if (y + h > ST7735_HEIGHT) { h = ST7735_HEIGHT - y; }
    if (h <= 0) return;

    uint16_t be_color = to_be16(color);
    uint16_t *p = &dev->buffer[(size_t)x + (size_t)y * ST7735_WIDTH];
    while (h--) {
        *p = be_color;
        p += ST7735_WIDTH;
    }
}

void st7735_fill_rect(st7735_t *dev, int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) {
    if (!dev || !dev->buffer || w <= 0 || h <= 0) return;
    for (int16_t i = y; i < y + h; i++) {
        st7735_draw_fast_hline(dev, x, i, w, color);
    }
}

static void fill_circle_helper(st7735_t *dev, int16_t x0, int16_t y0, int16_t r, uint8_t corners, int16_t delta, uint16_t color) {
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
            if (corners & 1) st7735_draw_fast_vline(dev, x0 + x, y0 - y, 2 * y + delta, color);
            if (corners & 2) st7735_draw_fast_vline(dev, x0 - x, y0 - y, 2 * y + delta, color);
        }
        if (y != py) {
            if (corners & 1) st7735_draw_fast_vline(dev, x0 + py, y0 - px, 2 * px + delta, color);
            if (corners & 2) st7735_draw_fast_vline(dev, x0 - py, y0 - px, 2 * px + delta, color);
            py = y;
        }
        px = x;
    }
}

void st7735_fill_round_rect(st7735_t *dev, int16_t x, int16_t y, int16_t w, int16_t h, int16_t r, uint16_t color) {
    if (!dev || !dev->buffer || w <= 0 || h <= 0) return;
    int16_t max_r = ((w < h) ? w : h) / 2;
    if (r > max_r) r = max_r;
    if (r <= 0) {
        st7735_fill_rect(dev, x, y, w, h, color);
        return;
    }

    st7735_fill_rect(dev, x + r, y, w - 2 * r, h, color);
    fill_circle_helper(dev, x + w - r - 1, y + r, r, 1, h - 2 * r - 1, color);
    fill_circle_helper(dev, x + r, y + r, r, 2, h - 2 * r - 1, color);
}

void st7735_fill_triangle(st7735_t *dev, int16_t x0, int16_t y0, int16_t x1, int16_t y1, int16_t x2, int16_t y2, uint16_t color) {
    if (!dev || !dev->buffer) return;
    int16_t a, b, y, last;

    if (y0 > y1) { SWAP_INT16(y0, y1); SWAP_INT16(x0, x1); }
    if (y1 > y2) { SWAP_INT16(y2, y1); SWAP_INT16(x2, x1); }
    if (y0 > y1) { SWAP_INT16(y0, y1); SWAP_INT16(x0, x1); }

    if (y0 == y2) {
        a = b = x0;
        if (x1 < a) a = x1; else if (x1 > b) b = x1;
        if (x2 < a) a = x2; else if (x2 > b) b = x2;
        st7735_draw_fast_hline(dev, a, y0, b - a + 1, color);
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
        st7735_draw_fast_hline(dev, a, y, b - a + 1, color);
    }

    sa = (int32_t)dx12 * (y - y1);
    sb = (int32_t)dx02 * (y - y0);
    for (; y <= y2; y++) {
        a = x1 + sa / dy12;
        b = x0 + sb / dy02;
        sa += dx12;
        sb += dx02;
        if (a > b) SWAP_INT16(a, b);
        st7735_draw_fast_hline(dev, a, y, b - a + 1, color);
    }
}

static void draw_circle_helper(st7735_t *dev, int16_t x0, int16_t y0, int16_t r, uint8_t cornername, uint16_t color) {
    int16_t f = 1 - r;
    int16_t ddF_x = 1;
    int16_t ddF_y = -2 * r;
    int16_t x = 0;
    int16_t y = r;

    while (x < y) {
        if (f >= 0) {
            y--;
            ddF_y += 2;
            f += ddF_y;
        }
        x++;
        ddF_x += 2;
        f += ddF_x;
        if (cornername & 0x4) {
            st7735_draw_pixel(dev, x0 + x, y0 + y, color);
            st7735_draw_pixel(dev, x0 + y, y0 + x, color);
        }
        if (cornername & 0x2) {
            st7735_draw_pixel(dev, x0 + x, y0 - y, color);
            st7735_draw_pixel(dev, x0 + y, y0 - x, color);
        }
        if (cornername & 0x8) {
            st7735_draw_pixel(dev, x0 - y, y0 + x, color);
            st7735_draw_pixel(dev, x0 - x, y0 + y, color);
        }
        if (cornername & 0x1) {
            st7735_draw_pixel(dev, x0 - y, y0 - x, color);
            st7735_draw_pixel(dev, x0 - x, y0 - y, color);
        }
    }
}

void st7735_draw_rect(st7735_t *dev, int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) {
    if (!dev || !dev->buffer || w <= 0 || h <= 0) return;
    st7735_draw_fast_hline(dev, x, y, w, color);
    st7735_draw_fast_hline(dev, x, y + h - 1, w, color);
    st7735_draw_fast_vline(dev, x, y, h, color);
    st7735_draw_fast_vline(dev, x + w - 1, y, h, color);
}

void st7735_draw_circle(st7735_t *dev, int16_t x0, int16_t y0, int16_t r, uint16_t color) {
    if (!dev || !dev->buffer || r <= 0) return;
    int16_t f = 1 - r;
    int16_t ddF_x = 1;
    int16_t ddF_y = -2 * r;
    int16_t x = 0;
    int16_t y = r;

    st7735_draw_pixel(dev, x0, y0 + r, color);
    st7735_draw_pixel(dev, x0, y0 - r, color);
    st7735_draw_pixel(dev, x0 + r, y0, color);
    st7735_draw_pixel(dev, x0 - r, y0, color);

    while (x < y) {
        if (f >= 0) {
            y--;
            ddF_y += 2;
            f += ddF_y;
        }
        x++;
        ddF_x += 2;
        f += ddF_x;

        st7735_draw_pixel(dev, x0 + x, y0 + y, color);
        st7735_draw_pixel(dev, x0 - x, y0 + y, color);
        st7735_draw_pixel(dev, x0 + x, y0 - y, color);
        st7735_draw_pixel(dev, x0 - x, y0 - y, color);
        st7735_draw_pixel(dev, x0 + y, y0 + x, color);
        st7735_draw_pixel(dev, x0 - y, y0 + x, color);
        st7735_draw_pixel(dev, x0 + y, y0 - x, color);
        st7735_draw_pixel(dev, x0 - y, y0 - x, color);
    }
}

void st7735_fill_circle(st7735_t *dev, int16_t x0, int16_t y0, int16_t r, uint16_t color) {
    if (!dev || !dev->buffer || r <= 0) return;
    st7735_draw_fast_vline(dev, x0, y0 - r, 2 * r + 1, color);
    fill_circle_helper(dev, x0, y0, r, 3, 0, color);
}

void st7735_draw_round_rect(st7735_t *dev, int16_t x, int16_t y, int16_t w, int16_t h, int16_t r, uint16_t color) {
    if (!dev || !dev->buffer || w <= 0 || h <= 0) return;
    int16_t max_r = ((w < h) ? w : h) / 2;
    if (r > max_r) r = max_r;
    if (r <= 0) {
        st7735_draw_rect(dev, x, y, w, h, color);
        return;
    }
    st7735_draw_fast_hline(dev, x + r, y, w - 2 * r, color);
    st7735_draw_fast_hline(dev, x + r, y + h - 1, w - 2 * r, color);
    st7735_draw_fast_vline(dev, x, y + r, h - 2 * r, color);
    st7735_draw_fast_vline(dev, x + w - 1, y + r, h - 2 * r, color);
    draw_circle_helper(dev, x + r, y + r, r, 1, color);
    draw_circle_helper(dev, x + w - r - 1, y + r, r, 2, color);
    draw_circle_helper(dev, x + w - r - 1, y + h - r - 1, r, 4, color);
    draw_circle_helper(dev, x + r, y + h - r - 1, r, 8, color);
}

static const uint8_t font5x7[] = {
    0x00, 0x00, 0x00, 0x00, 0x00, // 32 space
    0x00, 0x00, 0x5F, 0x00, 0x00, // 33 !
    0x00, 0x07, 0x00, 0x07, 0x00, // 34 "
    0x14, 0x7F, 0x14, 0x7F, 0x14, // 35 #
    0x24, 0x2A, 0x7F, 0x2A, 0x12, // 36 $
    0x23, 0x13, 0x08, 0x64, 0x62, // 37 %
    0x36, 0x49, 0x55, 0x22, 0x50, // 38 &
    0x00, 0x05, 0x03, 0x00, 0x00, // 39 '
    0x00, 0x1C, 0x22, 0x41, 0x00, // 40 (
    0x00, 0x41, 0x22, 0x1C, 0x00, // 41 )
    0x08, 0x2A, 0x1C, 0x2A, 0x08, // 42 *
    0x08, 0x08, 0x3E, 0x08, 0x08, // 43 +
    0x00, 0x50, 0x30, 0x00, 0x00, // 44 ,
    0x08, 0x08, 0x08, 0x08, 0x08, // 45 -
    0x00, 0x60, 0x60, 0x00, 0x00, // 46 .
    0x20, 0x10, 0x08, 0x04, 0x02, // 47 /
    0x3E, 0x51, 0x49, 0x45, 0x3E, // 48 0
    0x00, 0x42, 0x7F, 0x40, 0x00, // 49 1
    0x42, 0x61, 0x51, 0x49, 0x46, // 50 2
    0x21, 0x41, 0x45, 0x4B, 0x31, // 51 3
    0x18, 0x14, 0x12, 0x7F, 0x10, // 52 4
    0x27, 0x45, 0x45, 0x45, 0x39, // 53 5
    0x3C, 0x4A, 0x49, 0x49, 0x30, // 54 6
    0x01, 0x71, 0x09, 0x05, 0x03, // 55 7
    0x36, 0x49, 0x49, 0x49, 0x36, // 56 8
    0x06, 0x49, 0x49, 0x29, 0x1E, // 57 9
    0x00, 0x36, 0x36, 0x00, 0x00, // 58 :
    0x00, 0x56, 0x36, 0x00, 0x00, // 59 ;
    0x00, 0x08, 0x14, 0x22, 0x41, // 60 <
    0x14, 0x14, 0x14, 0x14, 0x14, // 61 =
    0x41, 0x22, 0x14, 0x08, 0x00, // 62 >
    0x02, 0x01, 0x51, 0x09, 0x06, // 63 ?
    0x32, 0x49, 0x79, 0x41, 0x3E, // 64 @
    0x7E, 0x11, 0x11, 0x11, 0x7E, // 65 A
    0x7F, 0x49, 0x49, 0x49, 0x36, // 66 B
    0x3E, 0x41, 0x41, 0x41, 0x22, // 67 C
    0x7F, 0x41, 0x41, 0x22, 0x1C, // 68 D
    0x7F, 0x49, 0x49, 0x49, 0x41, // 69 E
    0x7F, 0x09, 0x09, 0x01, 0x01, // 70 F
    0x3E, 0x41, 0x41, 0x51, 0x32, // 71 G
    0x7F, 0x08, 0x08, 0x08, 0x7F, // 72 H
    0x00, 0x41, 0x7F, 0x41, 0x00, // 73 I
    0x20, 0x40, 0x41, 0x3F, 0x01, // 74 J
    0x7F, 0x08, 0x14, 0x22, 0x41, // 75 K
    0x7F, 0x40, 0x40, 0x40, 0x40, // 76 L
    0x7F, 0x02, 0x04, 0x02, 0x7F, // 77 M
    0x7F, 0x04, 0x08, 0x10, 0x7F, // 78 N
    0x3E, 0x41, 0x41, 0x41, 0x3E, // 79 O
    0x7F, 0x09, 0x09, 0x09, 0x06, // 80 P
    0x3E, 0x41, 0x51, 0x21, 0x5E, // 81 Q
    0x7F, 0x09, 0x19, 0x29, 0x46, // 82 R
    0x46, 0x49, 0x49, 0x49, 0x31, // 83 S
    0x01, 0x01, 0x7F, 0x01, 0x01, // 84 T
    0x3F, 0x40, 0x40, 0x40, 0x3F, // 85 U
    0x1F, 0x20, 0x40, 0x20, 0x1F, // 86 V
    0x7F, 0x20, 0x18, 0x20, 0x7F, // 87 W
    0x63, 0x14, 0x08, 0x14, 0x63, // 88 X
    0x03, 0x04, 0x78, 0x04, 0x03, // 89 Y
    0x61, 0x51, 0x49, 0x45, 0x43, // 90 Z
    0x00, 0x7F, 0x41, 0x41, 0x00, // 91 [
    0x02, 0x04, 0x08, 0x10, 0x20, // 92 "\"
    0x00, 0x41, 0x41, 0x7F, 0x00, // 93 ]
    0x04, 0x02, 0x01, 0x02, 0x04, // 94 ^
    0x40, 0x40, 0x40, 0x40, 0x40, // 95 _
    0x00, 0x01, 0x02, 0x04, 0x00, // 96 `
    0x20, 0x54, 0x54, 0x54, 0x78, // 97 a
    0x7F, 0x48, 0x44, 0x44, 0x38, // 98 b
    0x38, 0x44, 0x44, 0x44, 0x20, // 99 c
    0x38, 0x44, 0x44, 0x48, 0x7F, // 100 d
    0x38, 0x54, 0x54, 0x54, 0x18, // 101 e
    0x08, 0x7E, 0x09, 0x01, 0x02, // 102 f
    0x08, 0x14, 0x54, 0x54, 0x3C, // 103 g
    0x7F, 0x08, 0x04, 0x04, 0x78, // 104 h
    0x00, 0x44, 0x7D, 0x40, 0x00, // 105 i
    0x20, 0x40, 0x44, 0x3D, 0x00, // 106 j
    0x7F, 0x10, 0x28, 0x44, 0x00, // 107 k
    0x00, 0x41, 0x7F, 0x40, 0x00, // 108 l
    0x7C, 0x04, 0x18, 0x04, 0x78, // 109 m
    0x7C, 0x08, 0x04, 0x04, 0x78, // 110 n
    0x38, 0x44, 0x44, 0x44, 0x38, // 111 o
    0x7C, 0x14, 0x14, 0x14, 0x08, // 112 p
    0x08, 0x14, 0x14, 0x18, 0x7C, // 113 q
    0x7C, 0x08, 0x04, 0x04, 0x08, // 114 r
    0x48, 0x54, 0x54, 0x54, 0x20, // 115 s
    0x04, 0x3F, 0x44, 0x40, 0x20, // 116 t
    0x3C, 0x40, 0x40, 0x20, 0x7C, // 117 u
    0x1C, 0x20, 0x40, 0x20, 0x1C, // 118 v
    0x3C, 0x40, 0x30, 0x40, 0x3C, // 119 w
    0x44, 0x28, 0x10, 0x28, 0x44, // 120 x
    0x0C, 0x50, 0x50, 0x50, 0x3C, // 121 y
    0x44, 0x64, 0x54, 0x4C, 0x44, // 122 z
    0x00, 0x08, 0x36, 0x41, 0x00, // 123 {
    0x00, 0x00, 0x7F, 0x00, 0x00, // 124 |
    0x00, 0x41, 0x36, 0x08, 0x00, // 125 }
    0x08, 0x08, 0x2A, 0x1C, 0x08  // 126 ~
};

void st7735_draw_char(st7735_t *dev, int16_t x, int16_t y, char c, uint16_t color, uint16_t bg, uint8_t size) {
    if (!dev || !dev->buffer || c < 32 || c > 126 || size == 0) return;
    const uint8_t *glyph = &font5x7[(c - 32) * 5];

    for (int8_t i = 0; i < 5; i++) {
        uint8_t line = glyph[i];
        for (int8_t j = 0; j < 8; j++) {
            if (line & 0x1) {
                if (size == 1) st7735_draw_pixel(dev, x + i, y + j, color);
                else st7735_fill_rect(dev, x + i * size, y + j * size, size, size, color);
            } else if (bg != color) {
                if (size == 1) st7735_draw_pixel(dev, x + i, y + j, bg);
                else st7735_fill_rect(dev, x + i * size, y + j * size, size, size, bg);
            }
            line >>= 1;
        }
    }
    if (bg != color) {
        if (size == 1) st7735_draw_fast_vline(dev, x + 5, y, 8, bg);
        else st7735_fill_rect(dev, x + 5 * size, y, size, 8 * size, bg);
    }
}

void st7735_draw_string(st7735_t *dev, int16_t x, int16_t y, const char *str, uint16_t color, uint16_t bg, uint8_t size) {
    if (!dev || !dev->buffer || !str || size == 0) return;
    int16_t cur_x = x;
    int16_t cur_y = y;

    while (*str) {
        if (*str == '\n') {
            cur_x = x;
            cur_y += 8 * size;
        } else if (*str == '\r') {
            // skip
        } else {
            st7735_draw_char(dev, cur_x, cur_y, *str, color, bg, size);
            cur_x += 6 * size;
        }
        str++;
    }
}

void st7735_draw_bitmap(st7735_t *dev, int16_t x, int16_t y, const uint8_t *bitmap, int16_t w, int16_t h, uint16_t color, uint16_t bg) {
    if (!dev || !dev->buffer || !bitmap || w <= 0 || h <= 0) return;
    int16_t byte_width = (w + 7) / 8;
    for (int16_t j = 0; j < h; j++) {
        for (int16_t i = 0; i < w; i++) {
            uint8_t byte = bitmap[j * byte_width + i / 8];
            if (byte & (0x80 >> (i % 8))) {
                st7735_draw_pixel(dev, x + i, y + j, color);
            } else if (bg != color) {
                st7735_draw_pixel(dev, x + i, y + j, bg);
            }
        }
    }
}

