#include "thermal_printer.h"

#include <string.h>
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "printer";

static esp_err_t printer_send_bytes(const uint8_t *data, size_t len)
{
    if (data == NULL || len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    int written = uart_write_bytes(PRINTER_UART_NUM, (const char *)data, len);
    if (written < 0 || (size_t)written != len) {
        ESP_LOGE(TAG, "uart_write_bytes failed, written=%d, len=%u", written, (unsigned)len);
        return ESP_FAIL;
    }

    /*
     * 等待串口发送完成。
     * 注意：这只代表 UART 已发送完，不代表打印头已经完成打印。
     */
    uint32_t timeout_ms = (uint32_t)(((uint64_t)len * 12ULL * 1000ULL) / PRINTER_BAUD_RATE) + 3000U;

    if (timeout_ms < 3000U) {
        timeout_ms = 3000U;
    }

    if (timeout_ms > 60000U) {
        timeout_ms = 60000U;
    }

    return uart_wait_tx_done(PRINTER_UART_NUM, pdMS_TO_TICKS(timeout_ms));
}

esp_err_t thermal_printer_init(void)
{
    uart_config_t uart_config = {
        .baud_rate = PRINTER_BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    ESP_RETURN_ON_ERROR(uart_param_config(PRINTER_UART_NUM, &uart_config), TAG, "uart_param_config failed");

    ESP_RETURN_ON_ERROR(
        uart_set_pin(
            PRINTER_UART_NUM,
            PRINTER_TX_GPIO,
            PRINTER_RX_GPIO,
            UART_PIN_NO_CHANGE,
            UART_PIN_NO_CHANGE
        ),
        TAG,
        "uart_set_pin failed"
    );

    /*
     * TX 测试只需要 TX buffer；RX buffer 设 1024 方便后续读取状态。
     */
    ESP_RETURN_ON_ERROR(
        uart_driver_install(
            PRINTER_UART_NUM,
            1024,
            4096,
            0,
            NULL,
            0
        ),
        TAG,
        "uart_driver_install failed"
    );

    ESP_LOGI(TAG, "printer uart init done: UART%d TX=GPIO%d RX=GPIO%d baud=%d",
             PRINTER_UART_NUM,
             PRINTER_TX_GPIO,
             PRINTER_RX_GPIO,
             PRINTER_BAUD_RATE);

    return ESP_OK;
}

esp_err_t thermal_printer_write(const uint8_t *data, size_t len)
{
    return printer_send_bytes(data, len);
}

esp_err_t thermal_printer_write_ascii(const char *text)
{
    if (text == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    return printer_send_bytes((const uint8_t *)text, strlen(text));
}

esp_err_t thermal_printer_feed_lines(uint8_t lines)
{
    /*
     * ESC d n：打印并走纸 n 行
     */
    uint8_t cmd[] = {0x1B, 0x64, lines};
    return printer_send_bytes(cmd, sizeof(cmd));
}

esp_err_t thermal_printer_test_page(void)
{
    /*
     * 常用 ESC/POS：
     * ESC @      初始化
     * ESC a n    对齐：0左，1中，2右
     * ESC ! n    字体模式
     * GS ! n     字体宽高倍数
     */

    const uint8_t init[]      = {0x1B, 0x40};
    const uint8_t align_l[]   = {0x1B, 0x61, 0x00};
    const uint8_t align_c[]   = {0x1B, 0x61, 0x01};
    const uint8_t normal[]    = {0x1B, 0x21, 0x00};
    const uint8_t double_wh[] = {0x1D, 0x21, 0x11};
    const uint8_t normal_wh[] = {0x1D, 0x21, 0x00};

    /*
     * 中文必须按打印机字库编码发送。
     * 大多数国产 58mm 热敏模块支持 GBK/GB2312。
     *
     * 下面这行是 GBK 编码：
     * “中文打印测试”
     * 中 D6 D0
     * 文 CE C4
     * 打 B4 F2
     * 印 D3 A1
     * 测 B2 E2
     * 试 CA D4
     */
    const uint8_t chinese_gbk[] = {
        0xD6, 0xD0, 0xCE, 0xC4, 0xB4, 0xF2,
        0xD3, 0xA1, 0xB2, 0xE2, 0xCA, 0xD4,
        '\n'
    };

    ESP_RETURN_ON_ERROR(printer_send_bytes(init, sizeof(init)), TAG, "send init failed");
    vTaskDelay(pdMS_TO_TICKS(100));

    ESP_RETURN_ON_ERROR(printer_send_bytes(align_c, sizeof(align_c)), TAG, "send align failed");
    ESP_RETURN_ON_ERROR(printer_send_bytes(double_wh, sizeof(double_wh)), TAG, "send size failed");
    ESP_RETURN_ON_ERROR(thermal_printer_write_ascii("ESP32-S3\n"), TAG, "send ascii failed");
    ESP_RETURN_ON_ERROR(thermal_printer_write_ascii("Printer Test\n"), TAG, "send ascii failed");

    ESP_RETURN_ON_ERROR(printer_send_bytes(normal_wh, sizeof(normal_wh)), TAG, "send size failed");
    ESP_RETURN_ON_ERROR(printer_send_bytes(normal, sizeof(normal)), TAG, "send normal failed");

    ESP_RETURN_ON_ERROR(thermal_printer_write_ascii("----------------\n"), TAG, "send line failed");

    ESP_RETURN_ON_ERROR(printer_send_bytes(align_l, sizeof(align_l)), TAG, "send align failed");
    ESP_RETURN_ON_ERROR(thermal_printer_write_ascii("ASCII OK\n"), TAG, "send ascii failed");
    ESP_RETURN_ON_ERROR(printer_send_bytes(chinese_gbk, sizeof(chinese_gbk)), TAG, "send gbk failed");

    ESP_RETURN_ON_ERROR(thermal_printer_write_ascii("UART: 9600 8N1\n"), TAG, "send ascii failed");
    ESP_RETURN_ON_ERROR(thermal_printer_write_ascii("TX: GPIO17 -> Printer RXD\n"), TAG, "send ascii failed");
    ESP_RETURN_ON_ERROR(thermal_printer_write_ascii("----------------\n"), TAG, "send line failed");

    ESP_RETURN_ON_ERROR(thermal_printer_feed_lines(4), TAG, "feed failed");

    ESP_LOGI(TAG, "test page sent");

    return ESP_OK;
}
