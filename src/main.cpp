#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_system.h"

// Project Components
#include "st7735.h"
#include "esp_robo_eyes.hpp"
#include "bmi160.h"
#include "audio_manager.h"
#include "haptics.h"
#include "wifi_manager.h"
#include "alarm_clock.h"

static const char *TAG = "DESK_ROBOT";

// ─── Hardware Pin Configuration ──────────────────────────────────────────────
// 1.8" SPI TFT (ST7735R 160x128)
#define PIN_TFT_MOSI        GPIO_NUM_11  // DIN
#define PIN_TFT_SCLK        GPIO_NUM_12  // CLK
#define PIN_TFT_CS          GPIO_NUM_13  // CS
#define PIN_TFT_DC          GPIO_NUM_14  // D/C
#define PIN_TFT_RST         GPIO_NUM_21  // RST
#define PIN_TFT_BL          GPIO_NUM_47  // BL

// TTP223 Capacitive Touch
#define PIN_TOUCH_SIG       GPIO_NUM_4

// BMI160 IMU (I2C_0)
#define PIN_I2C_SDA         GPIO_NUM_8
#define PIN_I2C_SCL         GPIO_NUM_9

// MAX98357A I2S Audio Amplifier (I2S_1)
#define PIN_I2S_BCLK        GPIO_NUM_5
#define PIN_I2S_WS          GPIO_NUM_6   // LRC
#define PIN_I2S_DOUT        GPIO_NUM_7   // DIN

// Coin Vibration Motor
#define PIN_VIBRO_SIG       GPIO_NUM_10

static st7735_t      s_display;
static EspRoboEyes   s_roboEyes;

// Robot Operating Modes (matching all 5 modes of COD2.ino)
typedef enum {
    ROBOT_MODE_EYES      = 0,
    ROBOT_MODE_CLOCK     = 1,
    ROBOT_MODE_TELEMETRY = 2,
    ROBOT_MODE_DINO      = 3,
    ROBOT_MODE_CAR       = 4
} robot_mode_t;

// Inactivity sleep timeout (2 minutes matching COD2.ino)
#define INACTIVITY_TIMEOUT_MS 120000ULL

// Shared state between Core 0 and Core 1
typedef struct {
    volatile robot_mode_t         current_mode;
    volatile bool                 touch_active;
    volatile bool                 touch_just_pressed;
    volatile bool                 motion_detected;
    volatile bmi160_motion_mood_t motion_mood;
    volatile uint8_t              gaze_direction;
    volatile float                tilt_ax;
    volatile float                tilt_ay;
    volatile float                tilt_az;
    volatile uint64_t             last_activity_ms;
    volatile bool                 is_sleeping;
} robot_shared_state_t;

static robot_shared_state_t s_robot_state = {
    .current_mode = ROBOT_MODE_EYES,
    .touch_active = false,
    .touch_just_pressed = false,
    .motion_detected = false,
    .motion_mood = BMI160_MOOD_NEUTRAL,
    .gaze_direction = DEFAULT,
    .tilt_ax = 0.0f,
    .tilt_ay = 0.0f,
    .tilt_az = 1.0f,
    .last_activity_ms = 0,
    .is_sleeping = false
};

// ─── Chrome Dino Jump Game Variables (Mode 3 matching COD2.ino) ─────────────
static const unsigned char dinoSprite[] = {
    0x00, 0xfe, 0x01, 0xff, 0x01, 0xdf, 0x01, 0xff, 0x01, 0xff, 0x01, 0xc0, 0x01, 0xf8, 0x83, 0xc0,
    0x87, 0xf0, 0xcf, 0xd0, 0xff, 0xc0, 0x7f, 0xc0, 0x3f, 0x80, 0x1f, 0x00, 0x12, 0x00, 0x1b, 0x00
};

static float         s_dino_y = 84.0f;
static float         s_dino_vy = 0.0f;
static const float   s_dino_gravity = 0.65f;
static const float   s_dino_jump_power = -7.5f;
static bool          s_dino_is_jumping = false;
static float         s_dino_obstacle_x = 160.0f;
static int           s_dino_obstacle_type = 0;
static float         s_dino_speed = 3.5f;
static uint32_t      s_dino_score = 0;
static uint32_t      s_dino_high_score = 0;
static bool          s_dino_game_over = false;
static uint64_t      s_last_dino_tick = 0;

static void reset_dino_game(void) {
    s_dino_y = 84.0f;
    s_dino_vy = 0.0f;
    s_dino_is_jumping = false;
    s_dino_obstacle_x = 160.0f;
    s_dino_speed = 3.5f;
    s_dino_score = 0;
    s_dino_game_over = false;
    audio_play_tone(1000, 60);
}

