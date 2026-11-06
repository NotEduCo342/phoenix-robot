#include "alarm_clock.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <ctype.h>
#include "esp_log.h"
#include "esp_http_server.h"
#include "esp_timer.h"
#include "cJSON.h"
#include "audio_manager.h"
#include "haptics.h"
#include "wifi_manager.h"

#include "web_assets.h"

static const char *TAG = "alarm_clock";

// ─── Internal Alarm State ────────────────────────────────────────────────────
static httpd_handle_t s_http_server = NULL;
static bool     s_alarm_active = false;
static bool     s_alarm_ringing = false;
static uint64_t s_alarm_target_ms = 0;
static uint64_t s_last_beep_ms = 0;
static bool     s_beep_state = false;

// ─── URL Decoder Helper ───────────────────────────────────────────────────────
static void url_decode(char *dst, const char *src, size_t max_len) {
    if (!dst || !src || max_len == 0) return;
    char a, b;
    size_t out_len = 0;
    while (*src && out_len < max_len - 1) {
        if ((*src == '%') &&
            ((a = src[1]) && (b = src[2])) &&
            (isxdigit((int)a) && isxdigit((int)b))) {
            if (a >= 'a' && a <= 'f') a -= ('a' - 10);
            else if (a >= 'A' && a <= 'F') a -= ('A' - 10);
            else a -= '0';
            if (b >= 'a' && b <= 'f') b -= ('a' - 10);
            else if (b >= 'A' && b <= 'F') b -= ('A' - 10);
            else b -= '0';
            *dst++ = 16 * a + b;
            src += 3;
        } else if (*src == '+') {
            *dst++ = ' ';
            src++;
        } else {
            *dst++ = *src++;
        }
        out_len++;
    }
    *dst = '\0';
}

// ─── Static Web Asset Handlers ───────────────────────────────────────────────
static esp_err_t index_get_handler(httpd_req_t *req) {
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_send(req, (const char *)index_html, index_html_len);
    return ESP_OK;
}

static esp_err_t style_get_handler(httpd_req_t *req) {
    httpd_resp_set_type(req, "text/css");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(req, "Cache-Control", "max-age=3600");
    httpd_resp_send(req, (const char *)style_css, style_css_len);
    return ESP_OK;
}

static esp_err_t app_js_get_handler(httpd_req_t *req) {
    httpd_resp_set_type(req, "application/javascript");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(req, "Cache-Control", "max-age=3600");
    httpd_resp_send(req, (const char *)app_js, app_js_len);
    return ESP_OK;
}

// ─── REST API: GET /api/scan ─────────────────────────────────────────────────
static esp_err_t api_scan_handler(httpd_req_t *req) {
    size_t qlen = httpd_req_get_url_query_len(req);
    if (qlen > 0) {
        char *q = (char *)malloc(qlen + 1);
        if (q) {
            if (httpd_req_get_url_query_str(req, q, qlen + 1) == ESP_OK) {
                char val[16];
                if (httpd_query_key_value(q, "refresh", val, sizeof(val)) == ESP_OK) {
                    wifi_manager_trigger_scan();
                }
            }
            free(q);
        }
    }

    wifi_scanned_ap_t cached_aps[20];
    uint16_t count = wifi_manager_get_cached_aps(cached_aps, 20);
    bool is_scanning = wifi_manager_is_scanning();

    if (count == 0 && !is_scanning) {
        wifi_manager_trigger_scan();
        is_scanning = true;
    }

    char *json = (char *)malloc(2048);
    if (!json) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    int pos = snprintf(json, 2048, "{\"scanning\":%s,\"count\":%u,\"networks\":[",
                       is_scanning ? "true" : "false", count);

    for (uint16_t i = 0; i < count; i++) {
        if (strlen(cached_aps[i].ssid) == 0) continue;
        int pct = (cached_aps[i].rssi + 100) * 2;
        if (pct < 0) pct = 0;
        if (pct > 100) pct = 100;
        bool is_locked = (cached_aps[i].authmode != WIFI_AUTH_OPEN);

        char item[128];
        int ilen = snprintf(item, sizeof(item), "%s{\"ssid\":\"%s\",\"rssi\":%d,\"pct\":%d,\"lock\":%s}",
                            (i > 0) ? "," : "",
                            cached_aps[i].ssid,
                            cached_aps[i].rssi,
                            pct,
                            is_locked ? "true" : "false");
        if (pos + ilen < 2040) {
            strcpy(json + pos, item);
            pos += ilen;
        }
    }
    strcpy(json + pos, "]}");

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache, no-store, must-revalidate");
    httpd_resp_send(req, json, HTTPD_RESP_USE_STRLEN);
    free(json);
    return ESP_OK;
}

