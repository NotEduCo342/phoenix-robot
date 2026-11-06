#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "esp_wifi.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    WIFI_SETUP_IDLE = 0,
    WIFI_SETUP_WAITING,      /*!< In AP mode, waiting for customer credentials */
    WIFI_SETUP_CONNECTING,   /*!< Customer submitted, attempting connection to router */
    WIFI_SETUP_CONNECTED,    /*!< Connection to router successful! */
    WIFI_SETUP_FAILED        /*!< Connection failed (wrong password/timeout) */
} wifi_setup_status_t;

typedef struct {
    const char *ap_ssid;      /*!< AP SSID (default "PHOENIX-SETUP") */
    const char *ap_password;  /*!< AP Password (default "gamephoenix") */
    const char *sta_ssid;     /*!< Router SSID (optional fallback) */
    const char *sta_password; /*!< Router Password (optional fallback) */
} wifi_manager_config_t;

/**
 * @brief Initialize NVS, WiFi stack, and determine whether to boot in Setup AP or pure STA
 * 
 * @param cfg Configuration options
 * @return ESP_OK on success
 */
esp_err_t wifi_manager_init(const wifi_manager_config_t *cfg);

/**
 * @brief Check if WiFi is connected to home router (STA) and has leased IP
 */
bool wifi_manager_is_sta_connected(void);

/**
 * @brief Check if valid saved WiFi credentials exist in NVS
 */
bool wifi_manager_has_saved_credentials(void);

/**
 * @brief Get current onboarding / setup state machine status
 */
wifi_setup_status_t wifi_manager_get_setup_status(void);

/**
 * @brief Reset setup status back to WAITING after a failure
 */
void wifi_manager_reset_setup_status(void);

/**
 * @brief Get the SSID currently being connected to during setup
 */
const char* wifi_manager_get_connecting_ssid(void);

/**
 * @brief Get WiFi RSSI percent (0-100%)
 */
int wifi_manager_get_rssi_percent(void);

/**
 * @brief Get Access Point IP string (typically "192.168.4.1")
 */
const char* wifi_manager_get_ap_ip(void);

/**
 * @brief Get Station IP string if connected, or "Not Connected"
 */
const char* wifi_manager_get_sta_ip(void);

/**
 * @brief Check if time is synchronized via SNTP
 */
bool wifi_manager_is_time_synced(void);

/**
 * @brief Start onboarding SoftAP and DNS captive portal
 * 
 * @param ap_ssid AP SSID name
 * @param ap_password AP password
 * @return ESP_OK on success
 */
esp_err_t wifi_manager_start_setup_ap(const char *ap_ssid, const char *ap_password);

/**
 * @brief Permanently turn off the SoftAP and DNS captive portal (pure STA mode)
 * 
 * @return ESP_OK on success
 */
esp_err_t wifi_manager_stop_ap(void);

/**
 * @brief Connect to home WiFi router and persist credentials in NVS
 * 
 * @param ssid Router SSID
 * @param password Router password (or NULL/empty)
 * @return ESP_OK on success
 */
esp_err_t wifi_manager_connect_sta(const char *ssid, const char *password);

/**
 * @brief Clear persisted WiFi router credentials in NVS (Factory Reset)
 * 
 * @return ESP_OK on success
 */
esp_err_t wifi_manager_clear_credentials(void);

typedef struct {
    char ssid[33];
    int8_t rssi;
    uint8_t authmode;
} wifi_scanned_ap_t;

/**
 * @brief Trigger a non-blocking asynchronous Wi-Fi scan in background
 * 
 * @return ESP_OK on success
 */
esp_err_t wifi_manager_trigger_scan(void);

/**
 * @brief Check if a Wi-Fi scan is currently in progress
 */
bool wifi_manager_is_scanning(void);

/**
 * @brief Retrieve cached scanned AP records (0ms response, no blocking)
 * 
 * @param out_records Array to fill
 * @param max_records Maximum capacity of out_records
 * @return Number of cached records copied
 */
uint16_t wifi_manager_get_cached_aps(wifi_scanned_ap_t *out_records, uint16_t max_records);

/**
 * @brief Scan available 2.4GHz WiFi networks
 * 
 * @param records Array of wifi_ap_record_t to store results
 * @param max_records Maximum number of records to retrieve
 * @param out_count Actual number of records found
 * @return ESP_OK on success
 */
esp_err_t wifi_manager_scan_networks(wifi_ap_record_t *records, uint16_t max_records, uint16_t *out_count);

/**
 * @brief Check if saved SSID exists in NVS and retrieve it
 * 
 * @param out_ssid Buffer of at least 33 bytes
 * @param max_len Size of out_ssid buffer
 * @return true if credentials found, false otherwise
 */
bool wifi_manager_get_saved_ssid(char *out_ssid, size_t max_len);

#ifdef __cplusplus
}
#endif