static void handle_dino_game(st7735_t *dev, uint64_t now_ms, bool touch_pressed, bool touch_just_pressed) {
    if (s_dino_game_over) {
        if (touch_just_pressed) reset_dino_game();
        return;
    }

    if (now_ms - s_last_dino_tick < 25ULL) return;
    s_last_dino_tick = now_ms;

    // Jump trigger
    if (touch_pressed && !s_dino_is_jumping) {
        s_dino_vy = s_dino_jump_power;
        s_dino_is_jumping = true;
        audio_play(SOUND_JUMP);
    }

    // Physics
    s_dino_y += s_dino_vy;
    s_dino_vy += s_dino_gravity;
    if (s_dino_y >= 84.0f) {
        s_dino_y = 84.0f;
        s_dino_vy = 0.0f;
        s_dino_is_jumping = false;
    }

    // Obstacle movement
    s_dino_obstacle_x -= s_dino_speed;
    if (s_dino_obstacle_x < -18.0f) {
        s_dino_obstacle_x = 160.0f + (rand() % 40);
        s_dino_obstacle_type = rand() % 3;
        s_dino_score++;
        audio_play(SOUND_SCORE);
        if (s_dino_score > s_dino_high_score) s_dino_high_score = s_dino_score;
        if (s_dino_speed < 7.5f) s_dino_speed += 0.15f;
    }

    int obs_w = (s_dino_obstacle_type == 2) ? 14 : (s_dino_obstacle_type == 1 ? 8 : 6);
    int obs_h = (s_dino_obstacle_type == 1) ? 16 : 12;
    int obs_y = 100 - obs_h;

    // Collision check
    if (s_dino_obstacle_x < 36.0f && (s_dino_obstacle_x + obs_w) > 20.0f) {
        if ((s_dino_y + 14.0f) > obs_y) {
            s_dino_game_over = true;
            audio_play(SOUND_GAME_OVER);
            haptics_pulse(250);
        }
    }

    // Render Dino Frame
    st7735_clear(dev, COLOR_BLACK);

    // Score Banner
    st7735_fill_rect(dev, 0, 0, 160, 14, 0x18E3);
    st7735_draw_string(dev, 8, 3, "HI:", COLOR_CYAN, 0x18E3, 1);
    char hi_str[16], sc_str[16];
    snprintf(hi_str, sizeof(hi_str), "%lu", s_dino_high_score);
    snprintf(sc_str, sizeof(sc_str), "%lu", s_dino_score);
    st7735_draw_string(dev, 32, 3, hi_str, COLOR_WHITE, 0x18E3, 1);
    st7735_draw_string(dev, 90, 3, "SC:", COLOR_YELLOW, 0x18E3, 1);
    st7735_draw_string(dev, 114, 3, sc_str, COLOR_WHITE, 0x18E3, 1);

    // Ground line
    st7735_draw_fast_hline(dev, 0, 100, 160, COLOR_WHITE);

    // Dino Sprite
    st7735_draw_bitmap(dev, 20, (int16_t)s_dino_y, dinoSprite, 16, 16, COLOR_EMERALD, COLOR_BLACK);

    // Running legs animation
    if (!s_dino_is_jumping) {
        if ((now_ms / 100ULL) % 2 == 0) st7735_fill_rect(dev, 22, 98, 3, 2, COLOR_BLACK);
        else st7735_fill_rect(dev, 28, 98, 3, 2, COLOR_BLACK);
    }

    // Cactus
    int ox = (int)s_dino_obstacle_x;
    if (s_dino_obstacle_type == 0) {
        st7735_fill_rect(dev, ox, 88, 6, 12, COLOR_ORANGE);
        st7735_fill_rect(dev, ox - 2, 91, 2, 5, COLOR_ORANGE);
        st7735_fill_rect(dev, ox + 6, 90, 2, 5, COLOR_ORANGE);
    } else if (s_dino_obstacle_type == 1) {
        st7735_fill_rect(dev, ox, 84, 8, 16, COLOR_ORANGE);
        st7735_fill_rect(dev, ox - 3, 87, 3, 6, COLOR_ORANGE);
        st7735_fill_rect(dev, ox + 8, 86, 3, 6, COLOR_ORANGE);
    } else {
        st7735_fill_rect(dev, ox, 88, 5, 12, COLOR_ORANGE);
        st7735_fill_rect(dev, ox + 8, 86, 6, 14, COLOR_ORANGE);
    }

    // Game Over Overlay
    if (s_dino_game_over) {
        st7735_fill_round_rect(dev, 18, 32, 124, 46, 6, COLOR_BLACK);
        st7735_draw_round_rect(dev, 18, 32, 124, 46, 6, COLOR_RED);
        st7735_fill_round_rect(dev, 18, 32, 124, 18, 6, COLOR_RED);
        st7735_draw_string(dev, 44, 36, "GAME OVER", COLOR_WHITE, COLOR_RED, 1);
        st7735_draw_string(dev, 30, 58, "Touch to Replay", COLOR_YELLOW, COLOR_BLACK, 1);
    }

    st7735_display(dev);
}

// ─── Gyroscope Tilt Car Racing Game Variables (Mode 4) ───────────────────────
static float         s_car_x = 74.0f;
static float         s_enemy_x = 74.0f;
static float         s_enemy_y = -20.0f;
static float         s_car_speed = 3.0f;
static int           s_road_offset = 0;
static uint32_t      s_car_score = 0;
static uint32_t      s_car_high_score = 0;
static bool          s_car_game_over = false;
static uint64_t      s_last_car_tick = 0;

static void reset_car_game(void) {
    s_car_x = 74.0f;
    s_enemy_y = -20.0f;
    int lanes[3] = {46, 74, 102};
    s_enemy_x = lanes[rand() % 3];
    s_car_speed = 3.0f;
    s_car_score = 0;
    s_car_game_over = false;
    audio_play_tone(1000, 60);
}

