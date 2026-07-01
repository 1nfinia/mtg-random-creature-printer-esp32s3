#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_err.h"
#include "esp_log.h"

#include "ssd1306.h"
#include "encoder.h"
#include "sd_card_reader.h"
#include "card_db.h"
#include "thermal_printer.h"

static const char *TAG = "esp32 magic printer Ver0.1";

/*
 * 当前选择的 CMC
 */
static int s_current_cmc = 1;

/*
 * 编码器 raw count 累加。
 * EC11 软件解码中通常 4 个 raw count 对应 1 个定位格。
 */
static int s_encoder_raw_accum = 0;

/*
 * CMC 1~15 对应的卡牌数量。
 *
 * s_json_card_count:
 *   来自 /sdcard/index/cmc_x.idx
 *   对应 cards.dat，主要用于后续 OLED 显示卡名/调试。
 *
 * s_printer_card_count:
 *   来自 /sdcard/index_printer/cmc_x.idx
 *   对应 cards_printer.bin，当前阶段真正用于打印。
 */
static uint32_t s_json_card_count[CARD_DB_MAX_CMC + 1] = {0};
static uint32_t s_printer_card_count[CARD_DB_MAX_CMC + 1] = {0};

/*
 * OLED 底部状态行
 */
static char s_status[32] = "Init...";

/**
 * @brief 限制 CMC 范围
 */
static int clamp_cmc(int cmc)
{
    if (cmc < CARD_DB_MIN_CMC) {
        return CARD_DB_MIN_CMC;
    }

    if (cmc > CARD_DB_MAX_CMC) {
        return CARD_DB_MAX_CMC;
    }

    return cmc;
}

/**
 * @brief 刷新 OLED 主界面
 */
static void oled_show_main_ui(void)
{
    char line[32];

    ssd1306_clear();

    /*
     * OLED page 0~1
     */
    ssd1306_show_str(0, 0, "Magic Printer", 2);

    /*
     * OLED page 2~3
     */
    snprintf(line, sizeof(line), "CMC: %d", s_current_cmc);
    ssd1306_show_str(0, 2, line, 2);

    /*
     * OLED page 4~5
     * 当前阶段显示可打印卡牌数量，而不是 JSON 数量。
     */
    snprintf(line,
             sizeof(line),
             "Cards:%lu",
             (unsigned long)s_printer_card_count[s_current_cmc]);
    ssd1306_show_str(0, 4, line, 2);

    /*
     * OLED page 7，使用小字体显示状态。
     */
    ssd1306_show_str(0, 7, s_status, 1);
}

/**
 * @brief 读取 CMC 1~15 的索引数量
 */
static void load_card_counts(void)
{
    for (int cmc = CARD_DB_MIN_CMC; cmc <= CARD_DB_MAX_CMC; cmc++) {
        uint32_t json_count = 0;
        uint32_t printer_count = 0;

        esp_err_t ret_json = card_db_get_json_count((uint8_t)cmc, &json_count);
        esp_err_t ret_printer = card_db_get_printer_count((uint8_t)cmc, &printer_count);

        s_json_card_count[cmc] = (ret_json == ESP_OK) ? json_count : 0;
        s_printer_card_count[cmc] = (ret_printer == ESP_OK) ? printer_count : 0;

        ESP_LOGI(TAG,
                 "CMC %2d: json=%lu, printer=%lu",
                 cmc,
                 (unsigned long)s_json_card_count[cmc],
                 (unsigned long)s_printer_card_count[cmc]);

        if (s_json_card_count[cmc] != s_printer_card_count[cmc]) {
            ESP_LOGW(TAG,
                     "CMC %d count mismatch: json=%lu, printer=%lu",
                     cmc,
                     (unsigned long)s_json_card_count[cmc],
                     (unsigned long)s_printer_card_count[cmc]);
        }
    }
}

/**
 * @brief 根据编码器 raw_delta 更新 CMC
 */
static bool handle_encoder_rotation(int raw_delta)
{
    bool changed = false;

    s_encoder_raw_accum += raw_delta;

    while (s_encoder_raw_accum >= ENCODER_RAW_COUNTS_PER_DETENT) {
        int new_cmc = clamp_cmc(s_current_cmc + 1);

        if (new_cmc != s_current_cmc) {
            s_current_cmc = new_cmc;
            changed = true;
        }

        s_encoder_raw_accum -= ENCODER_RAW_COUNTS_PER_DETENT;
    }

    while (s_encoder_raw_accum <= -ENCODER_RAW_COUNTS_PER_DETENT) {
        int new_cmc = clamp_cmc(s_current_cmc - 1);

        if (new_cmc != s_current_cmc) {
            s_current_cmc = new_cmc;
            changed = true;
        }

        s_encoder_raw_accum += ENCODER_RAW_COUNTS_PER_DETENT;
    }

    if (changed) {
        snprintf(s_status, sizeof(s_status), "Press to Print");

        ESP_LOGI(TAG,
                 "CMC changed: %d, printable cards=%lu",
                 s_current_cmc,
                 (unsigned long)s_printer_card_count[s_current_cmc]);
    }

    return changed;
}

