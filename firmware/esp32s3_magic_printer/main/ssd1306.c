#include "ssd1306.h"

#include <string.h>
#include <stdbool.h>
#include "driver/i2c_master.h"
#include "esp_check.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "bsp_oled_codetab.h"

/*
 * 按你的实际接线修改这两个 GPIO。
 *
 * 如果你把 OLED 接到 DNESP32S3 板载 I2C 排针，请查看开发板原理图确认 SDA/SCL。
 * 这里给出的是通用示例值。
 */
#define OLED_I2C_PORT           I2C_NUM_0
#define OLED_I2C_SDA_GPIO       GPIO_NUM_4
#define OLED_I2C_SCL_GPIO       GPIO_NUM_5
#define OLED_I2C_FREQ_HZ        400000
#define OLED_I2C_TIMEOUT_MS     1000

static i2c_master_bus_handle_t s_i2c_bus = NULL;
static i2c_master_dev_handle_t s_oled_dev = NULL;

esp_err_t ssd1306_i2c_init(void)
{
    if (s_oled_dev != NULL) {
        return ESP_OK;
    }

    i2c_master_bus_config_t bus_config = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .i2c_port = OLED_I2C_PORT,
        .scl_io_num = OLED_I2C_SCL_GPIO,
        .sda_io_num = OLED_I2C_SDA_GPIO,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };

    ESP_RETURN_ON_ERROR(
        i2c_new_master_bus(&bus_config, &s_i2c_bus),
        "ssd1306",
        "i2c_new_master_bus failed"
    );

    i2c_device_config_t dev_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = SSD1306_I2C_ADDR,
        .scl_speed_hz = OLED_I2C_FREQ_HZ,
    };

    ESP_RETURN_ON_ERROR(
        i2c_master_bus_add_device(s_i2c_bus, &dev_config, &s_oled_dev),
        "ssd1306",
        "i2c_master_bus_add_device failed"
    );

    return ESP_OK;
}

static esp_err_t ssd1306_write_byte(uint8_t control, uint8_t value)
{
    uint8_t buf[2] = {control, value};

    return i2c_master_transmit(
        s_oled_dev,
        buf,
        sizeof(buf),
        pdMS_TO_TICKS(OLED_I2C_TIMEOUT_MS)
    );
}

esp_err_t ssd1306_write_cmd(uint8_t cmd)
{
    return ssd1306_write_byte(SSD1306_CTRL_CMD, cmd);
}

esp_err_t ssd1306_write_data(uint8_t data)
{
    return ssd1306_write_byte(SSD1306_CTRL_DATA, data);
}

esp_err_t ssd1306_init(void)
{
    ESP_RETURN_ON_ERROR(ssd1306_i2c_init(), "ssd1306", "i2c init failed");

    vTaskDelay(pdMS_TO_TICKS(100));

    /*
     * SSD1306 例程的初始化序列改写。
     * 128x64、页寻址模式、SEG/COM 重映射，适合常见 0.96 寸 OLED。
     */
    uint8_t init_cmds[] = {
        0xAE,       // display off
        0x81, 0xFF, // contrast
        0xA4,       // resume to RAM content display
        0xA6,       // normal display
        0x2E,       // deactivate scroll

        0x26, 0x00, 0x00, 0x03, 0x07, 0x00, 0xFF,

        0x20, 0x10, // page addressing mode
        0xB0,
        0x00,
        0x10,

        0x40,       // start line
        0xA1,       // segment remap
        0xA8, 0x3F, // multiplex ratio 1/64
        0xC8,       // COM output scan direction remapped
        0xD3, 0x00, // display offset
        0xDA, 0x12, // COM pins
        0xD9, 0x22, // pre-charge
        0xDB, 0x20, // VCOMH
        0x8D, 0x14, // charge pump enable
        0xAF        // display on
    };

    for (size_t i = 0; i < sizeof(init_cmds); i++) {
        ESP_RETURN_ON_ERROR(ssd1306_write_cmd(init_cmds[i]), "ssd1306", "init cmd failed");
    }

    ssd1306_clear();

    return ESP_OK;
}