static void draw_car_shape(st7735_t *dev, int x, int y, uint16_t color) {
    st7735_fill_round_rect(dev, x + 2, y, 10, 18, 3, color);
    st7735_fill_rect(dev, x, y + 2, 2, 4, COLOR_WHITE);       // Left front wheel
    st7735_fill_rect(dev, x + 12, y + 2, 2, 4, COLOR_WHITE);  // Right front wheel
    st7735_fill_rect(dev, x, y + 12, 2, 4, COLOR_WHITE);      // Left rear wheel
    st7735_fill_rect(dev, x + 12, y + 12, 2, 4, COLOR_WHITE); // Right rear wheel
    st7735_fill_rect(dev, x + 4, y + 5, 6, 4, COLOR_BLACK);   // Windshield
}

static void handle_car_game(st7735_t *dev, uint64_t now_ms, bool touch_just_pressed, float tilt_ax, float tilt_ay) {
    if (s_car_game_over) {
        if (touch_just_pressed) reset_car_game();
        return;
    }

    if (now_ms - s_last_car_tick < 25ULL) return;
    s_last_car_tick = now_ms;

    // Steering from BMI160 tilt
    float tilt = (fabsf(tilt_ay) > fabsf(tilt_ax)) ? tilt_ay : tilt_ax;
    if (fabsf(tilt) > 0.08f) {
        s_car_x -= tilt * 8.0f;
    }

    // Road boundaries
    if (s_car_x < 36.0f) s_car_x = 36.0f;
    if (s_car_x > 110.0f) s_car_x = 110.0f;

    // Enemy movement
    s_enemy_y += s_car_speed;
    s_road_offset = (s_road_offset + (int)s_car_speed) % 20;

    if (s_enemy_y > 130.0f) {
        s_enemy_y = -20.0f;
        int lanes[3] = {46, 74, 102};
        s_enemy_x = lanes[rand() % 3];
        s_car_score++;
        audio_play(SOUND_SCORE);
        if (s_car_score > s_car_high_score) s_car_high_score = s_car_score;
        if (s_car_speed < 7.0f) s_car_speed += 0.15f;
    }

    // Collision check
    if (s_enemy_y + 16.0f >= 96.0f && s_enemy_y <= 116.0f) {
        if (fabsf(s_car_x - s_enemy_x) < 13.0f) {
            s_car_game_over = true;
            audio_play(SOUND_CRASH);
            haptics_pulse(300);
        }
    }

    // Render Car Game Frame
    st7735_clear(dev, 0x0821);

    // Road Borders
    st7735_draw_fast_vline(dev, 34, 0, 128, COLOR_WHITE);
    st7735_draw_fast_vline(dev, 126, 0, 128, COLOR_WHITE);

    // Dashed Lane Dividers
    for (int i = -20; i < 128; i += 20) {
        st7735_draw_fast_vline(dev, 64, i + s_road_offset, 10, COLOR_YELLOW);
        st7735_draw_fast_vline(dev, 96, i + s_road_offset, 10, COLOR_YELLOW);
    }

    // Score HUD Cards on shoulders
    st7735_fill_round_rect(dev, 2, 8, 30, 32, 4, COLOR_BLACK);
    st7735_draw_round_rect(dev, 2, 8, 30, 32, 4, COLOR_CYAN);
    st7735_draw_string(dev, 7, 12, "SC", COLOR_CYAN, COLOR_BLACK, 1);
    char sc_str[8];
    snprintf(sc_str, sizeof(sc_str), "%lu", s_car_score);
    st7735_draw_string(dev, 7, 26, sc_str, COLOR_WHITE, COLOR_BLACK, 1);

    st7735_fill_round_rect(dev, 128, 8, 30, 32, 4, COLOR_BLACK);
    st7735_draw_round_rect(dev, 128, 8, 30, 32, 4, COLOR_ORANGE);
    st7735_draw_string(dev, 133, 12, "HI", COLOR_ORANGE, COLOR_BLACK, 1);
    char hi_str[8];
    snprintf(hi_str, sizeof(hi_str), "%lu", s_car_high_score);
    st7735_draw_string(dev, 133, 26, hi_str, COLOR_WHITE, COLOR_BLACK, 1);

    // Draw Cars
    draw_car_shape(dev, (int)s_car_x, 96, COLOR_CYAN);
    draw_car_shape(dev, (int)s_enemy_x, (int)s_enemy_y, COLOR_RED);

    // Crash Dialog
    if (s_car_game_over) {
        st7735_fill_round_rect(dev, 22, 36, 116, 46, 6, COLOR_BLACK);
        st7735_draw_round_rect(dev, 22, 36, 116, 46, 6, COLOR_RED);
        st7735_fill_round_rect(dev, 22, 36, 116, 18, 6, COLOR_RED);
        st7735_draw_string(dev, 48, 40, "CRASHED!", COLOR_WHITE, COLOR_RED, 1);
        st7735_draw_string(dev, 34, 62, "Touch to Replay", COLOR_YELLOW, COLOR_BLACK, 1);
    }

    st7735_display(dev);
}

