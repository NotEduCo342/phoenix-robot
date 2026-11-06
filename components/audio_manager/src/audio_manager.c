#include "audio_manager.h"
#include <string.h>
#include <math.h>
#include <stdlib.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/i2s_std.h"

static const char *TAG = "audio_mgr";

#define AUDIO_CHUNK_FRAMES  256

typedef enum {
    AUDIO_CMD_PLAY_PRESET,
    AUDIO_CMD_PLAY_TONE,
    AUDIO_CMD_PLAY_SWEEP,
    AUDIO_CMD_STOP,
} audio_cmd_type_t;

typedef struct {
    audio_cmd_type_t type;
    robot_sound_t    preset;
    uint16_t         f1;
    uint16_t         f2;
    uint16_t         duration_ms;
} audio_cmd_t;

static i2s_chan_handle_t    s_tx_chan = NULL;
static QueueHandle_t        s_audio_queue = NULL;
static TaskHandle_t         s_audio_task_handle = NULL;
static uint32_t             s_sample_rate = 22050;
static uint8_t              s_volume = 75;
static volatile bool        s_is_playing = false;
static volatile bool        s_stop_requested = false;

static void synth_silence(uint32_t duration_ms) {
    if (duration_ms == 0) return;
    uint32_t total_frames = (duration_ms * s_sample_rate) / 1000;
    int16_t buffer[AUDIO_CHUNK_FRAMES * 2] = {0}; // stereo
    size_t bytes_written = 0;

    while (total_frames > 0 && !s_stop_requested) {
        uint32_t chunk = (total_frames > AUDIO_CHUNK_FRAMES) ? AUDIO_CHUNK_FRAMES : total_frames;
        i2s_channel_write(s_tx_chan, buffer, chunk * 2 * sizeof(int16_t), &bytes_written, pdMS_TO_TICKS(100));
        total_frames -= chunk;
    }
}

static void synth_tone(uint16_t freq_hz, uint32_t duration_ms) {
    if (freq_hz == 0 || duration_ms == 0 || s_volume == 0) {
        synth_silence(duration_ms);
        return;
    }

    uint32_t total_frames = (duration_ms * s_sample_rate) / 1000;
    if (total_frames == 0) total_frames = 1;

    // Smooth envelope attack and decay to prevent clicks (approx 4ms)
    uint32_t fade_frames = s_sample_rate / 250;
    if (total_frames < fade_frames * 2) {
        fade_frames = total_frames / 4;
    }

    float max_amp = (float)((32767 * s_volume) / 100);
    float phase = 0.0f;
    float phase_inc = (2.0f * (float)M_PI * (float)freq_hz) / (float)s_sample_rate;

    int16_t buffer[AUDIO_CHUNK_FRAMES * 2];
    uint32_t frames_rendered = 0;
    size_t bytes_written = 0;

    while (frames_rendered < total_frames && !s_stop_requested) {
        uint32_t chunk = (total_frames - frames_rendered > AUDIO_CHUNK_FRAMES) ? 
                         AUDIO_CHUNK_FRAMES : (total_frames - frames_rendered);

        for (uint32_t i = 0; i < chunk; i++) {
            uint32_t current_frame = frames_rendered + i;
            float env = 1.0f;
            if (fade_frames > 0) {
                if (current_frame < fade_frames) {
                    env = (float)current_frame / (float)fade_frames;
                } else if (current_frame > total_frames - fade_frames) {
                    env = (float)(total_frames - current_frame) / (float)fade_frames;
                }
            }

            int16_t sample = (int16_t)(sinf(phase) * env * max_amp);
            phase += phase_inc;
            if (phase >= 2.0f * (float)M_PI) phase -= 2.0f * (float)M_PI;

            // Stereo: Left = Right
            buffer[i * 2]     = sample;
            buffer[i * 2 + 1] = sample;
        }

        i2s_channel_write(s_tx_chan, buffer, chunk * 2 * sizeof(int16_t), &bytes_written, pdMS_TO_TICKS(100));
        frames_rendered += chunk;
    }
}

