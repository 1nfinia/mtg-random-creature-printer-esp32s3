#ifndef THERMAL_PRINTER_H
#define THERMAL_PRINTER_H

#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "driver/uart.h"
#include "driver/gpio.h"

/*
 * 默认接线：
 *
 * ESP32-S3 GPIO17 / UART1 TX -> 打印机 RXD
 * ESP32-S3 GPIO18 / UART1 RX <- 打印机 TXD，可选
 * ESP32-S3 GND              -> 打印机 GND
 *
 * 如果你测到打印机 TXD 是 5V，不要直接接 ESP32 RX。
 * 只做打印测试时，可以只接 ESP32 TX -> 打印机 RXD 和 GND。
 */

#define PRINTER_UART_NUM        UART_NUM_1
#define PRINTER_TX_GPIO         GPIO_NUM_17
#define PRINTER_RX_GPIO         GPIO_NUM_18

/*
 * DP-EH400 说明书常见默认值可能是 9600。
 * 若自检页显示 115200，把这里改成 115200。
 */
#define PRINTER_BAUD_RATE       9600

esp_err_t thermal_printer_init(void);
esp_err_t thermal_printer_write(const uint8_t *data, size_t len);
esp_err_t thermal_printer_write_ascii(const char *text);
esp_err_t thermal_printer_feed_lines(uint8_t lines);
esp_err_t thermal_printer_test_page(void);

#endif