// ─── Mode 2: System Info & Telemetry Screen (matching COD2.ino) ──────────────
static void draw_telemetry_screen(st7735_t *dev) {
    st7735_clear(dev, COLOR_BLACK);

    // 1. Top Bar
    st7735_draw_string(dev, 38, 3, "SYSTEM INFO", COLOR_YELLOW, COLOR_BLACK, 1);
    st7735_draw_fast_hline(dev, 0, 14, 160, COLOR_CYAN);

    // WiFi Bars in top bar
    int wifi_pct = wifi_manager_get_rssi_percent();
    int bars = (wifi_pct > 75) ? 4 : (wifi_pct > 50) ? 3 : (wifi_pct > 25) ? 2 : (wifi_pct > 0) ? 1 : 0;
    for (int i = 0; i < 4; i++) {
        int h = (i + 1) * 2;
        if (i < bars) st7735_fill_rect(dev, 4 + i * 4, 12 - h, 2, h, COLOR_EMERALD);
        else st7735_draw_rect(dev, 4 + i * 4, 12 - h, 2, h, 0x4208);
    }
    // Battery in top bar
    st7735_draw_rect(dev, 138, 3, 16, 8, COLOR_WHITE);
    st7735_fill_rect(dev, 154, 5, 2, 4, COLOR_WHITE);
    st7735_fill_rect(dev, 140, 5, 12, 4, COLOR_EMERALD);

    // 2. Temperature Display Box
    st7735_fill_round_rect(dev, 8, 22, 116, 34, 5, 0x0821);
    st7735_draw_round_rect(dev, 8, 22, 116, 34, 5, COLOR_CYAN);
    st7735_draw_string(dev, 16, 26, "TEMP:", COLOR_CYAN, 0x0821, 1);

    float temp_c = 26.5f + (float)((esp_timer_get_time() / 1000000ULL) % 5) * 0.2f;
    char temp_str[16];
    snprintf(temp_str, sizeof(temp_str), "%.1f C", temp_c);
    st7735_draw_string(dev, 54, 26, temp_str, COLOR_WHITE, 0x0821, 2);

    // 3. WiFi Signal Gauge Bar
    st7735_draw_string(dev, 8, 64, "WIFI", COLOR_WHITE, COLOR_BLACK, 1);
    st7735_draw_round_rect(dev, 42, 63, 72, 10, 3, COLOR_WHITE);
    int w_fill = (wifi_pct * 68) / 100;
    if (w_fill > 0) st7735_fill_rect(dev, 44, 65, w_fill, 6, COLOR_EMERALD);
    char w_pct_str[16];
    snprintf(w_pct_str, sizeof(w_pct_str), "%d%%", wifi_pct);
    st7735_draw_string(dev, 118, 64, w_pct_str, COLOR_EMERALD, COLOR_BLACK, 1);

    // 4. Battery Level Bar
    st7735_draw_string(dev, 8, 80, "BATT", COLOR_WHITE, COLOR_BLACK, 1);
    st7735_draw_round_rect(dev, 42, 79, 72, 10, 3, COLOR_WHITE);
    st7735_fill_rect(dev, 44, 81, 68, 6, COLOR_ORANGE);
    st7735_draw_string(dev, 118, 80, "100%", COLOR_ORANGE, COLOR_BLACK, 1);

    // 5. Vertical Thermometer Graphic
    st7735_draw_round_rect(dev, 138, 22, 10, 50, 4, COLOR_WHITE);
    st7735_draw_circle(dev, 143, 80, 8, COLOR_WHITE);
    int therm_fill = (int)((temp_c / 50.0f) * 44.0f);
    if (therm_fill > 44) therm_fill = 44;
    st7735_fill_round_rect(dev, 140, 70 - therm_fill, 6, therm_fill, 2, COLOR_RED);
    st7735_fill_circle(dev, 143, 80, 6, COLOR_RED);

    // 6. Network Footer
    st7735_fill_round_rect(dev, 8, 98, 144, 24, 4, 0x18E3);
    st7735_draw_round_rect(dev, 8, 98, 144, 24, 4, COLOR_CYAN);
    char net_info[48];
    if (wifi_manager_is_sta_connected()) {
        snprintf(net_info, sizeof(net_info), "IP: %s", wifi_manager_get_sta_ip());
        st7735_draw_string(dev, 16, 105, net_info, COLOR_ORANGE, 0x18E3, 1);
    } else {
        snprintf(net_info, sizeof(net_info), "AP: %s", wifi_manager_get_ap_ip());
        st7735_draw_string(dev, 24, 105, net_info, COLOR_YELLOW, 0x18E3, 1);
    }

    st7735_display(dev);
}

