#include "card_db.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_log.h"
#include "esp_random.h"
#include "sd_card_reader.h"

static const char *TAG = "card_db";

#define CARD_DB_CARDS_DAT       SD_CARD_MOUNT_POINT "/cards.dat"
#define CARD_DB_PRINTER_BIN     SD_CARD_MOUNT_POINT "/cards_printer.bin"

static esp_err_t build_idx_path(char *buf, size_t buf_size, uint8_t cmc, bool printer_index)
{
    if (cmc < CARD_DB_MIN_CMC || cmc > CARD_DB_MAX_CMC) {
        return ESP_ERR_INVALID_ARG;
    }

    int n;
    if (printer_index) {
        n = snprintf(buf, buf_size, SD_CARD_MOUNT_POINT "/index_printer/cmc_%u.idx", (unsigned)cmc);
    } else {
        n = snprintf(buf, buf_size, SD_CARD_MOUNT_POINT "/index/cmc_%u.idx", (unsigned)cmc);
    }

    if (n < 0 || (size_t)n >= buf_size) {
        return ESP_ERR_INVALID_SIZE;
    }

    return ESP_OK;
}

static esp_err_t read_le_u64(FILE *f, uint64_t *out)
{
    uint8_t b[8];
    if (fread(b, 1, sizeof(b), f) != sizeof(b)) {
        return ESP_FAIL;
    }

    *out = ((uint64_t)b[0]) |
           ((uint64_t)b[1] << 8) |
           ((uint64_t)b[2] << 16) |
           ((uint64_t)b[3] << 24) |
           ((uint64_t)b[4] << 32) |
           ((uint64_t)b[5] << 40) |
           ((uint64_t)b[6] << 48) |
           ((uint64_t)b[7] << 56);

    return ESP_OK;
}

static esp_err_t read_le_u32(FILE *f, uint32_t *out)
{
    uint8_t b[4];
    if (fread(b, 1, sizeof(b), f) != sizeof(b)) {
        return ESP_FAIL;
    }

    *out = ((uint32_t)b[0]) |
           ((uint32_t)b[1] << 8) |
           ((uint32_t)b[2] << 16) |
           ((uint32_t)b[3] << 24);

    return ESP_OK;
}

static esp_err_t get_idx_count(const char *idx_path, uint32_t *out_count)
{
    if (out_count == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    *out_count = 0;

    FILE *f = fopen(idx_path, "rb");
    if (f == NULL) {
        ESP_LOGW(TAG, "index open failed: %s", idx_path);
        return ESP_FAIL;
    }

    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return ESP_FAIL;
    }

    long size = ftell(f);
    fclose(f);

    if (size < 0) {
        return ESP_FAIL;
    }

    if ((size % 8) != 0) {
        ESP_LOGW(TAG, "index size is not multiple of 8: %s, size=%ld", idx_path, size);
    }

    *out_count = (uint32_t)(size / 8);
    return ESP_OK;
}

static esp_err_t read_idx_offset(const char *idx_path,
                                 uint32_t pick_index,
                                 uint64_t *out_offset)
{
    FILE *f = fopen(idx_path, "rb");
    if (f == NULL) {
        ESP_LOGE(TAG, "index open failed: %s", idx_path);
        return ESP_FAIL;
    }

    long seek_pos = (long)((uint64_t)pick_index * 8ULL);
    if (fseek(f, seek_pos, SEEK_SET) != 0) {
        fclose(f);
        return ESP_FAIL;
    }

    esp_err_t ret = read_le_u64(f, out_offset);
    fclose(f);

    return ret;
}

