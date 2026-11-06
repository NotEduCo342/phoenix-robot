#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "driver/gpio.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize the vibration motor GPIO pin
 * 
 * @param vibro_pin GPIO pin for coin vibration motor (e.g. GPIO 10)
 * @return ESP_OK on success
 */
esp_err_t haptics_init(gpio_num_t vibro_pin);

/**
 * @brief Trigger a single non-blocking vibration pulse
 * 
 * @param duration_ms Pulse duration in milliseconds
 */
void haptics_pulse(uint32_t duration_ms);

/**
 * @brief Trigger a non-blocking realistic heartbeat pattern (lub-dub)
 */
void haptics_heartbeat(void);

/**
 * @brief Continuous vibration rumble
 * 
 * @param enable true to turn on, false to turn off
 */
void haptics_set_active(bool enable);

/**
 * @brief Immediately stop any active vibration
 */
void haptics_stop(void);

#ifdef __cplusplus
}
#endif