/**
 * @brief 按下编码器后随机读取当前 CMC 的打印数据并发送给热敏打印机
 *
 * 当前版本只读取 cards_printer.bin。
 * 不读取 cards.dat JSON，避免同一次按键中随机两次导致显示和打印不是同一张。
 */
static void draw_random_card(void)
{
    if (s_printer_card_count[s_current_cmc] == 0) {
        snprintf(s_status, sizeof(s_status), "No Card");
        ESP_LOGW(TAG, "No printable card for CMC %d", s_current_cmc);
        oled_show_main_ui();
        return;
    }

    snprintf(s_status, sizeof(s_status), "Reading...");
    oled_show_main_ui();

    uint8_t *payload = NULL;
    uint32_t payload_len = 0;
    uint32_t pick = 0;
    uint32_t total = 0;

    esp_err_t ret = card_db_read_random_printer_record(
        (uint8_t)s_current_cmc,
        &payload,
        &payload_len,
        &pick,
        &total
    );

    if (ret != ESP_OK) {
        snprintf(s_status, sizeof(s_status), "Read Fail");

        ESP_LOGW(TAG,
                 "card_db_read_random_printer_record failed: %s",
                 esp_err_to_name(ret));

        oled_show_main_ui();
        return;
    }

    ESP_LOGI(TAG,
             "Print card: CMC=%d, pick=%lu/%lu, payload_len=%lu",
             s_current_cmc,
             (unsigned long)pick,
             (unsigned long)total,
             (unsigned long)payload_len);

    snprintf(s_status, sizeof(s_status), "Printing...");
    oled_show_main_ui();

    ret = thermal_printer_write(payload, payload_len);

    free(payload);
    payload = NULL;

    if (ret == ESP_OK) {
        snprintf(s_status, sizeof(s_status), "Print OK");
        ESP_LOGI(TAG, "Print OK");
    } else {
        snprintf(s_status, sizeof(s_status), "Print Fail");

        ESP_LOGW(TAG,
                 "thermal_printer_write failed: %s",
                 esp_err_to_name(ret));
    }

    oled_show_main_ui();
}

void app_main(void)
{
    ESP_LOGI(TAG, "Magic printer version 0.1 start");

    /*
     * 1. 初始化 OLED
     */
    ESP_ERROR_CHECK(ssd1306_init());

    snprintf(s_status, sizeof(s_status), "OLED OK");
    oled_show_main_ui();
    vTaskDelay(pdMS_TO_TICKS(500));

    /*
     * 2. 初始化 EC11 编码器
     */
    ESP_ERROR_CHECK(encoder_init());

    snprintf(s_status, sizeof(s_status), "ENC OK");
    oled_show_main_ui();
    vTaskDelay(pdMS_TO_TICKS(500));

    /*
     * 3. 初始化热敏打印机 UART
     */
    snprintf(s_status, sizeof(s_status), "PRT Init...");
    oled_show_main_ui();

    ESP_ERROR_CHECK(thermal_printer_init());

    snprintf(s_status, sizeof(s_status), "PRT OK");
    oled_show_main_ui();
    vTaskDelay(pdMS_TO_TICKS(500));

    /*
     * 4. 挂载 SD 卡
     */
    snprintf(s_status, sizeof(s_status), "Mount SD...");
    oled_show_main_ui();

    esp_err_t ret = sd_card_mount();
    if (ret != ESP_OK) {
        snprintf(s_status, sizeof(s_status), "SD FAIL");
        oled_show_main_ui();

        ESP_LOGE(TAG, "SD card mount failed: %s", esp_err_to_name(ret));

        while (1) {
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }

    snprintf(s_status, sizeof(s_status), "SD OK");
    oled_show_main_ui();
    vTaskDelay(pdMS_TO_TICKS(500));

    /*
     * 5. 读取 CMC 1~15 的索引数量
     */
    snprintf(s_status, sizeof(s_status), "Load Index...");
    oled_show_main_ui();

    load_card_counts();

    snprintf(s_status, sizeof(s_status), "Press to Print");
    oled_show_main_ui();

    ESP_LOGI(TAG, "Stage 1 printer version ready");

    /*
     * 6. 主循环
     *
     * 旋转 EC11：
     *   改变 CMC
     *
     * 按下 EC11：
     *   从 cards_printer.bin 随机读取一条打印数据
     *   通过 UART 发给热敏打印机
     */
    while (1) {
        encoder_event_t event;

        if (encoder_get_event(&event)) {
            bool need_update = false;

            if (event.raw_delta != 0) {
                need_update |= handle_encoder_rotation(event.raw_delta);

                ESP_LOGI(TAG,
                         "encoder raw_delta=%ld, direction=%s",
                         (long)event.raw_delta,
                         encoder_dir_to_str(event.direction));
            }

            /*
             * 只在 PRESS 时触发打印。
             * RELEASE 不触发，避免一次按键打印两次。
             */
            if (event.key_event == ENCODER_KEY_PRESS) {
                ESP_LOGI(TAG, "Button pressed, print CMC=%d", s_current_cmc);

                draw_random_card();

                /*
                 * draw_random_card() 内部已经刷新 OLED。
                 */
                need_update = false;
            }

            if (need_update) {
                oled_show_main_ui();
            }
        }

        vTaskDelay(pdMS_TO_TICKS(20));
    }
}