// ─── Sensor, Touch & Alarm Processing Task (Core 0) ──────────────────────────
static void sensor_motion_task(void *pvParameters) {
    ESP_LOGI(TAG, "Sensor & Motion worker running on Core 0");

    bool was_touched = false;
    bool was_angry = false;
    uint64_t touch_start_time = 0;
    bool is_holding_touch = false;
    s_robot_state.last_activity_ms = esp_timer_get_time() / 1000ULL;

    while (1) {
        uint64_t now_ms = esp_timer_get_time() / 1000ULL;

        // 1. Process Alarm Countdown & Ringing Sequencer
        alarm_clock_update();

        // 2. Read Capacitive Touch Sensor (TTP223 on GPIO 4)
        int touch_val = gpio_get_level(PIN_TOUCH_SIG);
        bool touch_now = (touch_val == 1);
        s_robot_state.touch_just_pressed = (touch_now && !s_robot_state.touch_active);
        s_robot_state.touch_active = touch_now;

        // Inactivity activity detector
        if (touch_now || s_robot_state.motion_detected || alarm_clock_is_ringing()) {
            s_robot_state.last_activity_ms = now_ms;
            if (s_robot_state.is_sleeping) {
                s_robot_state.is_sleeping = false;
                st7735_set_backlight(&s_display, true);
                audio_play(SOUND_CLICK);
                ESP_LOGI(TAG, "Waking up from sleep mode!");
            }
        }

        // Inactivity Sleep check (2 minutes timeout matching COD2.ino)
        if (!s_robot_state.is_sleeping && !alarm_clock_is_ringing()) {
            if (now_ms - s_robot_state.last_activity_ms > INACTIVITY_TIMEOUT_MS) {
                s_robot_state.is_sleeping = true;
                st7735_set_backlight(&s_display, false);
                ESP_LOGI(TAG, "Entering energy-saving sleep mode after 120s inactivity");
            }
        }

        if (s_robot_state.touch_active) {
            // Dismiss alarm if ringing
            if (alarm_clock_is_ringing()) {
                alarm_clock_dismiss();
                vTaskDelay(pdMS_TO_TICKS(400));
                continue;
            }

            if (!is_holding_touch) {
                touch_start_time = now_ms;
                is_holding_touch = true;
            }

            // Long Press (> 1.2s): Cycle through all 5 modes (matching COD2.ino)
            if (now_ms - touch_start_time > 1200ULL) {
                is_holding_touch = false;
                audio_play(SOUND_MODE_SWITCH);
                haptics_pulse(80);

                // Advance mode: 0 -> 1 -> 2 -> 3 -> 4 -> 0
                s_robot_state.current_mode = (robot_mode_t)((s_robot_state.current_mode + 1) % 5);

                if (s_robot_state.current_mode == ROBOT_MODE_DINO) reset_dino_game();
                if (s_robot_state.current_mode == ROBOT_MODE_CAR)  reset_car_game();

                ESP_LOGI(TAG, "Switched to Robot Mode: %d", s_robot_state.current_mode);

                vTaskDelay(pdMS_TO_TICKS(500));
                continue;
            }

            // Short tap trigger in EYES mode
            if (!was_touched && s_robot_state.current_mode == ROBOT_MODE_EYES) {
                was_touched = true;
                audio_play(SOUND_HAPPY_CHIRP);
                haptics_heartbeat();
            }
        } else {
            is_holding_touch = false;
            was_touched = false;
        }

        // 3. Read BMI160 IMU Data
        bmi160_data_t imu_data;
        if (bmi160_read_data(&imu_data) == ESP_OK) {
            s_robot_state.tilt_ax = imu_data.ax;
            s_robot_state.tilt_ay = imu_data.ay;
            s_robot_state.tilt_az = imu_data.az;

            float diff = 0.0f;
            s_robot_state.motion_detected = bmi160_check_motion(0.22f, &diff);

            bmi160_motion_mood_t current_mood = bmi160_evaluate_mood(&imu_data);
            s_robot_state.motion_mood = current_mood;

            if (current_mood == BMI160_MOOD_ANGRY && s_robot_state.current_mode == ROBOT_MODE_EYES) {
                if (!was_angry) {
                    was_angry = true;
                    ESP_LOGW(TAG, "Angry mood triggered! ax=%.2f, az=%.2f", imu_data.ax, imu_data.az);
                    audio_play(SOUND_ANGRY);
                    haptics_pulse(150);
                }
            } else {
                was_angry = false;
            }

            // Gaze tracking based on gentle tilt
            if (current_mood == BMI160_MOOD_NEUTRAL) {
                if (imu_data.ax > 0.25f) {
                    s_robot_state.gaze_direction = E;
                } else if (imu_data.ax < -0.25f) {
                    s_robot_state.gaze_direction = W;
                } else if (imu_data.ay > 0.25f) {
                    s_robot_state.gaze_direction = S;
                } else if (imu_data.ay < -0.25f) {
                    s_robot_state.gaze_direction = N;
                } else {
                    s_robot_state.gaze_direction = DEFAULT;
                }
            }
        }

        vTaskDelay(pdMS_TO_TICKS(40)); // 25 Hz
    }
}

