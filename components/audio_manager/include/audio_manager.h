#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "driver/gpio.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    SOUND_BOOT = 0,
    SOUND_HAPPY_CHIRP,
    SOUND_ANGRY,
    SOUND_MODE_SWITCH,
    SOUND_JUMP,
    SOUND_SCORE,
    SOUND_GAME_OVER,
    SOUND_CRASH,
    SOUND_ALARM_BEEP,
    SOUND_HEARTBEAT,
    SOUND_CLICK,
} robot_sound_t;

typedef struct {
    gpio_num_t bclk_pin;    /*!< I2S BCLK (GPIO 5) */
    gpio_num_t ws_pin;      /*!< I2S LRC / WS (GPIO 6) */
    gpio_num_t dout_pin;    /*!< I2S DIN / DOUT (GPIO 7) */
    uint32_t   sample_rate; /*!< Default 22050 Hz */
    uint8_t    volume;      /*!< Volume 0 - 100 (default 75) */
} audio_config_t;

/**
 * @brief Initialize MAX98357A I2S driver and start asynchronous audio task
 * 
 * @param cfg Audio configuration
 * @return ESP_OK on success
 */
esp_err_t audio_manager_init(const audio_config_t *cfg);

/**
 * @brief Play a preset robotic sound effect asynchronously
 * 
 * @param sound Predefined sound effect enum
 * @return ESP_OK if queued successfully
 */
esp_err_t audio_play(robot_sound_t sound);

/**
 * @brief Play a single tone asynchronously
 * 
 * @param freq_hz Frequency in Hertz
 * @param duration_ms Duration in milliseconds
 * @return ESP_OK if queued successfully
 */
esp_err_t audio_play_tone(uint16_t freq_hz, uint16_t duration_ms);

/**
 * @brief Play a frequency sweep asynchronously
 * 
 * @param start_freq_hz Start frequency
 * @param end_freq_hz End frequency
 * @param duration_ms Sweep duration in milliseconds
 * @return ESP_OK if queued successfully
 */
esp_err_t audio_play_sweep(uint16_t start_freq_hz, uint16_t end_freq_hz, uint16_t duration_ms);

/**
 * @brief Set master volume (0 - 100%)
 * 
 * @param volume Percentage volume
 */
void audio_set_volume(uint8_t volume);

/**
 * @brief Get current master volume
 * 
 * @return uint8_t 0 - 100
 */
uint8_t audio_get_volume(void);

/**
 * @brief Stop current sound playback
 */
void audio_stop(void);

/**
 * @brief Check if audio is currently playing
 */
bool audio_is_playing(void);

#ifdef __cplusplus
}
#endif

