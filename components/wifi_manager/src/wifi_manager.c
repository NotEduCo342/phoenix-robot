#include "wifi_manager.h"
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_sntp.h"
#include "esp_mac.h"
#include "lwip/sockets.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"

static const char *TAG = "wifi_mgr";

#define NVS_NAMESPACE "robot_wifi"

static bool s_time_synced = false;
static char s_sta_ip[16] = "Not Connected";
static char s_ap_ip[16]  = "192.168.4.1";
static int8_t s_rssi = -100;

static esp_netif_t *s_netif_ap = NULL;
static esp_netif_t *s_netif_sta = NULL;
static TaskHandle_t s_dns_task_handle = NULL;
static int s_dns_sock = -1;
static bool s_is_ap_running = false;

static wifi_setup_status_t s_setup_status = WIFI_SETUP_IDLE;
static char s_connecting_ssid[33] = {0};
static char s_pending_pass[65] = {0};
static int s_retry_count = 0;

// ─── Scanned Networks Cache (Zero-latency, Non-blocking) ──────────────────────
#define MAX_CACHED_APS 20
static wifi_scanned_ap_t s_cached_aps[MAX_CACHED_APS];
static uint16_t s_cached_ap_count = 0;
static bool s_is_scanning = false;
static uint64_t s_last_scan_time_ms = 0;

// ─── DNS Captive Portal Server (Port 53 UDP) ─────────────────────────────────
#define DNS_PORT 53
#pragma pack(push, 1)
typedef struct {
    uint16_t id;
    uint16_t flags;
    uint16_t qdcount;
    uint16_t ancount;
    uint16_t nscount;
    uint16_t arcount;
} dns_header_t;
#pragma pack(pop)

