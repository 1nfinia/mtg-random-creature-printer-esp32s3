#ifndef SD_CARD_READER_H
#define SD_CARD_READER_H

#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"

#define SD_CARD_MOUNT_POINT     "/sdcard"

esp_err_t sd_card_mount(void);
void sd_card_unmount(void);
bool sd_card_is_mounted(void);

esp_err_t sd_card_print_info(void);
esp_err_t sd_card_list_dir(const char *path);
esp_err_t sd_card_read_text_file(const char *path, size_t max_bytes);

#endif