static esp_err_t read_line_alloc(FILE *f, char **out_line, size_t *out_len, size_t max_len)
{
    if (out_line == NULL || out_len == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    *out_line = NULL;
    *out_len = 0;

    size_t cap = 512;
    char *buf = (char *)malloc(cap);
    if (buf == NULL) {
        return ESP_ERR_NO_MEM;
    }

    size_t len = 0;

    while (len < max_len) {
        int ch = fgetc(f);
        if (ch == EOF) {
            break;
        }

        if (ch == '\n') {
            break;
        }

        if (len + 1 >= cap) {
            size_t new_cap = cap * 2;
            if (new_cap > max_len + 1) {
                new_cap = max_len + 1;
            }

            char *new_buf = (char *)realloc(buf, new_cap);
            if (new_buf == NULL) {
                free(buf);
                return ESP_ERR_NO_MEM;
            }

            buf = new_buf;
            cap = new_cap;
        }

        buf[len++] = (char)ch;
    }

    if (len == 0) {
        free(buf);
        return ESP_FAIL;
    }

    buf[len] = '\0';

    *out_line = buf;
    *out_len = len;
    return ESP_OK;
}

esp_err_t card_db_get_json_count(uint8_t cmc, uint32_t *out_count)
{
    char path[96];
    esp_err_t ret = build_idx_path(path, sizeof(path), cmc, false);
    if (ret != ESP_OK) {
        return ret;
    }

    return get_idx_count(path, out_count);
}

esp_err_t card_db_get_printer_count(uint8_t cmc, uint32_t *out_count)
{
    char path[104];
    esp_err_t ret = build_idx_path(path, sizeof(path), cmc, true);
    if (ret != ESP_OK) {
        return ret;
    }

    return get_idx_count(path, out_count);
}

esp_err_t card_db_print_counts(void)
{
    ESP_LOGI(TAG, "CMC index counts:");

    for (uint8_t cmc = CARD_DB_MIN_CMC; cmc <= CARD_DB_MAX_CMC; cmc++) {
        uint32_t json_count = 0;
        uint32_t printer_count = 0;

        esp_err_t ret_json = card_db_get_json_count(cmc, &json_count);
        esp_err_t ret_printer = card_db_get_printer_count(cmc, &printer_count);

        if (ret_json == ESP_OK || ret_printer == ESP_OK) {
            ESP_LOGI(TAG, "CMC %2u: json=%lu, printer=%lu",
                     (unsigned)cmc,
                     (unsigned long)json_count,
                     (unsigned long)printer_count);
        } else {
            ESP_LOGW(TAG, "CMC %2u: index missing or unreadable", (unsigned)cmc);
        }
    }

    return ESP_OK;
}

esp_err_t card_db_read_random_json(uint8_t cmc,
                                   char **out_json,
                                   size_t *out_len,
                                   uint32_t *out_pick_index,
                                   uint32_t *out_total_count)
{
    if (out_json == NULL || out_len == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    *out_json = NULL;
    *out_len = 0;

    char idx_path[96];
    esp_err_t ret = build_idx_path(idx_path, sizeof(idx_path), cmc, false);
    if (ret != ESP_OK) {
        return ret;
    }

    uint32_t count = 0;
    ret = get_idx_count(idx_path, &count);
    if (ret != ESP_OK) {
        return ret;
    }

    if (count == 0) {
        ESP_LOGW(TAG, "No JSON card for CMC %u", (unsigned)cmc);
        return ESP_ERR_NOT_FOUND;
    }

    uint32_t pick = esp_random() % count;
    uint64_t offset = 0;

    ret = read_idx_offset(idx_path, pick, &offset);
    if (ret != ESP_OK) {
        return ret;
    }

    FILE *cards = fopen(CARD_DB_CARDS_DAT, "rb");
    if (cards == NULL) {
        ESP_LOGE(TAG, "open failed: %s", CARD_DB_CARDS_DAT);
        return ESP_FAIL;
    }

    if (fseek(cards, (long)offset, SEEK_SET) != 0) {
        fclose(cards);
        return ESP_FAIL;
    }

    ret = read_line_alloc(cards, out_json, out_len, CARD_DB_MAX_JSON_LINE_BYTES);
    fclose(cards);

    if (ret == ESP_OK) {
        if (out_pick_index != NULL) {
            *out_pick_index = pick;
        }
        if (out_total_count != NULL) {
            *out_total_count = count;
        }

        ESP_LOGI(TAG, "JSON CMC %u pick %lu/%lu offset=%llu len=%u",
                 (unsigned)cmc,
                 (unsigned long)pick,
                 (unsigned long)count,
                 (unsigned long long)offset,
                 (unsigned)*out_len);
    }

    return ret;
}

esp_err_t card_db_read_random_printer_record(uint8_t cmc,
                                             uint8_t **out_payload,
                                             uint32_t *out_payload_len,
                                             uint32_t *out_pick_index,
                                             uint32_t *out_total_count)
{
    if (out_payload == NULL || out_payload_len == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    *out_payload = NULL;
    *out_payload_len = 0;

    char idx_path[104];
    esp_err_t ret = build_idx_path(idx_path, sizeof(idx_path), cmc, true);
    if (ret != ESP_OK) {
        return ret;
    }

    uint32_t count = 0;
    ret = get_idx_count(idx_path, &count);
    if (ret != ESP_OK) {
        return ret;
    }

    if (count == 0) {
        ESP_LOGW(TAG, "No printer card for CMC %u", (unsigned)cmc);
        return ESP_ERR_NOT_FOUND;
    }

    uint32_t pick = esp_random() % count;
    uint64_t offset = 0;

    ret = read_idx_offset(idx_path, pick, &offset);
    if (ret != ESP_OK) {
        return ret;
    }

    FILE *bin = fopen(CARD_DB_PRINTER_BIN, "rb");
    if (bin == NULL) {
        ESP_LOGE(TAG, "open failed: %s", CARD_DB_PRINTER_BIN);
        return ESP_FAIL;
    }

    if (fseek(bin, (long)offset, SEEK_SET) != 0) {
        fclose(bin);
        return ESP_FAIL;
    }

    uint32_t payload_len = 0;
    ret = read_le_u32(bin, &payload_len);
    if (ret != ESP_OK) {
        fclose(bin);
        return ret;
    }

    if (payload_len == 0 || payload_len > CARD_DB_MAX_PRINTER_RECORD) {
        ESP_LOGE(TAG, "invalid printer record length: %lu", (unsigned long)payload_len);
        fclose(bin);
        return ESP_ERR_INVALID_SIZE;
    }

    uint8_t *payload = (uint8_t *)malloc(payload_len);
    if (payload == NULL) {
        fclose(bin);
        return ESP_ERR_NO_MEM;
    }

    size_t n = fread(payload, 1, payload_len, bin);
    fclose(bin);

    if (n != payload_len) {
        free(payload);
        ESP_LOGE(TAG, "printer payload read incomplete: want=%lu got=%u",
                 (unsigned long)payload_len,
                 (unsigned)n);
        return ESP_FAIL;
    }

    *out_payload = payload;
    *out_payload_len = payload_len;

    if (out_pick_index != NULL) {
        *out_pick_index = pick;
    }
    if (out_total_count != NULL) {
        *out_total_count = count;
    }

    ESP_LOGI(TAG, "PRINTER CMC %u pick %lu/%lu offset=%llu payload_len=%lu",
             (unsigned)cmc,
             (unsigned long)pick,
             (unsigned long)count,
             (unsigned long long)offset,
             (unsigned long)payload_len);

    return ESP_OK;
}