static void dns_server_task(void *pvParameters) {
    s_dns_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (s_dns_sock < 0) {
        ESP_LOGE(TAG, "Unable to create DNS socket: errno %d", errno);
        s_dns_task_handle = NULL;
        vTaskDelete(NULL);
        return;
    }

    struct sockaddr_in server_addr = {
        .sin_family = AF_INET,
        .sin_addr = { .s_addr = htonl(INADDR_ANY) },
        .sin_port = htons(DNS_PORT)
    };

    if (bind(s_dns_sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        ESP_LOGE(TAG, "DNS socket unable to bind: errno %d", errno);
        close(s_dns_sock);
        s_dns_sock = -1;
        s_dns_task_handle = NULL;
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "DNS Captive Portal responder running on UDP 53");

    uint8_t rx_buffer[512];
    struct sockaddr_in client_addr;
    socklen_t client_addr_len = sizeof(client_addr);

    while (1) {
        int len = recvfrom(s_dns_sock, rx_buffer, sizeof(rx_buffer), 0,
                           (struct sockaddr *)&client_addr, &client_addr_len);
        if (len < 0) {
            break;
        }
        if (len >= (int)sizeof(dns_header_t)) {
            // Find end of query question name (QNAME)
            int q_end = sizeof(dns_header_t);
            while (q_end < len && rx_buffer[q_end] != 0) {
                q_end += 1 + rx_buffer[q_end];
            }
            if (q_end + 5 <= len) {
                q_end++; // skip null byte (terminating the QNAME)
                uint16_t qtype = ((uint16_t)rx_buffer[q_end] << 8) | rx_buffer[q_end + 1];
                int question_end = q_end + 4; // skip QTYPE (2 bytes) + QCLASS (2 bytes)

                dns_header_t *dns_hdr = (dns_header_t *)rx_buffer;
                dns_hdr->nscount = 0;
                dns_hdr->arcount = 0;

                if (qtype == 0x001C) {
                    // Type AAAA (IPv6): Return NOERROR with 0 answers so Windows/Mac immediately fall back to IPv4
                    dns_hdr->flags = htons(0x8180);
                    dns_hdr->ancount = 0;
                    sendto(s_dns_sock, rx_buffer, question_end, 0,
                           (struct sockaddr *)&client_addr, client_addr_len);
                } else {
                    // Type A (IPv4) or any other query: return 192.168.4.1
                    dns_hdr->flags = htons(0x8180);
                    dns_hdr->ancount = htons(1);

                    if (question_end + 16 < (int)sizeof(rx_buffer)) {
                        uint8_t *ans = &rx_buffer[question_end];
                        ans[0] = 0xC0; ans[1] = 0x0C; // Pointer to query name at offset 12
                        ans[2] = 0x00; ans[3] = 0x01; // Type A
                        ans[4] = 0x00; ans[5] = 0x01; // Class IN
                        ans[6] = 0x00; ans[7] = 0x00; ans[8] = 0x00; ans[9] = 0x3C; // TTL 60s
                        ans[10] = 0x00; ans[11] = 0x04; // Len 4 bytes
                        ans[12] = 192; ans[13] = 168; ans[14] = 4; ans[15] = 1; // 192.168.4.1

                        sendto(s_dns_sock, rx_buffer, question_end + 16, 0,
                               (struct sockaddr *)&client_addr, client_addr_len);
                    }
                }
            }
        }
    }

    if (s_dns_sock >= 0) {
        close(s_dns_sock);
        s_dns_sock = -1;
    }
    s_dns_task_handle = NULL;
    vTaskDelete(NULL);
}

// ─── SNTP Time Sync Callback ─────────────────────────────────────────────────
static void time_sync_notification_cb(struct timeval *tv) {
    ESP_LOGI(TAG, "NTP Time synchronized successfully!");
    s_time_synced = true;
}

// ─── WiFi & IP Event Handlers ────────────────────────────────────────────────
static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        if (wifi_manager_has_saved_credentials()) {
            esp_wifi_connect();
        } else {
            // Station interface started in APSTA mode: kick off initial background scan!
            wifi_manager_trigger_scan();
        }
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_SCAN_DONE) {
        uint16_t ap_num = 0;
        esp_wifi_scan_get_ap_num(&ap_num);
        ESP_LOGI(TAG, "Background Wi-Fi scan completed! Found %u APs", ap_num);

        if (ap_num > 0) {
            uint16_t fetch_num = (ap_num > 32) ? 32 : ap_num;
            wifi_ap_record_t *raw_records = (wifi_ap_record_t *)malloc(sizeof(wifi_ap_record_t) * fetch_num);
            if (raw_records) {
                if (esp_wifi_scan_get_ap_records(&fetch_num, raw_records) == ESP_OK) {
                    s_cached_ap_count = 0;
                    for (int i = 0; i < fetch_num; i++) {
                        if (strlen((char *)raw_records[i].ssid) == 0) continue;

                        int found_idx = -1;
                        for (int j = 0; j < s_cached_ap_count; j++) {
                            if (strcmp(s_cached_aps[j].ssid, (char *)raw_records[i].ssid) == 0) {
                                found_idx = j;
                                break;
                            }
                        }
                        if (found_idx >= 0) {
                            if (raw_records[i].rssi > s_cached_aps[found_idx].rssi) {
                                s_cached_aps[found_idx].rssi = raw_records[i].rssi;
                                s_cached_aps[found_idx].authmode = raw_records[i].authmode;
                            }
                        } else if (s_cached_ap_count < MAX_CACHED_APS) {
                            strncpy(s_cached_aps[s_cached_ap_count].ssid, (char *)raw_records[i].ssid, sizeof(s_cached_aps[s_cached_ap_count].ssid) - 1);
                            s_cached_aps[s_cached_ap_count].rssi = raw_records[i].rssi;
                            s_cached_aps[s_cached_ap_count].authmode = raw_records[i].authmode;
                            s_cached_ap_count++;
                        }
                    }

                    // Sort descending by RSSI (strongest signal first)
                    for (int i = 0; i < s_cached_ap_count - 1; i++) {
                        for (int j = i + 1; j < s_cached_ap_count; j++) {
                            if (s_cached_aps[j].rssi > s_cached_aps[i].rssi) {
                                wifi_scanned_ap_t tmp = s_cached_aps[i];
                                s_cached_aps[i] = s_cached_aps[j];
                                s_cached_aps[j] = tmp;
                            }
                        }
                    }
                    s_last_scan_time_ms = esp_timer_get_time() / 1000ULL;
                    ESP_LOGI(TAG, "Cached %u unique APs sorted by signal strength", s_cached_ap_count);
                }
                free(raw_records);
            } else {
                esp_wifi_clear_ap_list();
            }
        } else {
            esp_wifi_clear_ap_list();
        }
        s_is_scanning = false;
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        s_rssi = -100;
        snprintf(s_sta_ip, sizeof(s_sta_ip), "Not Connected");

        if (s_setup_status == WIFI_SETUP_CONNECTING) {
            s_retry_count++;
            if (s_retry_count >= 3) {
                s_setup_status = WIFI_SETUP_FAILED;
                ESP_LOGW(TAG, "Failed to connect to '%s' after %d retries", s_connecting_ssid, s_retry_count);
            } else {
                esp_wifi_connect();
            }
        } else {
            // Normal background reconnect attempt
            esp_wifi_connect();
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        snprintf(s_sta_ip, sizeof(s_sta_ip), IPSTR, IP2STR(&event->ip_info.ip));
        ESP_LOGI(TAG, "Connected to Home WiFi! Dynamic IP: %s", s_sta_ip);

        wifi_ap_record_t ap_info;
        if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
            s_rssi = ap_info.rssi;
        }

        // Successfully connected!
        s_setup_status = WIFI_SETUP_CONNECTED;

        // Persist verified working credentials in NVS
        if (strlen(s_connecting_ssid) > 0) {
            nvs_handle_t nvs_h;
            if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_h) == ESP_OK) {
                nvs_set_str(nvs_h, "ssid", s_connecting_ssid);
                nvs_set_str(nvs_h, "pass", s_pending_pass);
                nvs_commit(nvs_h);
                nvs_close(nvs_h);
                ESP_LOGI(TAG, "Successfully committed working WiFi credentials to NVS: SSID='%s'", s_connecting_ssid);
            }
        }

        // Force immediate SNTP time sync
        esp_sntp_restart();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STACONNECTED) {
        wifi_event_ap_staconnected_t *event = (wifi_event_ap_staconnected_t *)event_data;
        ESP_LOGI(TAG, "Customer joined robot setup AP, AID=%d", event->aid);
    }
}

