#include "encoder.h"

#include <string.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
#include "esp_check.h"

static const char *TAG = "encoder";

#define ENCODER_SCAN_PERIOD_US      1000    // 1ms 扫描一次
#define KEY_DEBOUNCE_MS             20      // 按键消抖 20ms

static esp_timer_handle_t s_scan_timer = NULL;
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;

static uint8_t s_last_quad_state = 0;

static int32_t s_raw_count = 0;
static int32_t s_pending_delta = 0;
static encoder_direction_t s_pending_dir = ENCODER_DIR_STILLNESS;
static encoder_key_event_t s_pending_key = ENCODER_KEY_NONE;

static uint8_t s_key_last_raw = 1;
static uint8_t s_key_stable = 1;
static uint8_t s_key_same_count = 0;

/*
 * 正交编码器状态表。
 * state = (A << 1) | B
 * index = (last_state << 2) | now_state
 *
 * 如果你的 EC11 顺/逆方向显示反了：
 * 1. 交换 A/B 接线；或
 * 2. 把表中的 +1 和 -1 对调；或
 * 3. 在 scan 回调里对 delta 取反。
 */
static const int8_t s_quad_table[16] = {
     0, -1, +1,  0,
    +1,  0,  0, -1,
    -1,  0,  0, +1,
     0, +1, -1,  0
};

static inline uint8_t read_quad_state(void)
{
    uint8_t a = gpio_get_level(ENCODER_A_GPIO) ? 1 : 0;
    uint8_t b = gpio_get_level(ENCODER_B_GPIO) ? 1 : 0;
    return (uint8_t)((a << 1) | b);
}

static void encoder_scan_callback(void *arg)
{
    (void)arg;

    uint8_t now_state = read_quad_state();

    if (now_state != s_last_quad_state) {
        uint8_t index = (uint8_t)((s_last_quad_state << 2) | now_state);
        int8_t delta = s_quad_table[index];

        /*
         * 对机械抖动产生的非法跳变，delta 会等于 0。
         * 合法跳变才计数。
         */
        if (delta != 0) {
            portENTER_CRITICAL(&s_lock);
            s_raw_count += delta;
            s_pending_delta += delta;
            s_pending_dir = (delta > 0) ? ENCODER_DIR_CW : ENCODER_DIR_CCW;
            portEXIT_CRITICAL(&s_lock);
        }

        s_last_quad_state = now_state;
    }

    /*
     * 按键 SW 默认上拉，按下为 0，松开为 1。
     * 这里做 20ms 稳定判定。
     */
    uint8_t key_raw = gpio_get_level(ENCODER_SW_GPIO) ? 1 : 0;

    if (key_raw == s_key_last_raw) {
        if (s_key_same_count < KEY_DEBOUNCE_MS) {
            s_key_same_count++;
        }

        if (s_key_same_count >= KEY_DEBOUNCE_MS && key_raw != s_key_stable) {
            s_key_stable = key_raw;

            portENTER_CRITICAL(&s_lock);
            s_pending_key = (s_key_stable == 0) ? ENCODER_KEY_PRESS : ENCODER_KEY_RELEASE;
            portEXIT_CRITICAL(&s_lock);
        }
    } else {
        s_key_last_raw = key_raw;
        s_key_same_count = 0;
    }
}

esp_err_t encoder_init(void)
{
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << ENCODER_A_GPIO) |
                        (1ULL << ENCODER_B_GPIO) |
                        (1ULL << ENCODER_SW_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    ESP_RETURN_ON_ERROR(gpio_config(&io_conf), TAG, "gpio_config failed");

    s_last_quad_state = read_quad_state();
    s_key_last_raw = gpio_get_level(ENCODER_SW_GPIO) ? 1 : 0;
    s_key_stable = s_key_last_raw;
    s_key_same_count = 0;

    const esp_timer_create_args_t timer_args = {
        .callback = encoder_scan_callback,
        .arg = NULL,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "encoder_scan",
        .skip_unhandled_events = true,
    };

    ESP_RETURN_ON_ERROR(esp_timer_create(&timer_args, &s_scan_timer), TAG, "esp_timer_create failed");
    ESP_RETURN_ON_ERROR(esp_timer_start_periodic(s_scan_timer, ENCODER_SCAN_PERIOD_US), TAG, "esp_timer_start_periodic failed");

    ESP_LOGI(TAG, "EC11 init done: A=GPIO%d, B=GPIO%d, SW=GPIO%d",
             ENCODER_A_GPIO, ENCODER_B_GPIO, ENCODER_SW_GPIO);

    return ESP_OK;
}

bool encoder_get_event(encoder_event_t *event)
{
    if (event == NULL) {
        return false;
    }

    memset(event, 0, sizeof(*event));

    portENTER_CRITICAL(&s_lock);
    int32_t pending_delta = s_pending_delta;
    int32_t raw_count = s_raw_count;
    encoder_direction_t dir = s_pending_dir;
    encoder_key_event_t key = s_pending_key;

    s_pending_delta = 0;
    s_pending_dir = ENCODER_DIR_STILLNESS;
    s_pending_key = ENCODER_KEY_NONE;
    portEXIT_CRITICAL(&s_lock);

    event->raw_delta = pending_delta;
    event->raw_count = raw_count;
    event->detent_count = raw_count / ENCODER_RAW_COUNTS_PER_DETENT;
    event->revolutions = (float)raw_count / (float)ENCODER_RAW_COUNTS_PER_REV;
    event->direction = (pending_delta != 0) ? dir : ENCODER_DIR_STILLNESS;
    event->key_event = key;

    return (pending_delta != 0) || (key != ENCODER_KEY_NONE);
}

const char *encoder_dir_to_str(encoder_direction_t dir)
{
    switch (dir) {
    case ENCODER_DIR_CW:
        return "CW / 正转";
    case ENCODER_DIR_CCW:
        return "CCW / 反转";
    case ENCODER_DIR_STILLNESS:
    default:
        return "Still / 静止";
    }
}

const char *encoder_key_to_str(encoder_key_event_t key)
{
    switch (key) {
    case ENCODER_KEY_PRESS:
        return "PRESS / 按下";
    case ENCODER_KEY_RELEASE:
        return "RELEASE / 松开";
    case ENCODER_KEY_NONE:
    default:
        return "NONE";
    }
}