// ─── Display Rendering Task (Core 1) ─────────────────────────────────────────
static void robo_eyes_task(void *pvParameters) {
    ESP_LOGI(TAG, "Display Rendering worker running on Core 1");

    uint64_t last_anim_tick = esp_timer_get_time() / 1000ULL;
    uint64_t last_clock_draw = 0;
    uint64_t last_telemetry_draw = 0;

    while (1) {
        uint64_t now_ms = esp_timer_get_time() / 1000ULL;

        // If sleeping, skip render to save CPU
        if (s_robot_state.is_sleeping) {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        // 1. Alarm Ringing Screen (Highest Priority)
        if (alarm_clock_is_ringing()) {
            alarm_clock_draw_ringing_screen(&s_display);
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }

        // 2. Mode 1: Clock & Shamsi Alarm Mode
        if (s_robot_state.current_mode == ROBOT_MODE_CLOCK) {
            if (now_ms - last_clock_draw > 250ULL) {
                last_clock_draw = now_ms;
                alarm_clock_draw_screen(&s_display);
            }
            vTaskDelay(pdMS_TO_TICKS(30));
            continue;
        }

        // 3. Mode 2: System Info & Telemetry Screen
        if (s_robot_state.current_mode == ROBOT_MODE_TELEMETRY) {
            if (now_ms - last_telemetry_draw > 500ULL) {
                last_telemetry_draw = now_ms;
                draw_telemetry_screen(&s_display);
            }
            vTaskDelay(pdMS_TO_TICKS(40));
            continue;
        }

        // 4. Mode 3: Chrome Dino Jump Game
        if (s_robot_state.current_mode == ROBOT_MODE_DINO) {
            handle_dino_game(&s_display, now_ms, s_robot_state.touch_active, s_robot_state.touch_just_pressed);
            vTaskDelay(pdMS_TO_TICKS(25));
            continue;
        }

        // 5. Mode 4: Gyroscope Tilt Car Racing Game
        if (s_robot_state.current_mode == ROBOT_MODE_CAR) {
            handle_car_game(&s_display, now_ms, s_robot_state.touch_just_pressed, s_robot_state.tilt_ax, s_robot_state.tilt_ay);
            vTaskDelay(pdMS_TO_TICKS(25));
            continue;
        }

        // 6. Mode 0: RoboEyes Mode (50 FPS)
        if (s_robot_state.touch_active) {
            s_roboEyes.setMood(HAPPY);
            s_roboEyes.setPosition(N);
            s_roboEyes.setWidth(50, 50);
            s_roboEyes.setHeight(42, 42);
        }
        else if (s_robot_state.motion_mood == BMI160_MOOD_ANGRY) {
            s_roboEyes.setMood(ANGRY);
            s_roboEyes.setPosition(DEFAULT);
            s_roboEyes.setWidth(48, 48);
            s_roboEyes.setHeight(44, 44);
        }
        else if (s_robot_state.motion_mood == BMI160_MOOD_TIRED) {
            s_roboEyes.setMood(TIRED);
            s_roboEyes.setPosition(S);
            s_roboEyes.setWidth(46, 46);
            s_roboEyes.setHeight(32, 32);
        }
        else {
            s_roboEyes.setMood(DEFAULT);
            s_roboEyes.setPosition(s_robot_state.gaze_direction);
            s_roboEyes.setWidth(46, 46);
            s_roboEyes.setHeight(48, 48);

            // Autonomous random gaze/animation (every 8s)
            if (now_ms - last_anim_tick > 8000ULL) {
                last_anim_tick = now_ms;
                s_roboEyes.anim_laugh();
            }
        }

        s_roboEyes.update();
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

extern "C" void app_main(void) {
    ESP_LOGI(TAG, "=========================================================");
    ESP_LOGI(TAG, "   PHOENIX DESKTOP ROBOT - ESP-IDF PRODUCTION FIRMWARE   ");
    ESP_LOGI(TAG, "=========================================================");

    // 1. Initialize Touch Sensor (TTP223 on GPIO 4)
    gpio_config_t touch_io_conf = {
        .pin_bit_mask = (1ULL << PIN_TOUCH_SIG),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&touch_io_conf);
    ESP_LOGI(TAG, "Touch sensor initialized on GPIO %d", PIN_TOUCH_SIG);

    // 2. Initialize Haptics (GPIO 10)
    haptics_init(PIN_VIBRO_SIG);
    haptics_pulse(50);

    // 3. Initialize Audio Amplifier (GPIO 5, 6, 7)
    audio_config_t audio_cfg = {
        .bclk_pin = PIN_I2S_BCLK,
        .ws_pin   = PIN_I2S_WS,
        .dout_pin = PIN_I2S_DOUT,
        .sample_rate = 22050,
        .volume = 75
    };
    audio_manager_init(&audio_cfg);

    // 4. Initialize 1.8" SPI TFT Display (ST7735R 160x128 Landscape)
    st7735_config_t tft_cfg = {
        .spi_host = SPI2_HOST,
        .mosi_pin = PIN_TFT_MOSI,
        .sclk_pin = PIN_TFT_SCLK,
        .cs_pin = PIN_TFT_CS,
        .dc_pin = PIN_TFT_DC,
        .rst_pin = PIN_TFT_RST,
        .bl_pin = PIN_TFT_BL,
        .col_offset = 0,
        .row_offset = 0,
        .bgr_order = false
    };
    if (st7735_init(&s_display, &tft_cfg) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize ST7735R TFT!");
        return;
    }

    // 5. Check for Factory Reset on Boot (Holding Touch Sensor during power on)
    if (gpio_get_level(PIN_TOUCH_SIG) == 1) {
        st7735_clear(&s_display, COLOR_BLACK);
        st7735_draw_round_rect(&s_display, 8, 20, 144, 88, 6, COLOR_RED);
        st7735_draw_string(&s_display, 24, 38, "FACTORY RESET", COLOR_RED, COLOR_BLACK, 2);
        st7735_draw_string(&s_display, 16, 68, "Clearing WiFi NVS...", COLOR_WHITE, COLOR_BLACK, 1);
        st7735_display(&s_display);

        audio_play_tone(400, 500);
        wifi_manager_clear_credentials();
        vTaskDelay(pdMS_TO_TICKS(2000));
        esp_restart();
    }

    // 6. Initialize BMI160 IMU (GPIO 8 SDA, GPIO 9 SCL)
    bmi160_config_t bmi_cfg = {
        .i2c_port = I2C_NUM_0,
        .sda_pin  = PIN_I2C_SDA,
        .scl_pin  = PIN_I2C_SCL,
        .clk_speed_hz = 400000,
        .i2c_addr = 0
    };
    bmi160_init(&bmi_cfg);

    // 7. Initialize WiFi Manager & Web Server
    wifi_manager_config_t wifi_cfg = {
        .ap_ssid = "PHOENIX-SETUP",
        .ap_password = "gamephoenix",
        .sta_ssid = NULL,
        .sta_password = NULL
    };
    wifi_manager_init(&wifi_cfg);
    alarm_clock_init();

    // 8. Check if Robot Needs Onboarding Setup (First Ever Run or Saved WiFi Unreachable)
    bool needs_onboarding = !wifi_manager_has_saved_credentials();

    if (!needs_onboarding) {
        // Robot has saved credentials: Show connecting screen and attempt connection
        char saved_ssid[33] = {0};
        wifi_manager_get_saved_ssid(saved_ssid, sizeof(saved_ssid));

        st7735_clear(&s_display, COLOR_BLACK);
        st7735_draw_string(&s_display, 38, 6, "CONNECTING", COLOR_YELLOW, COLOR_BLACK, 1);
        st7735_draw_fast_hline(&s_display, 0, 16, 160, COLOR_CYAN);
        st7735_draw_round_rect(&s_display, 8, 24, 144, 88, 5, COLOR_WHITE);
        st7735_draw_string(&s_display, 16, 34, "Connecting to Wi-Fi:", COLOR_CYAN, COLOR_BLACK, 1);
        st7735_draw_string(&s_display, 16, 52, saved_ssid, COLOR_WHITE, COLOR_BLACK, 1);
        st7735_draw_string(&s_display, 16, 78, "Please wait...", 0x8410, COLOR_BLACK, 1);
        st7735_display(&s_display);

        uint64_t wait_start = esp_timer_get_time() / 1000ULL;
        while ((esp_timer_get_time() / 1000ULL) - wait_start < 8000ULL) {
            if (wifi_manager_is_sta_connected()) {
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(150));
        }

        // If after 8 seconds still not connected, start Onboarding AP
        if (!wifi_manager_is_sta_connected()) {
            ESP_LOGW(TAG, "Saved router '%s' not reachable. Starting setup AP...", saved_ssid);
            wifi_manager_start_setup_ap("PHOENIX-SETUP", "gamephoenix");
            needs_onboarding = true;
        }
    }

    // 9. MANDATORY BLOCKING ONBOARDING SETUP GUI
    if (needs_onboarding) {
        ESP_LOGI(TAG, "Entering Onboarding Setup GUI. Halting normal operations until customer connects.");

        uint64_t anim_tick = 0;
        int dot_count = 0;

        while (!wifi_manager_is_sta_connected()) {
            uint64_t now_ms = esp_timer_get_time() / 1000ULL;
            wifi_setup_status_t status = wifi_manager_get_setup_status();

            if (status == WIFI_SETUP_CONNECTED) {
                break;
            }

            if (status == WIFI_SETUP_FAILED) {
                st7735_clear(&s_display, COLOR_BLACK);
                st7735_draw_round_rect(&s_display, 6, 14, 148, 100, 6, COLOR_RED);
                st7735_fill_round_rect(&s_display, 6, 14, 148, 20, 6, COLOR_RED);
                st7735_draw_string(&s_display, 24, 18, "CONNECTION FAILED", COLOR_WHITE, COLOR_RED, 1);
                st7735_draw_string(&s_display, 14, 46, "Wrong password or", COLOR_WHITE, COLOR_BLACK, 1);
                st7735_draw_string(&s_display, 14, 60, "router unreachable.", COLOR_WHITE, COLOR_BLACK, 1);
                st7735_draw_string(&s_display, 14, 82, "Check phone portal...", COLOR_YELLOW, COLOR_BLACK, 1);
                st7735_display(&s_display);

                audio_play(SOUND_ANGRY);
                vTaskDelay(pdMS_TO_TICKS(3000));
                wifi_manager_reset_setup_status();
                continue;
            }

            if (status == WIFI_SETUP_CONNECTING) {
                st7735_clear(&s_display, COLOR_BLACK);
                st7735_draw_string(&s_display, 38, 6, "CONNECTING", COLOR_YELLOW, COLOR_BLACK, 1);
                st7735_draw_fast_hline(&s_display, 0, 16, 160, COLOR_CYAN);
                st7735_draw_round_rect(&s_display, 8, 24, 144, 88, 5, COLOR_CYAN);
                st7735_draw_string(&s_display, 16, 34, "Connecting to router:", COLOR_CYAN, COLOR_BLACK, 1);
                st7735_draw_string(&s_display, 16, 52, wifi_manager_get_connecting_ssid(), COLOR_WHITE, COLOR_BLACK, 1);
                st7735_draw_string(&s_display, 16, 74, "Checking password...", COLOR_YELLOW, COLOR_BLACK, 1);

                int pb = ((now_ms / 150ULL) % 10) * 12;
                st7735_fill_rect(&s_display, 16, 90, pb, 6, COLOR_EMERALD);
                st7735_display(&s_display);

                vTaskDelay(pdMS_TO_TICKS(150));
                continue;
            }

            // Status == WIFI_SETUP_WAITING: Display Setup Card with SSID and Password
            if (now_ms - anim_tick > 350ULL) {
                anim_tick = now_ms;
                dot_count = (dot_count + 1) % 4;

                st7735_clear(&s_display, COLOR_BLACK);
                st7735_draw_string(&s_display, 42, 4, "WIFI SETUP", COLOR_YELLOW, COLOR_BLACK, 1);
                st7735_draw_fast_hline(&s_display, 0, 14, 160, COLOR_CYAN);

                // Setup Card Box
                st7735_draw_round_rect(&s_display, 6, 18, 148, 92, 5, COLOR_WHITE);
                st7735_fill_round_rect(&s_display, 8, 20, 144, 16, 3, 0x18E3);
                st7735_draw_string(&s_display, 14, 24, "PHOENIX SMART ROBOT", COLOR_CYAN, 0x18E3, 1);

                st7735_draw_string(&s_display, 12, 42, "SSID: PHOENIX-SETUP", COLOR_WHITE, COLOR_BLACK, 1);
                st7735_draw_string(&s_display, 12, 56, "PWD : gamephoenix", COLOR_YELLOW, COLOR_BLACK, 1);
                st7735_draw_string(&s_display, 12, 70, "IP  : 192.168.4.1", COLOR_EMERALD, COLOR_BLACK, 1);

                char wait_msg[32] = "Waiting for setup";
                for (int d = 0; d < dot_count; d++) strcat(wait_msg, ".");
                st7735_draw_string(&s_display, 12, 88, wait_msg, 0x8410, COLOR_BLACK, 1);

                st7735_fill_circle(&s_display, 142, 92, (dot_count % 2 == 0) ? 3 : 2, COLOR_EMERALD);
                st7735_draw_string(&s_display, 8, 114, "Connect phone to Wi-Fi", COLOR_CYAN, COLOR_BLACK, 1);
                st7735_display(&s_display);
            }

            vTaskDelay(pdMS_TO_TICKS(100));
        }

        // 10. SETUP SUCCESS SCREEN
        st7735_clear(&s_display, COLOR_BLACK);
        st7735_draw_round_rect(&s_display, 6, 14, 148, 100, 6, COLOR_EMERALD);
        st7735_fill_round_rect(&s_display, 6, 14, 148, 20, 6, COLOR_EMERALD);
        st7735_draw_string(&s_display, 28, 18, "SETUP COMPLETE!", COLOR_BLACK, COLOR_EMERALD, 1);
        st7735_draw_string(&s_display, 14, 42, "Connected to:", COLOR_WHITE, COLOR_BLACK, 1);
        st7735_draw_string(&s_display, 14, 56, wifi_manager_get_connecting_ssid(), COLOR_YELLOW, COLOR_BLACK, 1);

        char ip_str[32];
        snprintf(ip_str, sizeof(ip_str), "IP: %s", wifi_manager_get_sta_ip());
        st7735_draw_string(&s_display, 14, 74, ip_str, COLOR_CYAN, COLOR_BLACK, 1);
        st7735_draw_string(&s_display, 14, 94, "Disabling setup AP...", 0x8410, COLOR_BLACK, 1);
        st7735_display(&s_display);

        audio_play(SOUND_SCORE);
        haptics_heartbeat();

        vTaskDelay(pdMS_TO_TICKS(2500));

        // 11. PERMANENTLY TURN OFF THE SETUP AP
        wifi_manager_stop_ap();
        ESP_LOGI(TAG, "SoftAP permanently disabled. Robot running purely in STA mode on home router.");
    }

    // 12. Animated Cyberpunk Boot Splash (matching COD2.ino)
    st7735_clear(&s_display, COLOR_BLACK);
    st7735_draw_round_rect(&s_display, 4, 4, 152, 120, 6, COLOR_CYAN);
    st7735_fill_round_rect(&s_display, 18, 16, 124, 22, 4, COLOR_CYAN);
    st7735_draw_string(&s_display, 28, 22, "GAME PHOENIX", COLOR_BLACK, COLOR_CYAN, 1);
    st7735_draw_string(&s_display, 36, 44, "DESKTOP BOT", COLOR_WHITE, COLOR_BLACK, 1);
    st7735_draw_round_rect(&s_display, 18, 72, 124, 14, 3, COLOR_WHITE);
    st7735_display(&s_display);

    // Boot progress bar
    for (int i = 0; i <= 120; i += 12) {
        st7735_fill_rect(&s_display, 20, 74, i, 10, COLOR_EMERALD);
        st7735_display(&s_display);
        vTaskDelay(pdMS_TO_TICKS(35));
    }
    audio_play(SOUND_BOOT);

    // 13. Initialize RoboEyes Engine
    s_roboEyes.begin(&s_display, 160, 128, 50);
    s_roboEyes.setAutoMoodColors(true);
    s_roboEyes.setAutoblinker(true, 2, 4);
    s_roboEyes.setWidth(46, 46);
    s_roboEyes.setHeight(48, 48);
    s_roboEyes.blink(true, true);

    // 14. Launch Dual-Core Tasks (Core 0: Sensors & State, Core 1: Display Rendering)
    xTaskCreatePinnedToCore(sensor_motion_task, "sensor_task", 4096, NULL, 4, NULL, 0);
    xTaskCreatePinnedToCore(robo_eyes_task, "robo_eyes_task", 4096, NULL, 5, NULL, 1);

    ESP_LOGI(TAG, "All systems operational! 5 Robot Modes active. Hold touch 1.2s to cycle modes.");
}
