#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "driver/i2c.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BMI160_CHIP_ID            0xD1
#define BMI160_I2C_ADDR_LOWER     0x68
#define BMI160_I2C_ADDR_UPPER     0x69

typedef struct {
    i2c_port_t i2c_port;     /*!< I2C port (e.g. I2C_NUM_0) */
    gpio_num_t sda_pin;      /*!< GPIO number for SDA */
    gpio_num_t scl_pin;      /*!< GPIO number for SCL */
    uint32_t   clk_speed_hz; /*!< Clock speed in Hz (e.g. 400000) */
    uint8_t    i2c_addr;     /*!< 0x68, 0x69, or 0 for auto-detect */
} bmi160_config_t;

typedef struct {
    float ax; /*!< Acceleration X in g (±2g range) */
    float ay; /*!< Acceleration Y in g (±2g range) */
    float az; /*!< Acceleration Z in g (±2g range) */
    float gx; /*!< Angular velocity X in dps (±2000 dps range) */
    float gy; /*!< Angular velocity Y in dps (±2000 dps range) */
    float gz; /*!< Angular velocity Z in dps (±2000 dps range) */
} bmi160_data_t;

typedef enum {
    BMI160_MOOD_NEUTRAL = 0,
    BMI160_MOOD_ANGRY,
    BMI160_MOOD_TIRED,
} bmi160_motion_mood_t;

/**
 * @brief Initialize BMI160 IMU over I2C
 * 
 * Configures I2C peripheral if not already initialized, auto-probes 
 * address 0x69 and 0x68, resets sensor, and configures:
 * - Accel: Normal mode, ±2g, 100Hz ODR
 * - Gyro: Normal mode, ±2000 dps, 100Hz ODR
 * 
 * @param cfg Configuration options
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t bmi160_init(const bmi160_config_t *cfg);

/**
 * @brief Read calibrated Accel and Gyro data
 * 
 * @param data Output data structure (ax, ay, az in g; gx, gy, gz in deg/s)
 * @return ESP_OK on success
 */
esp_err_t bmi160_read_data(bmi160_data_t *data);

/**
 * @brief Read raw 16-bit register values in order:
 *        [0]=gx, [1]=gy, [2]=gz, [3]=ax, [4]=ay, [5]=az
 * 
 * @param accel_gyro Array of 6 int16_t elements
 * @return ESP_OK on success
 */
esp_err_t bmi160_read_raw(int16_t accel_gyro[6]);

/**
 * @brief Check if robot was moved/shaken based on delta acceleration
 * 
 * @param threshold Difference threshold (default 0.22g matching COD2.ino)
 * @param diff_out Optional pointer to store computed diff
 * @return true if motion exceeded threshold
 */
bool bmi160_check_motion(float threshold, float *diff_out);

/**
 * @brief Determine robot mood based on physical orientation / tilt
 * 
 * Matching COD2.ino:
 * - az < -0.6 or |ax| > 0.6 -> ANGRY (upside down or tilted hard sideways)
 * - |ay| > 0.6 -> TIRED (face down or leaning back)
 * - Otherwise -> NEUTRAL
 * 
 * @param data Current sensor readings
 * @return bmi160_motion_mood_t
 */
bmi160_motion_mood_t bmi160_evaluate_mood(const bmi160_data_t *data);

#ifdef __cplusplus
}
#endif