bool wifi_manager_has_saved_credentials(void) {
    nvs_handle_t nvs_h;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs_h) == ESP_OK) {
        char dummy[33];
        size_t len = sizeof(dummy);
        esp_err_t err = nvs_get_str(nvs_h, "ssid", dummy, &len);
        nvs_close(nvs_h);
        return (err == ESP_OK && len > 1);
    }
    return false;
}

bool wifi_manager_get_saved_ssid(char *out_ssid, size_t max_len) {
    if (!out_ssid || max_len == 0) return false;
    nvs_handle_t nvs_h;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs_h) == ESP_OK) {
        size_t len = max_len;
        esp_err_t err = nvs_get_str(nvs_h, "ssid", out_ssid, &len);
        nvs_close(nvs_h);
        return (err == ESP_OK && len > 1);
    }
    return false;
}

esp_err_t wifi_manager_start_setup_ap(const char *ap_ssid, const char *ap_password) {
    const char *ssid = ap_ssid ? ap_ssid : "PHOENIX-SETUP";
    const char *pwd  = ap_password ? ap_password : "gamephoenix";

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));

    wifi_config_t ap_config = {0};
    strncpy((char *)ap_config.ap.ssid, ssid, sizeof(ap_config.ap.ssid));
    ap_config.ap.ssid_len = strlen(ssid);
    ap_config.ap.channel = 1;
    ap_config.ap.max_connection = 4;

    if (pwd && strlen(pwd) >= 8) {
        strncpy((char *)ap_config.ap.password, pwd, sizeof(ap_config.ap.password));
        ap_config.ap.authmode = WIFI_AUTH_WPA2_PSK;
    } else {
        ap_config.ap.authmode = WIFI_AUTH_OPEN;
    }
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_config));

    s_is_ap_running = true;
    s_setup_status = WIFI_SETUP_WAITING;

    // Start DNS Captive Portal server if not already running
    if (s_dns_task_handle == NULL) {
        xTaskCreatePinnedToCore(
            dns_server_task,
            "dns_task",
            3072,
            NULL,
            3,
            &s_dns_task_handle,
            0
        );
    }

    ESP_LOGI(TAG, "Onboarding Setup SoftAP started: SSID='%s', PWD='%s', IP=%s", ssid, pwd, s_ap_ip);
    return ESP_OK;
}

esp_err_t wifi_manager_stop_ap(void) {
    if (!s_is_ap_running) return ESP_OK;

    ESP_LOGI(TAG, "Terminating Setup SoftAP and DNS Captive Portal...");

    // Stop DNS socket and task
    if (s_dns_sock >= 0) {
        int sock = s_dns_sock;
        s_dns_sock = -1;
        close(sock);
    }
    if (s_dns_task_handle != NULL) {
        vTaskDelete(s_dns_task_handle);
        s_dns_task_handle = NULL;
    }

    // Permanently switch to pure STA mode (turns off SoftAP radio completely)
    esp_err_t err = esp_wifi_set_mode(WIFI_MODE_STA);
    s_is_ap_running = false;
    ESP_LOGI(TAG, "Robot is now running purely in STA mode on the customer's home router! SoftAP is OFF.");
    return err;
}

