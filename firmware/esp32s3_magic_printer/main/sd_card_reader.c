#include "sd_card_reader.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/unistd.h>

#include "driver/gpio.h"
#include "driver/sdspi_host.h"
#include "driver/spi_common.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "ff.h"
#include "sdmmc_cmd.h"

#define SD_SPI_HOST         SPI2_HOST
#define SD_SPI_SCLK_GPIO    GPIO_NUM_12
#define SD_SPI_MOSI_GPIO    GPIO_NUM_11
#define SD_SPI_MISO_GPIO    GPIO_NUM_13
#define SD_SPI_CS_GPIO      GPIO_NUM_2

static const char *TAG = "sd_reader";

static sdmmc_card_t *s_card = NULL;
static bool s_bus_initialized = false;

bool sd_card_is_mounted(void)
{
    return s_card != NULL;
}

esp_err_t sd_card_mount(void)
{
    if (s_card != NULL) {
        ESP_LOGW(TAG, "SD card already mounted");
        return ESP_OK;
    }

    esp_err_t ret;

    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 5,
        .allocation_unit_size = 16 * 1024,
    };

    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = SD_SPI_HOST;

    spi_bus_config_t bus_cfg = {
        .mosi_io_num = SD_SPI_MOSI_GPIO,
        .miso_io_num = SD_SPI_MISO_GPIO,
        .sclk_io_num = SD_SPI_SCLK_GPIO,
        .quadwp_io_num = GPIO_NUM_NC,
        .quadhd_io_num = GPIO_NUM_NC,
        .max_transfer_sz = 4096,
    };

    if (!s_bus_initialized) {
        ret = spi_bus_initialize(host.slot, &bus_cfg, SDSPI_DEFAULT_DMA);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "spi_bus_initialize failed: %s", esp_err_to_name(ret));
            return ret;
        }
        s_bus_initialized = true;
    }

    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.host_id = host.slot;
    slot_config.gpio_cs = SD_SPI_CS_GPIO;

    ESP_LOGI(TAG, "Mounting SD card...");
    ESP_LOGI(TAG, "SPI pins: SCLK=%d, MOSI=%d, MISO=%d, CS=%d",
             SD_SPI_SCLK_GPIO, SD_SPI_MOSI_GPIO, SD_SPI_MISO_GPIO, SD_SPI_CS_GPIO);

    ret = esp_vfs_fat_sdspi_mount(
        SD_CARD_MOUNT_POINT,
        &host,
        &slot_config,
        &mount_config,
        &s_card
    );

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to mount SD card: %s", esp_err_to_name(ret));
        ESP_LOGE(TAG, "Check card inserted, FAT32/exFAT format, board TF slot, GPIO pins");
        return ret;
    }

    ESP_LOGI(TAG, "SD card mounted at %s", SD_CARD_MOUNT_POINT);
    sdmmc_card_print_info(stdout, s_card);

    return ESP_OK;
}

void sd_card_unmount(void)
{
    if (s_card != NULL) {
        esp_vfs_fat_sdcard_unmount(SD_CARD_MOUNT_POINT, s_card);
        s_card = NULL;
        ESP_LOGI(TAG, "SD card unmounted");
    }

    if (s_bus_initialized) {
        spi_bus_free(SD_SPI_HOST);
        s_bus_initialized = false;
        ESP_LOGI(TAG, "SPI bus freed");
    }
}

esp_err_t sd_card_print_info(void)
{
    if (s_card == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    FATFS *fs = NULL;
    DWORD free_clusters = 0;
    FRESULT res = f_getfree("0:", &free_clusters, &fs);
    if (res != FR_OK || fs == NULL) {
        ESP_LOGW(TAG, "f_getfree failed: %d", res);
        return ESP_FAIL;
    }

    uint64_t total_bytes = (uint64_t)(fs->n_fatent - 2) * fs->csize * 512;
    uint64_t free_bytes = (uint64_t)free_clusters * fs->csize * 512;

    ESP_LOGI(TAG, "FATFS total: %llu MB", (unsigned long long)(total_bytes / (1024ULL * 1024ULL)));
    ESP_LOGI(TAG, "FATFS free : %llu MB", (unsigned long long)(free_bytes / (1024ULL * 1024ULL)));

    return ESP_OK;
}

esp_err_t sd_card_list_dir(const char *path)
{
    if (s_card == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    DIR *dir = opendir(path);
    if (dir == NULL) {
        ESP_LOGE(TAG, "Failed to open directory: %s", path);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Listing directory: %s", path);

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        size_t path_len = strlen(path);
        size_t name_len = strlen(entry->d_name);

        char *full_path = (char *)malloc(path_len + 1 + name_len + 1);
        if (full_path == NULL) {
            ESP_LOGW(TAG, "malloc failed while listing: %s", entry->d_name);
            continue;
        }

        memcpy(full_path, path, path_len);
        full_path[path_len] = '/';
        memcpy(full_path + path_len + 1, entry->d_name, name_len + 1);

        struct stat st;
        if (stat(full_path, &st) == 0) {
            if (S_ISDIR(st.st_mode)) {
                ESP_LOGI(TAG, "DIR : %s", entry->d_name);
            } else {
                ESP_LOGI(TAG, "FILE: %s (%ld bytes)", entry->d_name, (long)st.st_size);
            }
        } else {
            ESP_LOGI(TAG, "ITEM: %s", entry->d_name);
        }

        free(full_path);
    }

    closedir(dir);
    return ESP_OK;
}

esp_err_t sd_card_read_text_file(const char *path, size_t max_bytes)
{
    if (s_card == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    FILE *f = fopen(path, "r");
    if (f == NULL) {
        ESP_LOGW(TAG, "File not found or open failed: %s", path);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Reading file: %s", path);

    size_t total = 0;
    char buf[129];

    while (total < max_bytes) {
        size_t want = sizeof(buf) - 1;
        if (want > max_bytes - total) {
            want = max_bytes - total;
        }

        size_t n = fread(buf, 1, want, f);
        if (n == 0) {
            break;
        }

        buf[n] = '\0';
        printf("%s", buf);
        total += n;
    }

    printf("\n");
    fclose(f);

    ESP_LOGI(TAG, "Read %u bytes from %s", (unsigned)total, path);
    return ESP_OK;
}
