#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "st7735.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool     is_active;
    bool     is_ringing;
    uint32_t remaining_seconds;
} alarm_status_t;

/**
 * @brief Initialize the Alarm & Web Server subsystem
 * 
 * Starts esp_http_server on port 80 and registers the GamePhoenix Alarm Portal
 * 
 * @return ESP_OK on success
 */
esp_err_t alarm_clock_init(void);

/**
 * @brief Update alarm state machine (call from worker task or display loop)
 */
void alarm_clock_update(void);

/**
 * @brief Set alarm countdown
 * 
 * @param hours Hours until alarm rings
 * @param minutes Minutes until alarm rings
 */
void alarm_clock_set(uint32_t hours, uint32_t minutes);

/**
 * @brief Cancel any set or ringing alarm
 */
void alarm_clock_cancel(void);

/**
 * @brief Dismiss ringing alarm (e.g. on touch sensor tap)
 */
void alarm_clock_dismiss(void);

/**
 * @brief Check if alarm is currently ringing
 */
bool alarm_clock_is_ringing(void);

/**
 * @brief Check if alarm is currently set & active
 */
bool alarm_clock_is_active(void);

/**
 * @brief Get alarm status details
 */
void alarm_clock_get_status(alarm_status_t *status);

/**
 * @brief Convert Gregorian date to Persian Shamsi (Jalali) string
 * 
 * Matching exact algorithm from COD2.ino
 * 
 * @param gY Gregorian year
 * @param gM Gregorian month (1-12)
 * @param gD Gregorian day (1-31)
 * @param out_str Buffer for formatted Shamsi date (e.g. "1403/08/16")
 * @param max_len Buffer size
 */
void alarm_clock_get_shamsi_date(int gY, int gM, int gD, char *out_str, size_t max_len);

/**
 * @brief Render the Clock & Alarm screen on ST7735 160x128 TFT
 * 
 * @param dev Pointer to ST7735 display structure
 */
void alarm_clock_draw_screen(st7735_t *dev);

/**
 * @brief Render the urgent Alarm Ringing screen on ST7735 160x128 TFT
 * 
 * @param dev Pointer to ST7735 display structure
 */
void alarm_clock_draw_ringing_screen(st7735_t *dev);

#ifdef __cplusplus
}
#endif