esp_err_t wifi_manager_init(const wifi_manager_config_t *cfg) {
    // 1. Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    if (ret != ESP_OK) return ret;

    // 2. Initialize TCP/IP and event loop
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    s_netif_ap  = esp_netif_create_default_wifi_ap();
    s_netif_sta = esp_netif_create_default_wifi_sta();

    // 3. Initialize WiFi driver
    wifi_init_config_t wifi_cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&wifi_cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL));

    // 4. Initialize SNTP for time sync
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_setservername(1, "time.google.com");
    esp_sntp_set_time_sync_notification_cb(time_sync_notification_cb);
    esp_sntp_init();

    // Set timezone to Iran Standard Time (IRST = UTC+3:30 = 12600 seconds) matching COD2.ino
    setenv("TZ", "IRST-3:30", 1);
    tzset();

    // 5. Check if saved router credentials exist in NVS
    char saved_ssid[33] = {0};
    char saved_pass[65] = {0};

    if (wifi_manager_get_saved_ssid(saved_ssid, sizeof(saved_ssid))) {
        nvs_handle_t nvs_h;
        if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs_h) == ESP_OK) {
            size_t pass_len = sizeof(saved_pass);
            nvs_get_str(nvs_h, "pass", saved_pass, &pass_len);
            nvs_close(nvs_h);
        }

        // Credentials exist: Boot directly in pure STA mode (NO separate AP broadcast!)
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));

        wifi_config_t sta_config = {0};
        strncpy((char *)sta_config.sta.ssid, saved_ssid, sizeof(sta_config.sta.ssid) - 1);
        strncpy((char *)sta_config.sta.password, saved_pass, sizeof(sta_config.sta.password) - 1);
        sta_config.sta.threshold.authmode = (strlen(saved_pass) > 0) ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta_config));

        strncpy(s_connecting_ssid, saved_ssid, sizeof(s_connecting_ssid));
        s_setup_status = WIFI_SETUP_CONNECTING;
        s_is_ap_running = false;

        ESP_ERROR_CHECK(esp_wifi_start());
        ESP_LOGI(TAG, "Saved router credentials found! Booting in pure STA mode for SSID: '%s'", saved_ssid);
    } else {
        // First Ever Run: Start Onboarding SoftAP + DNS Captive Portal!
        const char *ap_name = (cfg && cfg->ap_ssid) ? cfg->ap_ssid : "PHOENIX-SETUP";
        const char *ap_pass = (cfg && cfg->ap_password) ? cfg->ap_password : "gamephoenix";

        wifi_manager_start_setup_ap(ap_name, ap_pass);
        ESP_ERROR_CHECK(esp_wifi_start());
        ESP_LOGI(TAG, "First ever run: No saved Wi-Fi credentials. Waiting for customer setup on AP: '%s'", ap_name);
    }

    return ESP_OK;
}

esp_err_t wifi_manager_connect_sta(const char *ssid, const char *password) {
    if (!ssid || strlen(ssid) == 0) return ESP_ERR_INVALID_ARG;

    strncpy(s_connecting_ssid, ssid, sizeof(s_connecting_ssid) - 1);
    strncpy(s_pending_pass, password ? password : "", sizeof(s_pending_pass) - 1);
    s_retry_count = 0;
    s_setup_status = WIFI_SETUP_CONNECTING;

    wifi_config_t sta_config = {0};
    strncpy((char *)sta_config.sta.ssid, ssid, sizeof(sta_config.sta.ssid) - 1);
    if (password && strlen(password) > 0) {
        strncpy((char *)sta_config.sta.password, password, sizeof(sta_config.sta.password) - 1);
        sta_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    } else {
        sta_config.sta.threshold.authmode = WIFI_AUTH_OPEN;
    }

    esp_wifi_disconnect();
    esp_err_t err = esp_wifi_set_config(WIFI_IF_STA, &sta_config);
    if (err == ESP_OK) {
        err = esp_wifi_connect();
    }
    ESP_LOGI(TAG, "Connecting to customer router: '%s'...", ssid);
    return err;
}

esp_err_t wifi_manager_clear_credentials(void) {
    nvs_handle_t nvs_h;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_h) == ESP_OK) {
        nvs_erase_all(nvs_h);
        nvs_commit(nvs_h);
        nvs_close(nvs_h);
        ESP_LOGI(TAG, "Erased WiFi credentials from NVS (Factory Reset)");
    }
    s_connecting_ssid[0] = '\0';
    s_pending_pass[0] = '\0';
    esp_wifi_disconnect();
    return ESP_OK;
}