// ─── REST API: POST /api/wifi (Connect to Home Router) ────────────────────────
static esp_err_t api_wifi_handler(httpd_req_t *req) {
    char ssid[64] = {0};
    char pass[64] = {0};

    // Check if payload is POST body JSON
    if (req->method == HTTP_POST && req->content_len > 0 && req->content_len < 512) {
        char *body = (char *)malloc(req->content_len + 1);
        if (body) {
            int ret = httpd_req_recv(req, body, req->content_len);
            if (ret > 0) {
                body[ret] = '\0';
                cJSON *root = cJSON_Parse(body);
                if (root) {
                    cJSON *j_ssid = cJSON_GetObjectItem(root, "ssid");
                    cJSON *j_pass = cJSON_GetObjectItem(root, "password");
                    if (j_ssid && cJSON_IsString(j_ssid)) {
                        strncpy(ssid, j_ssid->valuestring, sizeof(ssid) - 1);
                    }
                    if (j_pass && cJSON_IsString(j_pass)) {
                        strncpy(pass, j_pass->valuestring, sizeof(pass) - 1);
                    }
                    cJSON_Delete(root);
                }
            }
            free(body);
        }
    }

    // Fallback: Check URL query string
    if (strlen(ssid) == 0) {
        size_t qlen = httpd_req_get_url_query_len(req);
        if (qlen > 0) {
            char *q = (char *)malloc(qlen + 1);
            if (q) {
                if (httpd_req_get_url_query_str(req, q, qlen + 1) == ESP_OK) {
                    char raw_s[64] = {0};
                    char raw_p[64] = {0};
                    httpd_query_key_value(q, "ssid", raw_s, sizeof(raw_s));
                    httpd_query_key_value(q, "pass", raw_p, sizeof(raw_p));
                    url_decode(ssid, raw_s, sizeof(ssid));
                    url_decode(pass, raw_p, sizeof(pass));
                }
                free(q);
            }
        }
    }

    if (strlen(ssid) > 0) {
        ESP_LOGI(TAG, "API received Wi-Fi connection request: SSID='%s'", ssid);
        wifi_manager_connect_sta(ssid, pass);
        audio_play(SOUND_SCORE);
        haptics_pulse(100);

        httpd_resp_set_type(req, "application/json");
        httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
        httpd_resp_send(req, "{\"status\":\"connecting\"}", HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }

    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing SSID");
    return ESP_FAIL;
}

// ─── REST API: GET /api/status ───────────────────────────────────────────────
static esp_err_t api_status_handler(httpd_req_t *req) {
    wifi_setup_status_t status = wifi_manager_get_setup_status();
    const char *status_str = "waiting";
    if (status == WIFI_SETUP_CONNECTING) status_str = "connecting";
    else if (status == WIFI_SETUP_CONNECTED) status_str = "connected";
    else if (status == WIFI_SETUP_FAILED) status_str = "failed";

    bool setup_mode = !wifi_manager_is_sta_connected();

    char json[256];
    snprintf(json, sizeof(json),
             "{\"setup_mode\":%s,\"status\":\"%s\",\"sta_ip\":\"%s\",\"ap_ip\":\"%s\",\"ssid\":\"%s\",\"rssi_pct\":%d,\"time_synced\":%s}",
             setup_mode ? "true" : "false",
             status_str,
             wifi_manager_get_sta_ip(),
             wifi_manager_get_ap_ip(),
             wifi_manager_get_connecting_ssid(),
             wifi_manager_get_rssi_percent(),
             wifi_manager_is_time_synced() ? "true" : "false");

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache, no-store, must-revalidate");
    httpd_resp_send(req, json, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

// ─── REST API: GET & POST /api/alarm ─────────────────────────────────────────
static esp_err_t api_alarm_get_handler(httpd_req_t *req) {
    uint64_t now_ms = esp_timer_get_time() / 1000ULL;
    long rem_sec = 0;
    if (s_alarm_active && s_alarm_target_ms > now_ms) {
        rem_sec = (long)((s_alarm_target_ms - now_ms) / 1000ULL);
    }

    char json[128];
    snprintf(json, sizeof(json),
             "{\"is_active\":%s,\"is_ringing\":%s,\"remaining_sec\":%ld}",
             s_alarm_active ? "true" : "false",
             s_alarm_ringing ? "true" : "false",
             rem_sec);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache, no-store, must-revalidate");
    httpd_resp_send(req, json, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static esp_err_t api_alarm_post_handler(httpd_req_t *req) {
    bool cancel = false;
    int h = 0, m = 0;

    // Check JSON body
    if (req->content_len > 0 && req->content_len < 256) {
        char *body = (char *)malloc(req->content_len + 1);
        if (body) {
            int ret = httpd_req_recv(req, body, req->content_len);
            if (ret > 0) {
                body[ret] = '\0';
                cJSON *root = cJSON_Parse(body);
                if (root) {
                    cJSON *j_cancel = cJSON_GetObjectItem(root, "cancel");
                    if (j_cancel && cJSON_IsTrue(j_cancel)) cancel = true;

                    cJSON *j_h = cJSON_GetObjectItem(root, "hours");
                    cJSON *j_m = cJSON_GetObjectItem(root, "minutes");
                    if (j_h && cJSON_IsNumber(j_h)) h = j_h->valueint;
                    if (j_m && cJSON_IsNumber(j_m)) m = j_m->valueint;

                    cJSON_Delete(root);
                }
            }
            free(body);
        }
    }

    // Check query string fallback
    size_t qlen = httpd_req_get_url_query_len(req);
    if (qlen > 0) {
        char *q = (char *)malloc(qlen + 1);
        if (q) {
            if (httpd_req_get_url_query_str(req, q, qlen + 1) == ESP_OK) {
                char val[32];
                if (httpd_query_key_value(q, "cancel", val, sizeof(val)) == ESP_OK) cancel = true;
                if (httpd_query_key_value(q, "h", val, sizeof(val)) == ESP_OK) h = atoi(val);
                if (httpd_query_key_value(q, "m", val, sizeof(val)) == ESP_OK) m = atoi(val);
            }
            free(q);
        }
    }

    if (cancel) {
        alarm_clock_cancel();
        audio_play(SOUND_CLICK);
        httpd_resp_set_type(req, "application/json");
        httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
        httpd_resp_send(req, "{\"success\":true,\"action\":\"cancelled\"}", HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }

    if (h > 0 || m > 0) {
        alarm_clock_set((uint32_t)h, (uint32_t)m);
        audio_play(SOUND_ALARM_BEEP);
        haptics_pulse(80);

        httpd_resp_set_type(req, "application/json");
        httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
        httpd_resp_send(req, "{\"success\":true,\"action\":\"set\"}", HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }

    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid alarm parameters");
    return ESP_FAIL;
}

// ─── Captive Portal 302 Redirect Handler ──────────────────────────────────────
static esp_err_t captive_redirect_handler(httpd_req_t *req) {
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "http://192.168.4.1/");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

// ─── Catch-all Fallback Handler ───────────────────────────────────────────────
static esp_err_t catchall_get_handler(httpd_req_t *req) {
    if (!wifi_manager_is_sta_connected()) {
        char host[64] = {0};
        if (httpd_req_get_hdr_value_str(req, "Host", host, sizeof(host)) == ESP_OK) {
            if (strstr(host, "192.168.4.1") == NULL) {
                httpd_resp_set_status(req, "302 Found");
                httpd_resp_set_hdr(req, "Location", "http://192.168.4.1/");
                httpd_resp_send(req, NULL, 0);
                return ESP_OK;
            }
        }
    }
    return index_get_handler(req);
}

// ─── Initialize HTTP Web Server ──────────────────────────────────────────────
esp_err_t alarm_clock_init(void) {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.uri_match_fn = httpd_uri_match_wildcard;
    config.max_uri_handlers = 24;
    config.stack_size = 12288;

    esp_err_t ret = httpd_start(&s_http_server, &config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start HTTP server: %s", esp_err_to_name(ret));
        return ret;
    }

    // 1. Static Assets
    httpd_uri_t uri_root = { .uri = "/", .method = HTTP_GET, .handler = index_get_handler };
    httpd_register_uri_handler(s_http_server, &uri_root);

    httpd_uri_t uri_index = { .uri = "/index.html*", .method = HTTP_GET, .handler = index_get_handler };
    httpd_register_uri_handler(s_http_server, &uri_index);

    httpd_uri_t uri_style = { .uri = "/style.css*", .method = HTTP_GET, .handler = style_get_handler };
    httpd_register_uri_handler(s_http_server, &uri_style);

    httpd_uri_t uri_app_js = { .uri = "/app.js*", .method = HTTP_GET, .handler = app_js_get_handler };
    httpd_register_uri_handler(s_http_server, &uri_app_js);

    // 2. REST API Endpoints
    httpd_uri_t uri_api_scan = { .uri = "/api/scan*", .method = HTTP_GET, .handler = api_scan_handler };
    httpd_register_uri_handler(s_http_server, &uri_api_scan);

    httpd_uri_t uri_api_wifi_post = { .uri = "/api/wifi*", .method = HTTP_POST, .handler = api_wifi_handler };
    httpd_register_uri_handler(s_http_server, &uri_api_wifi_post);

    httpd_uri_t uri_api_wifi_get = { .uri = "/api/wifi*", .method = HTTP_GET, .handler = api_wifi_handler };
    httpd_register_uri_handler(s_http_server, &uri_api_wifi_get);

    httpd_uri_t uri_api_status = { .uri = "/api/status*", .method = HTTP_GET, .handler = api_status_handler };
    httpd_register_uri_handler(s_http_server, &uri_api_status);

    httpd_uri_t uri_api_alarm_get = { .uri = "/api/alarm*", .method = HTTP_GET, .handler = api_alarm_get_handler };
    httpd_register_uri_handler(s_http_server, &uri_api_alarm_get);

    httpd_uri_t uri_api_alarm_post = { .uri = "/api/alarm*", .method = HTTP_POST, .handler = api_alarm_post_handler };
    httpd_register_uri_handler(s_http_server, &uri_api_alarm_post);

    // 3. Backward Compatibility Aliases
    httpd_uri_t uri_wf_scan = { .uri = "/wifi_scan*", .method = HTTP_GET, .handler = api_scan_handler };
    httpd_register_uri_handler(s_http_server, &uri_wf_scan);

    httpd_uri_t uri_wf_set = { .uri = "/wifi_set*", .method = HTTP_GET, .handler = api_wifi_handler };
    httpd_register_uri_handler(s_http_server, &uri_wf_set);

    httpd_uri_t uri_wf_status = { .uri = "/wifi_status*", .method = HTTP_GET, .handler = api_status_handler };
    httpd_register_uri_handler(s_http_server, &uri_wf_status);

    httpd_uri_t uri_legacy_set = { .uri = "/set*", .method = HTTP_GET, .handler = api_alarm_post_handler };
    httpd_register_uri_handler(s_http_server, &uri_legacy_set);

    httpd_uri_t uri_legacy_cancel = { .uri = "/cancel*", .method = HTTP_GET, .handler = api_alarm_post_handler };
    httpd_register_uri_handler(s_http_server, &uri_legacy_cancel);

    // 4. Captive Portal OS Probes
    httpd_uri_t uri_gen204 = { .uri = "/generate_204*", .method = HTTP_GET, .handler = captive_redirect_handler };
    httpd_register_uri_handler(s_http_server, &uri_gen204);

    httpd_uri_t uri_gen204_2 = { .uri = "/gen_204*", .method = HTTP_GET, .handler = captive_redirect_handler };
    httpd_register_uri_handler(s_http_server, &uri_gen204_2);

    httpd_uri_t uri_hotspot = { .uri = "/hotspot-detect.html*", .method = HTTP_GET, .handler = captive_redirect_handler };
    httpd_register_uri_handler(s_http_server, &uri_hotspot);

    httpd_uri_t uri_apple_success = { .uri = "/success.html*", .method = HTTP_GET, .handler = captive_redirect_handler };
    httpd_register_uri_handler(s_http_server, &uri_apple_success);

    httpd_uri_t uri_win_connect = { .uri = "/connecttest.txt*", .method = HTTP_GET, .handler = captive_redirect_handler };
    httpd_register_uri_handler(s_http_server, &uri_win_connect);

    httpd_uri_t uri_win_ncsi = { .uri = "/ncsi.txt*", .method = HTTP_GET, .handler = captive_redirect_handler };
    httpd_register_uri_handler(s_http_server, &uri_win_ncsi);

    // 5. Catchall
    httpd_uri_t uri_catchall = { .uri = "/*", .method = HTTP_GET, .handler = catchall_get_handler };
    httpd_register_uri_handler(s_http_server, &uri_catchall);

    ESP_LOGI(TAG, "GamePhoenix Web Server initialized with embedded HTML/CSS/JS & REST API");
    return ESP_OK;
}

// ─── Alarm State Machine Implementation ──────────────────────────────────────
void alarm_clock_set(uint32_t hours, uint32_t minutes) {
    uint64_t now_ms = esp_timer_get_time() / 1000ULL;
    uint64_t duration_ms = ((uint64_t)hours * 3600000ULL) + ((uint64_t)minutes * 60000ULL);
    s_alarm_target_ms = now_ms + duration_ms;
    s_alarm_active = true;
    s_alarm_ringing = false;
    ESP_LOGI(TAG, "Alarm activated! Duration: %lu hours, %lu mins (Target: %llu ms)", hours, minutes, s_alarm_target_ms);
}

void alarm_clock_cancel(void) {
    s_alarm_active = false;
    s_alarm_ringing = false;
    s_alarm_target_ms = 0;
    haptics_stop();
    audio_stop();
    ESP_LOGI(TAG, "Alarm cancelled");
}

void alarm_clock_dismiss(void) {
    if (s_alarm_ringing) {
        alarm_clock_cancel();
        audio_play(SOUND_CLICK);
    }
}

bool alarm_clock_is_ringing(void) {
    return s_alarm_ringing;
}

bool alarm_clock_is_active(void) {
    return s_alarm_active;
}

void alarm_clock_get_status(alarm_status_t *status) {
    if (!status) return;
    uint64_t now_ms = esp_timer_get_time() / 1000ULL;
    status->is_active = s_alarm_active;
    status->is_ringing = s_alarm_ringing;
    if (s_alarm_active && s_alarm_target_ms > now_ms) {
        status->remaining_seconds = (uint32_t)((s_alarm_target_ms - now_ms) / 1000ULL);
    } else {
        status->remaining_seconds = 0;
    }
}

void alarm_clock_update(void) {
    uint64_t now_ms = esp_timer_get_time() / 1000ULL;

    if (s_alarm_active && now_ms >= s_alarm_target_ms) {
        s_alarm_active = false;
        s_alarm_ringing = true;
        ESP_LOGW(TAG, "ALARM TRIGGERED! WAKE UP!");
    }

    if (s_alarm_ringing) {
        if (now_ms - s_last_beep_ms > 250ULL) {
            s_last_beep_ms = now_ms;
            s_beep_state = !s_beep_state;

            if (s_beep_state) {
                audio_play_tone(2000, 200);
                haptics_pulse(200);
            } else {
                audio_play_tone(1200, 200);
            }
        }
    }
}

// ─── Gregorian to Shamsi (Jalali) Conversion (from COD2.ino) ─────────────────
void alarm_clock_get_shamsi_date(int gY, int gM, int gD, char *out_str, size_t max_len) {
    int gy = gY - 1600;
    int gm = gM - 1;
    int gd = gD - 1;
    long g_day_no = 365 * gy + (gy + 3) / 4 - (gy + 99) / 100 + (gy + 399) / 400;
    int g_days_in_month[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (gM > 2 && ((gY % 4 == 0 && gY % 100 != 0) || (gY % 400 == 0))) {
        g_day_no++;
    }
    for (int i = 0; i < gm; ++i) {
        g_day_no += g_days_in_month[i];
    }
    g_day_no += gd;

    long j_day_no = g_day_no - 79;
    long j_np = j_day_no / 12053;
    j_day_no %= 12053;
    int jy = 979 + 33 * j_np + 4 * (j_day_no / 1461);
    j_day_no %= 1461;
    if (j_day_no >= 366) {
        jy += (j_day_no - 1) / 365;
        j_day_no = (j_day_no - 1) % 365;
    }
    int jm = 0, jd = 0;
    for (int i = 0; i < 11 && j_day_no >= (i < 6 ? 31 : 30); ++i) {
        j_day_no -= (i < 6 ? 31 : 30);
        jm = i + 1;
    }
    jm = jm + 1;
    jd = j_day_no + 1;

    snprintf(out_str, max_len, "%d/%02d/%02d", jy, jm, jd);
}

// ─── Render Clock & Alarm Screen on ST7735 160x128 ───────────────────────────
void alarm_clock_draw_screen(st7735_t *dev) {
    if (!dev) return;

    st7735_clear(dev, COLOR_BLACK);

    // 1. Top Bar
    st7735_draw_string(dev, 34, 3, "PHOENIX-ALARM", COLOR_YELLOW, COLOR_BLACK, 1);
    st7735_draw_fast_hline(dev, 0, 14, ST7735_WIDTH, COLOR_CYAN);

    // Dynamic WiFi signal bars
    int wifi_pct = wifi_manager_get_rssi_percent();
    int bars = (wifi_pct > 75) ? 4 : (wifi_pct > 50) ? 3 : (wifi_pct > 25) ? 2 : (wifi_pct > 0) ? 1 : 0;
    for (int i = 0; i < 4; i++) {
        int h = (i + 1) * 2;
        if (i < bars) st7735_fill_rect(dev, 4 + i * 4, 12 - h, 2, h, COLOR_EMERALD);
        else st7735_draw_rect(dev, 4 + i * 4, 12 - h, 2, h, 0x4208);
    }

    // Battery icon
    st7735_draw_rect(dev, 138, 3, 16, 8, COLOR_WHITE);
    st7735_fill_rect(dev, 154, 5, 2, 4, COLOR_WHITE);
    st7735_fill_rect(dev, 140, 5, 12, 4, COLOR_EMERALD);

    // 2. Digital Clock Time
    time_t now_sec;
    time(&now_sec);
    struct tm timeinfo;
    localtime_r(&now_sec, &timeinfo);

    char time_str[16];
    uint64_t now_ms = esp_timer_get_time() / 1000ULL;
    bool colon_blink = ((now_ms / 500ULL) % 2 == 0);

    if (wifi_manager_is_time_synced() || timeinfo.tm_year > (2020 - 1900)) {
        snprintf(time_str, sizeof(time_str), "%02d%c%02d",
                 timeinfo.tm_hour, colon_blink ? ':' : ' ', timeinfo.tm_min);
    } else {
        snprintf(time_str, sizeof(time_str), "%02lu%c%02lu",
                 (unsigned long)((now_ms / 3600000ULL) % 24),
                 colon_blink ? ':' : ' ',
                 (unsigned long)((now_ms / 60000ULL) % 60));
    }

    // Render big clock digits (size 3)
    st7735_draw_string(dev, 35, 22, time_str, COLOR_CYAN, COLOR_BLACK, 3);

    // 3. Persian Shamsi Date
    char date_str[32] = {0};
    if (wifi_manager_is_time_synced() || timeinfo.tm_year > (2020 - 1900)) {
        alarm_clock_get_shamsi_date(timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
                                   date_str, sizeof(date_str));
    } else {
        snprintf(date_str, sizeof(date_str), "Syncing NTP...");
    }
    st7735_draw_string(dev, 48, 52, date_str, COLOR_EMERALD, COLOR_BLACK, 1);

    // 4. Alarm Status Card (Y=66, H=26)
    if (s_alarm_active) {
        long rem_min = (long)((s_alarm_target_ms - now_ms) / 60000ULL) + 1;
        if (rem_min < 0) rem_min = 0;
        int rh = rem_min / 60;
        int rm = rem_min % 60;

        st7735_fill_round_rect(dev, 8, 66, 144, 26, 5, 0x0183);
        st7735_draw_round_rect(dev, 8, 66, 144, 26, 5, COLOR_CYAN);

        char card_str[48];
        snprintf(card_str, sizeof(card_str), "ALARM: ON (%02dh %02dm)", rh, rm);
        st7735_draw_string(dev, 14, 75, card_str, COLOR_WHITE, 0x0183, 1);
    } else {
        st7735_fill_round_rect(dev, 8, 66, 144, 26, 5, 0x18C3);
        st7735_draw_round_rect(dev, 8, 66, 144, 26, 5, 0x4208);
        st7735_draw_string(dev, 44, 75, "ALARM: OFF", 0x8410, 0x18C3, 1);
    }

    // 5. Dynamic Live Network Footer (Y=98, H=24)
    st7735_fill_round_rect(dev, 8, 98, 144, 24, 4, 0x0821);
    st7735_draw_round_rect(dev, 8, 98, 144, 24, 4, 0x2945);

    char net_info[48];
    if (wifi_manager_is_sta_connected()) {
        snprintf(net_info, sizeof(net_info), "IP: %s", wifi_manager_get_sta_ip());
        st7735_draw_string(dev, 16, 105, net_info, COLOR_ORANGE, 0x0821, 1);
    } else {
        snprintf(net_info, sizeof(net_info), "AP: %s", wifi_manager_get_ap_ip());
        st7735_draw_string(dev, 24, 105, net_info, COLOR_YELLOW, 0x0821, 1);
    }

    st7735_display(dev);
}

// ─── Render Urgent Alarm Ringing Screen ───────────────────────────────────────
void alarm_clock_draw_ringing_screen(st7735_t *dev) {
    if (!dev) return;

    uint64_t now_ms = esp_timer_get_time() / 1000ULL;
    bool flash = ((now_ms / 250ULL) % 2 == 0);

    uint16_t bg_color = flash ? COLOR_RED : COLOR_BLACK;
    uint16_t fg_color = flash ? COLOR_WHITE : COLOR_RED;

    st7735_clear(dev, bg_color);
    st7735_draw_round_rect(dev, 6, 6, 148, 116, 8, fg_color);
    st7735_draw_round_rect(dev, 8, 8, 144, 112, 6, fg_color);

    st7735_draw_string(dev, 16, 36, "WAKE UP!", fg_color, bg_color, 3);
    st7735_draw_string(dev, 22, 82, "Touch to Dismiss", COLOR_WHITE, bg_color, 1);

    st7735_display(dev);
}