static void synth_sweep(uint16_t start_hz, uint16_t end_hz, uint32_t duration_ms) {
    if (duration_ms == 0 || s_volume == 0) return;

    uint32_t total_frames = (duration_ms * s_sample_rate) / 1000;
    if (total_frames == 0) total_frames = 1;

    uint32_t fade_frames = s_sample_rate / 250;
    if (total_frames < fade_frames * 2) {
        fade_frames = total_frames / 4;
    }

    float max_amp = (float)((32767 * s_volume) / 100);
    float phase = 0.0f;

    int16_t buffer[AUDIO_CHUNK_FRAMES * 2];
    uint32_t frames_rendered = 0;
    size_t bytes_written = 0;

    while (frames_rendered < total_frames && !s_stop_requested) {
        uint32_t chunk = (total_frames - frames_rendered > AUDIO_CHUNK_FRAMES) ? 
                         AUDIO_CHUNK_FRAMES : (total_frames - frames_rendered);

        for (uint32_t i = 0; i < chunk; i++) {
            uint32_t current_frame = frames_rendered + i;
            float progress = (float)current_frame / (float)total_frames;
            float freq = (float)start_hz + ((float)end_hz - (float)start_hz) * progress;
            float phase_inc = (2.0f * (float)M_PI * freq) / (float)s_sample_rate;

            float env = 1.0f;
            if (fade_frames > 0) {
                if (current_frame < fade_frames) {
                    env = (float)current_frame / (float)fade_frames;
                } else if (current_frame > total_frames - fade_frames) {
                    env = (float)(total_frames - current_frame) / (float)fade_frames;
                }
            }

            int16_t sample = (int16_t)(sinf(phase) * env * max_amp);
            phase += phase_inc;
            if (phase >= 2.0f * (float)M_PI) phase -= 2.0f * (float)M_PI;

            buffer[i * 2]     = sample;
            buffer[i * 2 + 1] = sample;
        }

        i2s_channel_write(s_tx_chan, buffer, chunk * 2 * sizeof(int16_t), &bytes_written, pdMS_TO_TICKS(100));
        frames_rendered += chunk;
    }
}

static void play_preset_sound(robot_sound_t preset) {
    switch (preset) {
        case SOUND_BOOT:
            // C5, E5, G5, C6 arpeggio
            synth_tone(523, 80);
            synth_silence(10);
            synth_tone(659, 80);
            synth_silence(10);
            synth_tone(784, 80);
            synth_silence(10);
            synth_tone(1046, 160);
            synth_silence(20);
            break;

        case SOUND_HAPPY_CHIRP:
            // Dual frequency sweep upwards and downwards
            synth_sweep(800, 2200, 85);
            synth_sweep(2200, 1200, 65);
            synth_silence(20);
            break;

        case SOUND_ANGRY:
            // Low growl descending
            synth_tone(220, 90);
            synth_silence(15);
            synth_tone(160, 140);
            synth_silence(20);
            break;

        case SOUND_MODE_SWITCH:
            // Fast 2-step beep
            synth_tone(1200, 50);
            synth_silence(10);
            synth_tone(1800, 70);
            synth_silence(20);
            break;

        case SOUND_JUMP:
            synth_sweep(700, 1100, 50);
            synth_silence(10);
            break;

        case SOUND_SCORE:
            synth_tone(1600, 45);
            synth_tone(2100, 55);
            synth_silence(10);
            break;

        case SOUND_GAME_OVER:
            synth_tone(500, 110);
            synth_silence(15);
            synth_tone(380, 110);
            synth_silence(15);
            synth_tone(260, 260);
            synth_silence(25);
            break;

        case SOUND_CRASH:
            // Noise burst simulation
            for (int i = 0; i < 8 && !s_stop_requested; i++) {
                uint16_t rf = 130 + (rand() % 320);
                synth_tone(rf, 22);
            }
            synth_silence(20);
            break;

        case SOUND_ALARM_BEEP:
            synth_tone(880, 70);
            synth_silence(15);
            synth_tone(1320, 120);
            synth_silence(20);
            break;

        case SOUND_HEARTBEAT:
            synth_tone(65, 80);
            synth_silence(50);
            synth_tone(55, 100);
            synth_silence(30);
            break;

        case SOUND_CLICK:
            synth_tone(2400, 10);
            synth_silence(5);
            break;

        default:
            break;
    }
}

