#ifndef CARD_DB_H
#define CARD_DB_H

#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

/*
 * SD 卡目录结构：
 *
 * /sdcard/cards.dat
 * /sdcard/cards_printer.bin
 * /sdcard/index/cmc_x.idx
 * /sdcard/index_printer/cmc_x.idx
 *
 * index/cmc_x.idx 每 8 字节一个 little-endian uint64 offset，指向 cards.dat。
 * index_printer/cmc_x.idx 每 8 字节一个 little-endian uint64 offset，指向 cards_printer.bin 中的记录。
 * cards_printer.bin 每条记录格式：
 * [4 字节 little-endian uint32 payload_len][GBK + ESC/POS payload]
 */

#define CARD_DB_MIN_CMC                 1
#define CARD_DB_MAX_CMC                 15
#define CARD_DB_MAX_JSON_LINE_BYTES     (32 * 1024)
#define CARD_DB_MAX_PRINTER_RECORD      (32 * 1024)

esp_err_t card_db_print_counts(void);
esp_err_t card_db_get_json_count(uint8_t cmc, uint32_t *out_count);
esp_err_t card_db_get_printer_count(uint8_t cmc, uint32_t *out_count);

esp_err_t card_db_read_random_json(uint8_t cmc,
                                   char **out_json,
                                   size_t *out_len,
                                   uint32_t *out_pick_index,
                                   uint32_t *out_total_count);

esp_err_t card_db_read_random_printer_record(uint8_t cmc,
                                             uint8_t **out_payload,
                                             uint32_t *out_payload_len,
                                             uint32_t *out_pick_index,
                                             uint32_t *out_total_count);

#endif
