#include "bmi160.h"
#include <string.h>
#include <math.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "bmi160";

#define REG_CHIP_ID       0x00
#define REG_ERR_REG       0x02
#define REG_PMU_STATUS    0x03
#define REG_DATA_0        0x0C  // Gyro X LSB
#define REG_ACC_CONF      0x40
#define REG_ACC_RANGE     0x41
#define REG_GYR_CONF      0x42
#define REG_GYR_RANGE     0x43
#define REG_CMD           0x7E

#define CMD_SOFT_RESET         0xB6
#define CMD_ACC_MODE_NORMAL    0x11
#define CMD_GYR_MODE_NORMAL    0x15

static i2c_port_t s_i2c_port = I2C_NUM_0;
static uint8_t    s_i2c_addr = BMI160_I2C_ADDR_UPPER;
static bool       s_is_initialized = false;

static float s_prev_ax = 0.0f;
static float s_prev_ay = 0.0f;
static float s_prev_az = 1.0f;

static esp_err_t bmi160_write_reg(uint8_t reg, uint8_t val) {
    uint8_t buf[2] = {reg, val};
    return i2c_master_write_to_device(s_i2c_port, s_i2c_addr, buf, 2, pdMS_TO_TICKS(100));
}

static esp_err_t bmi160_read_regs(uint8_t reg, uint8_t *data, size_t len) {
    return i2c_master_write_read_device(s_i2c_port, s_i2c_addr, &reg, 1, data, len, pdMS_TO_TICKS(100));
}

static bool bmi160_probe_address(uint8_t addr) {
    uint8_t reg = REG_CHIP_ID;
    uint8_t chip_id = 0;
    esp_err_t err = i2c_master_write_read_device(s_i2c_port, addr, &reg, 1, &chip_id, 1, pdMS_TO_TICKS(50));
    if (err == ESP_OK && chip_id == BMI160_CHIP_ID) {
        ESP_LOGI(TAG, "BMI160 found at I2C address 0x%02X (Chip ID: 0x%02X)", addr, chip_id);
        return true;
    }
    return false;
}

esp_err_t bmi160_init(const bmi160_config_t *cfg) {
    if (!cfg) return ESP_ERR_INVALID_ARG;
    s_i2c_port = cfg->i2c_port;

    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = cfg->sda_pin,
        .scl_io_num = cfg->scl_pin,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master = {
            .clk_speed = cfg->clk_speed_hz ? cfg->clk_speed_hz : 400000
        },
        .clk_flags = 0
    };

    esp_err_t err = i2c_param_config(s_i2c_port, &conf);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2c_param_config failed: %s", esp_err_to_name(err));
        return err;
    }

    err = i2c_driver_install(s_i2c_port, conf.mode, 0, 0, 0);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "i2c_driver_install failed: %s", esp_err_to_name(err));
        return err;
    }

    // Auto-detect or use user specified address
    if (cfg->i2c_addr != 0) {
        if (bmi160_probe_address(cfg->i2c_addr)) {
            s_i2c_addr = cfg->i2c_addr;
        } else {
            ESP_LOGE(TAG, "BMI160 not responding at specified address 0x%02X", cfg->i2c_addr);
            return ESP_ERR_NOT_FOUND;
        }
    } else {
        if (bmi160_probe_address(BMI160_I2C_ADDR_UPPER)) {
            s_i2c_addr = BMI160_I2C_ADDR_UPPER;
        } else if (bmi160_probe_address(BMI160_I2C_ADDR_LOWER)) {
            s_i2c_addr = BMI160_I2C_ADDR_LOWER;
        } else {
            ESP_LOGE(TAG, "BMI160 not found at 0x69 or 0x68! Check wiring on SDA=GPIO%d, SCL=GPIO%d", 
                     cfg->sda_pin, cfg->scl_pin);
            return ESP_ERR_NOT_FOUND;
        }
    }

    // 1. Soft reset
    ESP_LOGI(TAG, "Resetting BMI160...");
    bmi160_write_reg(REG_CMD, CMD_SOFT_RESET);
    vTaskDelay(pdMS_TO_TICKS(30));

    // 2. Set Accel to Normal Mode
    bmi160_write_reg(REG_CMD, CMD_ACC_MODE_NORMAL);
    vTaskDelay(pdMS_TO_TICKS(10));

    // 3. Set Gyro to Normal Mode
    bmi160_write_reg(REG_CMD, CMD_GYR_MODE_NORMAL);
    vTaskDelay(pdMS_TO_TICKS(80));

    // 4. Set Accel Range: ±2g (0x03 -> 16384 LSB/g)
    bmi160_write_reg(REG_ACC_RANGE, 0x03);

    // 5. Set Gyro Range: ±2000 dps (0x00 -> 16.4 LSB/dps)
    bmi160_write_reg(REG_GYR_RANGE, 0x00);

    // 6. Set Accel Config: 100Hz ODR, normal filter (0x28)
    bmi160_write_reg(REG_ACC_CONF, 0x28);

    // 7. Set Gyro Config: 100Hz ODR, normal filter (0x28)
    bmi160_write_reg(REG_GYR_CONF, 0x28);
    vTaskDelay(pdMS_TO_TICKS(10));

    s_is_initialized = true;
    ESP_LOGI(TAG, "BMI160 initialized successfully");
    return ESP_OK;
}