bool wifi_manager_is_sta_connected(void) {
    if (!s_netif_sta) return false;
    esp_netif_ip_info_t ip_info;
    if (esp_netif_get_ip_info(s_netif_sta, &ip_info) == ESP_OK) {
        return (ip_info.ip.addr != 0);
    }
    return false;
}

wifi_setup_status_t wifi_manager_get_setup_status(void) {
    return s_setup_status;
}

void wifi_manager_reset_setup_status(void) {
    s_setup_status = WIFI_SETUP_WAITING;
}

const char* wifi_manager_get_connecting_ssid(void) {
    return s_connecting_ssid;
}

int wifi_manager_get_rssi_percent(void) {
    if (!wifi_manager_is_sta_connected()) return 0;
    int pct = (s_rssi + 100) * 2;
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    return pct;
}

const char* wifi_manager_get_ap_ip(void) {
    if (s_netif_ap) {
        esp_netif_ip_info_t ip_info;
        if (esp_netif_get_ip_info(s_netif_ap, &ip_info) == ESP_OK && ip_info.ip.addr != 0) {
            esp_ip4addr_ntoa(&ip_info.ip, s_ap_ip, sizeof(s_ap_ip));
            return s_ap_ip;
        }
    }
    return "192.168.4.1";
}

const char* wifi_manager_get_sta_ip(void) {
    if (s_netif_sta) {
        esp_netif_ip_info_t ip_info;
        if (esp_netif_get_ip_info(s_netif_sta, &ip_info) == ESP_OK && ip_info.ip.addr != 0) {
            esp_ip4addr_ntoa(&ip_info.ip, s_sta_ip, sizeof(s_sta_ip));
            return s_sta_ip;
        }
    }
    return "Not Connected";
}

bool wifi_manager_is_time_synced(void) {
    return s_time_synced;
}

esp_err_t wifi_manager_trigger_scan(void) {
    if (s_is_scanning) {
        return ESP_OK;
    }
    s_is_scanning = true;
    // Pass NULL for default scan parameters (coexistence tuned by Espressif)
    // Pass block = false to return immediately without blocking caller
    esp_err_t err = esp_wifi_scan_start(NULL, false);
    if (err != ESP_OK) {
        s_is_scanning = false;
        ESP_LOGW(TAG, "Background Wi-Fi scan start failed: %s", esp_err_to_name(err));
    } else {
        ESP_LOGI(TAG, "Background Wi-Fi scan triggered successfully");
    }
    return err;
}

bool wifi_manager_is_scanning(void) {
    return s_is_scanning;
}

uint16_t wifi_manager_get_cached_aps(wifi_scanned_ap_t *out_records, uint16_t max_records) {
    if (!out_records || max_records == 0) return 0;
    uint16_t count = (s_cached_ap_count < max_records) ? s_cached_ap_count : max_records;
    memcpy(out_records, s_cached_aps, sizeof(wifi_scanned_ap_t) * count);
    return count;
}

esp_err_t wifi_manager_scan_networks(wifi_ap_record_t *records, uint16_t max_records, uint16_t *out_count) {
    if (!records || !out_count || max_records == 0) return ESP_ERR_INVALID_ARG;

    // Fast zero-latency return from cache
    if (s_cached_ap_count > 0) {
        uint16_t n = (s_cached_ap_count < max_records) ? s_cached_ap_count : max_records;
        memset(records, 0, sizeof(wifi_ap_record_t) * n);
        for (uint16_t i = 0; i < n; i++) {
            strncpy((char *)records[i].ssid, s_cached_aps[i].ssid, sizeof(records[i].ssid) - 1);
            records[i].rssi = s_cached_aps[i].rssi;
            records[i].authmode = (wifi_auth_mode_t)s_cached_aps[i].authmode;
        }
        *out_count = n;

        // Refresh cache in background if older than 10 seconds
        uint64_t now_ms = esp_timer_get_time() / 1000ULL;
        if (now_ms - s_last_scan_time_ms > 10000ULL && !s_is_scanning) {
            wifi_manager_trigger_scan();
        }
        return ESP_OK;
    }

    if (!s_is_scanning) {
        wifi_manager_trigger_scan();
    }
    *out_count = 0;
    return ESP_OK;
}