static void audio_task(void *pvParameters) {
    audio_cmd_t cmd;

    while (1) {
        if (xQueueReceive(s_audio_queue, &cmd, portMAX_DELAY) == pdTRUE) {
            s_is_playing = true;
            s_stop_requested = false;

            switch (cmd.type) {
                case AUDIO_CMD_PLAY_PRESET:
                    play_preset_sound(cmd.preset);
                    break;

                case AUDIO_CMD_PLAY_TONE:
                    synth_tone(cmd.f1, cmd.duration_ms);
                    synth_silence(10);
                    break;

                case AUDIO_CMD_PLAY_SWEEP:
                    synth_sweep(cmd.f1, cmd.f2, cmd.duration_ms);
                    synth_silence(10);
                    break;

                case AUDIO_CMD_STOP:
                    break;
            }

            s_is_playing = false;
        }
    }
}

esp_err_t audio_manager_init(const audio_config_t *cfg) {
    if (!cfg) return ESP_ERR_INVALID_ARG;
    if (cfg->sample_rate) s_sample_rate = cfg->sample_rate;
    if (cfg->volume <= 100) s_volume = cfg->volume;

    // 1. Configure I2S channel
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_1, I2S_ROLE_MASTER);
    chan_cfg.dma_desc_num = 6;
    chan_cfg.dma_frame_num = 240;
    chan_cfg.auto_clear_after_cb = true;

    esp_err_t ret = i2s_new_channel(&chan_cfg, &s_tx_chan, NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create I2S TX channel: %s", esp_err_to_name(ret));
        return ret;
    }

    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(s_sample_rate),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = cfg->bclk_pin,
            .ws   = cfg->ws_pin,
            .dout = cfg->dout_pin,
            .din  = I2S_GPIO_UNUSED,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv   = false,
            },
        },
    };

    ret = i2s_channel_init_std_mode(s_tx_chan, &std_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init I2S STD mode: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = i2s_channel_enable(s_tx_chan);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to enable I2S channel: %s", esp_err_to_name(ret));
        return ret;
    }

    // 2. Create command queue
    if (!s_audio_queue) {
        s_audio_queue = xQueueCreate(10, sizeof(audio_cmd_t));
    }

    // 3. Launch background audio task pinned to Core 0 (leaving Core 1 for screen rendering)
    if (!s_audio_task_handle) {
        xTaskCreatePinnedToCore(
            audio_task,
            "audio_task",
            4096,
            NULL,
            5,
            &s_audio_task_handle,
            0 // Core 0
        );
    }

    ESP_LOGI(TAG, "MAX98357A I2S audio initialized at %lu Hz (Vol: %d%%) on Core 0",
             s_sample_rate, s_volume);
    return ESP_OK;
}

esp_err_t audio_play(robot_sound_t sound) {
    if (!s_audio_queue) return ESP_ERR_INVALID_STATE;
    audio_cmd_t cmd = {
        .type = AUDIO_CMD_PLAY_PRESET,
        .preset = sound
    };
    return (xQueueSend(s_audio_queue, &cmd, 0) == pdTRUE) ? ESP_OK : ESP_FAIL;
}

esp_err_t audio_play_tone(uint16_t freq_hz, uint16_t duration_ms) {
    if (!s_audio_queue) return ESP_ERR_INVALID_STATE;
    audio_cmd_t cmd = {
        .type = AUDIO_CMD_PLAY_TONE,
        .f1 = freq_hz,
        .duration_ms = duration_ms
    };
    return (xQueueSend(s_audio_queue, &cmd, 0) == pdTRUE) ? ESP_OK : ESP_FAIL;
}

esp_err_t audio_play_sweep(uint16_t start_freq_hz, uint16_t end_freq_hz, uint16_t duration_ms) {
    if (!s_audio_queue) return ESP_ERR_INVALID_STATE;
    audio_cmd_t cmd = {
        .type = AUDIO_CMD_PLAY_SWEEP,
        .f1 = start_freq_hz,
        .f2 = end_freq_hz,
        .duration_ms = duration_ms
    };
    return (xQueueSend(s_audio_queue, &cmd, 0) == pdTRUE) ? ESP_OK : ESP_FAIL;
}

void audio_set_volume(uint8_t volume) {
    if (volume > 100) volume = 100;
    s_volume = volume;
}

uint8_t audio_get_volume(void) {
    return s_volume;
}

void audio_stop(void) {
    s_stop_requested = true;
    if (s_audio_queue) {
        xQueueReset(s_audio_queue);
    }
}

bool audio_is_playing(void) {
    return s_is_playing;
}

