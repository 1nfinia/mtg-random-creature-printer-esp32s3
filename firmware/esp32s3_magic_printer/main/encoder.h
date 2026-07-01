#ifndef ENCODER_H
#define ENCODER_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "driver/gpio.h"

/*
 * 默认接线：
 * EC11 A  -> GPIO6
 * EC11 B  -> GPIO7
 * EC11 SW -> GPIO15
 * EC11 C/GND -> GND
 * EC11 VCC   -> 3V3
 *
 * 若旋转方向与预期相反，可以交换 A/B 接线，
 * 或者在 encoder.c 中把 delta 的正负取反。
 */
#define ENCODER_A_GPIO              GPIO_NUM_6
#define ENCODER_B_GPIO              GPIO_NUM_7
#define ENCODER_SW_GPIO             GPIO_NUM_15

/*
 * 常见 EC11：360° 为 20 个周期。
 * 四倍频解码后约为 80 个 raw count / revolution。
 */
#define ENCODER_RAW_COUNTS_PER_REV  80
#define ENCODER_RAW_COUNTS_PER_DETENT 4

typedef enum {
    ENCODER_DIR_STILLNESS = 0,
    ENCODER_DIR_CW,
    ENCODER_DIR_CCW,
} encoder_direction_t;

typedef enum {
    ENCODER_KEY_NONE = 0,
    ENCODER_KEY_PRESS,
    ENCODER_KEY_RELEASE,
} encoder_key_event_t;

typedef struct {
    int32_t raw_delta;             // 本次读取期间累计的四倍频变化量
    int32_t raw_count;             // 上电以来累计的四倍频计数
    int32_t detent_count;          // raw_count / 4，近似对应“格数”
    float revolutions;             // raw_count / 80，近似对应圈数
    encoder_direction_t direction;
    encoder_key_event_t key_event;
} encoder_event_t;

esp_err_t encoder_init(void);
bool encoder_get_event(encoder_event_t *event);

const char *encoder_dir_to_str(encoder_direction_t dir);
const char *encoder_key_to_str(encoder_key_event_t key);

#endif