void ssd1306_set_pos(uint8_t x, uint8_t page)
{
    if (x >= SSD1306_WIDTH) {
        x = SSD1306_WIDTH - 1;
    }

    if (page >= SSD1306_PAGES) {
        page = SSD1306_PAGES - 1;
    }

    ssd1306_write_cmd(0xB0 + page);
    ssd1306_write_cmd(((x & 0xF0) >> 4) | 0x10);
    ssd1306_write_cmd((x & 0x0F) | 0x01);
}

void ssd1306_fill(uint8_t data)
{
    for (uint8_t page = 0; page < SSD1306_PAGES; page++) {
        ssd1306_write_cmd(0xB0 + page);
        ssd1306_write_cmd(0x00);
        ssd1306_write_cmd(0x10);

        for (uint8_t col = 0; col < SSD1306_WIDTH; col++) {
            ssd1306_write_data(data);
        }
    }
}

void ssd1306_clear(void)
{
    ssd1306_fill(0x00);
}

void ssd1306_on(void)
{
    ssd1306_write_cmd(0x8D);
    ssd1306_write_cmd(0x14);
    ssd1306_write_cmd(0xAF);
}

void ssd1306_off(void)
{
    ssd1306_write_cmd(0x8D);
    ssd1306_write_cmd(0x10);
    ssd1306_write_cmd(0xAE);
}

void ssd1306_show_str(uint8_t x, uint8_t page, const char *str, uint8_t text_size)
{
    if (str == NULL) {
        return;
    }

    uint8_t c;
    uint8_t i;

    switch (text_size) {
    case 1: /* 6x8 */
        while (*str != '\0') {
            if ((uint8_t)*str < 32 || (uint8_t)*str > 126) {
                str++;
                continue;
            }

            c = (uint8_t)*str - 32;

            if (x > 126) {
                x = 0;
                page++;
            }

            if (page >= SSD1306_PAGES) {
                return;
            }

            ssd1306_set_pos(x, page);
            for (i = 0; i < 6; i++) {
                ssd1306_write_data(F6x8[c][i]);
            }

            x += 6;
            str++;
        }
        break;

    case 2: /* 8x16 */
        while (*str != '\0') {
            if ((uint8_t)*str < 32 || (uint8_t)*str > 126) {
                str++;
                continue;
            }

            c = (uint8_t)*str - 32;

            if (x > 120) {
                x = 0;
                page += 2;
            }

            if (page + 1 >= SSD1306_PAGES) {
                return;
            }

            ssd1306_set_pos(x, page);
            for (i = 0; i < 8; i++) {
                ssd1306_write_data(F8X16[c * 16 + i]);
            }

            ssd1306_set_pos(x, page + 1);
            for (i = 0; i < 8; i++) {
                ssd1306_write_data(F8X16[c * 16 + i + 8]);
            }

            x += 8;
            str++;
        }
        break;

    default:
        break;
    }
}

void ssd1306_show_cn(uint8_t x, uint8_t page, uint8_t index)
{
    uint16_t adder = 32U * index;

    if (page + 1 >= SSD1306_PAGES) {
        return;
    }

    ssd1306_set_pos(x, page);
    for (uint8_t i = 0; i < 16; i++) {
        ssd1306_write_data(F16x16[adder++]);
    }

    ssd1306_set_pos(x, page + 1);
    for (uint8_t i = 0; i < 16; i++) {
        ssd1306_write_data(F16x16[adder++]);
    }
}

void ssd1306_draw_bmp(uint8_t x0, uint8_t y0, uint8_t x1, uint8_t y1, const uint8_t *bmp)
{
    uint16_t j = 0;

    if (bmp == NULL) {
        return;
    }

    if (x1 > SSD1306_WIDTH) {
        x1 = SSD1306_WIDTH;
    }

    if (y1 > SSD1306_PAGES) {
        y1 = SSD1306_PAGES;
    }

    for (uint8_t page = y0; page < y1; page++) {
        ssd1306_set_pos(x0, page);
        for (uint8_t x = x0; x < x1; x++) {
            ssd1306_write_data(bmp[j++]);
        }
    }
}
