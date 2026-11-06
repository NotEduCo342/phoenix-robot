#include "haptics.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

static const char *TAG = "haptics";

typedef enum {
    HAPTIC_CMD_PULSE,
    HAPTIC_CMD_HEARTBEAT,
    HAPTIC_CMD_SET_ACTIVE,
    HAPTIC_CMD_STOP,
} haptic_cmd_t;

typedef struct {
    haptic_cmd_t type;
    uint32_t     param;
} haptic_msg_t;

static gpio_num_t    s_vibro_pin = GPIO_NUM_NC;
static QueueHandle_t s_haptic_queue = NULL;
static TaskHandle_t  s_haptic_task_handle = NULL;

static void haptic_task(void *pvParameters) {
    haptic_msg_t msg;

    while (1) {
        if (xQueueReceive(s_haptic_queue, &msg, portMAX_DELAY) == pdTRUE) {
            switch (msg.type) {
                case HAPTIC_CMD_PULSE:
                    if (s_vibro_pin != GPIO_NUM_NC) {
                        gpio_set_level(s_vibro_pin, 1);
                        vTaskDelay(pdMS_TO_TICKS(msg.param));
                        gpio_set_level(s_vibro_pin, 0);
                    }
                    break;

                case HAPTIC_CMD_HEARTBEAT:
                    if (s_vibro_pin != GPIO_NUM_NC) {
                        // Lub (100ms)
                        gpio_set_level(s_vibro_pin, 1);
                        vTaskDelay(pdMS_TO_TICKS(100));
                        gpio_set_level(s_vibro_pin, 0);
                        vTaskDelay(pdMS_TO_TICKS(110));
                        // Dub (120ms)
                        gpio_set_level(s_vibro_pin, 1);
                        vTaskDelay(pdMS_TO_TICKS(120));
                        gpio_set_level(s_vibro_pin, 0);
                    }
                    break;

                case HAPTIC_CMD_SET_ACTIVE:
                    if (s_vibro_pin != GPIO_NUM_NC) {
                        gpio_set_level(s_vibro_pin, msg.param ? 1 : 0);
                    }
                    break;

                case HAPTIC_CMD_STOP:
                    if (s_vibro_pin != GPIO_NUM_NC) {
                        gpio_set_level(s_vibro_pin, 0);
                    }
                    break;
            }
        }
    }
}

esp_err_t haptics_init(gpio_num_t vibro_pin) {
    s_vibro_pin = vibro_pin;

    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << vibro_pin),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    esp_err_t ret = gpio_config(&io_conf);
    if (ret != ESP_OK) return ret;

    gpio_set_level(vibro_pin, 0);

    if (!s_haptic_queue) {
        s_haptic_queue = xQueueCreate(6, sizeof(haptic_msg_t));
    }

    if (!s_haptic_task_handle) {
        xTaskCreatePinnedToCore(
            haptic_task,
            "haptic_task",
            2048,
            NULL,
            4,
            &s_haptic_task_handle,
            0 // Core 0
        );
    }

    ESP_LOGI(TAG, "Haptics motor initialized on GPIO %d", vibro_pin);
    return ESP_OK;
}

void haptics_pulse(uint32_t duration_ms) {
    if (!s_haptic_queue) return;
    haptic_msg_t msg = {
        .type = HAPTIC_CMD_PULSE,
        .param = duration_ms
    };
    xQueueSend(s_haptic_queue, &msg, 0);
}

void haptics_heartbeat(void) {
    if (!s_haptic_queue) return;
    haptic_msg_t msg = {
        .type = HAPTIC_CMD_HEARTBEAT,
        .param = 0
    };
    xQueueSend(s_haptic_queue, &msg, 0);
}

void haptics_set_active(bool enable) {
    if (!s_haptic_queue) return;
    haptic_msg_t msg = {
        .type = HAPTIC_CMD_SET_ACTIVE,
        .param = enable ? 1 : 0
    };
    xQueueSend(s_haptic_queue, &msg, 0);
}

void haptics_stop(void) {
    if (!s_haptic_queue) return;
    xQueueReset(s_haptic_queue);
    haptic_msg_t msg = {
        .type = HAPTIC_CMD_STOP,
        .param = 0
    };
    xQueueSend(s_haptic_queue, &msg, 0);
}

