#ifndef SSD1306_H
#define SSD1306_H

#include <stdint.h>
#include "esp_err.h"

/*
 * 0.96 inch SSD1306 I2C OLED, usually 128x64.
 * 注意：
 * - 原 STM32 例程里的 OLED_ID = 0x78 是 8-bit I2C 写地址。
 * - ESP-IDF i2c_master_write_to_device() 使用 7-bit 地址，所以这里用 0x3C。
 */
#define SSD1306_I2C_ADDR        0x3C
#define SSD1306_WIDTH           128
#define SSD1306_HEIGHT          64
#define SSD1306_PAGES           8

#define SSD1306_CTRL_CMD        0x00
#define SSD1306_CTRL_DATA       0x40

esp_err_t ssd1306_i2c_init(void);
esp_err_t ssd1306_init(void);

esp_err_t ssd1306_write_cmd(uint8_t cmd);
esp_err_t ssd1306_write_data(uint8_t data);

void ssd1306_set_pos(uint8_t x, uint8_t page);
void ssd1306_fill(uint8_t data);
void ssd1306_clear(void);
void ssd1306_on(void);
void ssd1306_off(void);

void ssd1306_show_str(uint8_t x, uint8_t page, const char *str, uint8_t text_size);
void ssd1306_show_cn(uint8_t x, uint8_t page, uint8_t index);
void ssd1306_draw_bmp(uint8_t x0, uint8_t y0, uint8_t x1, uint8_t y1, const uint8_t *bmp);

#endif