esp_err_t bmi160_read_raw(int16_t accel_gyro[6]) {
    if (!s_is_initialized) return ESP_ERR_INVALID_STATE;
    if (!accel_gyro) return ESP_ERR_INVALID_ARG;

    uint8_t raw[12] = {0};
    esp_err_t err = bmi160_read_regs(REG_DATA_0, raw, 12);
    if (err != ESP_OK) return err;

    // Gyro X, Y, Z
    accel_gyro[0] = (int16_t)(((uint16_t)raw[1] << 8) | raw[0]);
    accel_gyro[1] = (int16_t)(((uint16_t)raw[3] << 8) | raw[2]);
    accel_gyro[2] = (int16_t)(((uint16_t)raw[5] << 8) | raw[4]);

    // Accel X, Y, Z
    accel_gyro[3] = (int16_t)(((uint16_t)raw[7] << 8) | raw[6]);
    accel_gyro[4] = (int16_t)(((uint16_t)raw[9] << 8) | raw[8]);
    accel_gyro[5] = (int16_t)(((uint16_t)raw[11] << 8) | raw[10]);

    return ESP_OK;
}

esp_err_t bmi160_read_data(bmi160_data_t *data) {
    if (!data) return ESP_ERR_INVALID_ARG;
    int16_t raw[6];
    esp_err_t err = bmi160_read_raw(raw);
    if (err != ESP_OK) return err;

    // Accel: ±2g -> 16384 LSB/g
    data->ax = (float)raw[3] / 16384.0f;
    data->ay = (float)raw[4] / 16384.0f;
    data->az = (float)raw[5] / 16384.0f;

    // Gyro: ±2000 dps -> 16.4 LSB/dps
    data->gx = (float)raw[0] / 16.4f;
    data->gy = (float)raw[1] / 16.4f;
    data->gz = (float)raw[2] / 16.4f;

    return ESP_OK;
}

bool bmi160_check_motion(float threshold, float *diff_out) {
    bmi160_data_t d;
    if (bmi160_read_data(&d) != ESP_OK) {
        return false;
    }

    float diff = fabsf(d.ax - s_prev_ax) + fabsf(d.ay - s_prev_ay) + fabsf(d.az - s_prev_az);
    s_prev_ax = d.ax;
    s_prev_ay = d.ay;
    s_prev_az = d.az;

    if (diff_out) *diff_out = diff;

    return (diff > threshold);
}

bmi160_motion_mood_t bmi160_evaluate_mood(const bmi160_data_t *data) {
    if (!data) return BMI160_MOOD_NEUTRAL;

    // Matching COD2.ino orientation thresholds:
    // Upside-down or heavy lateral tilt -> ANGRY
    if (data->az < -0.6f || fabsf(data->ax) > 0.6f) {
        return BMI160_MOOD_ANGRY;
    }
    // Forward / backward pitch -> TIRED
    if (fabsf(data->ay) > 0.6f) {
        return BMI160_MOOD_TIRED;
    }
    return BMI160_MOOD_NEUTRAL;
}

