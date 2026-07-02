# SD Card Data Format

<p>
  <strong>English</strong> |
  <a href="sd_card_format_zh-CN.md">简体中文</a>
</p>

This document defines the SD card directory structure and binary formats used by the firmware.

## File system

For initial testing, format the card as FAT32.

The firmware mounts the card at:

```text
/sdcard
```

The generated files must be placed in the SD card root directory.

## Directory structure

```text
SD card root
├── cards.dat
├── cards_printer.bin
├── stats.json
├── index/
│   ├── cmc_1.idx
│   ├── cmc_2.idx
│   ├── ...
│   └── cmc_15.idx
└── index_printer/
    ├── cmc_1.idx
    ├── cmc_2.idx
    ├── ...
    └── cmc_15.idx
```

`stats.json` is useful for validation but is not required by the current firmware.

## Byte order

All integer fields in the index and printer binary formats are stored in **little-endian** order.

## `cards.dat`

`cards.dat` is a UTF-8 JSON Lines file.

- One card is stored per line.
- Each line is an independent JSON object.
- Newline characters inside rules text are represented as escaped JSON text.
- Index offsets point to the first byte of the corresponding JSON line.

Example:

```json
{"card_id":"931345ae-39ab-5fd2-a6d7-51620e7db176","name":"犄牙兽","type_line":"生物～野兽","text":"当犄牙兽进战场时，你获得5点生命。\n当犄牙兽离开战场时，派出一个3/3绿色野兽衍生生物。","face_index":-1,"cmc":5,"power":"5","toughness":"3","mana_cost":"{4}{G}"}
```

Current retained fields:

| Field | Meaning |
|---|---|
| `card_id` | Card UUID used to merge data sources |
| `name` | Simplified Chinese card name |
| `type_line` | Simplified Chinese type line |
| `text` | Simplified Chinese rules text |
| `face_index` | Card-face selector |
| `cmc` | Integer mana value |
| `power` | Creature power |
| `toughness` | Creature toughness |
| `mana_cost` | Mana cost string |

The current printer-only firmware does not parse this file during printing, but it is retained for debugging and future OLED card previews.

## `index/cmc_x.idx`

Each file contains offsets into `cards.dat` for one CMC group.

Each entry is:

```text
uint64 little-endian offset
```

Entry size:

```text
8 bytes
```

Therefore:

```text
card count = index file size / 8
```

Example layout:

```text
byte 0..7    → offset of card 0 in cards.dat
byte 8..15   → offset of card 1 in cards.dat
byte 16..23  → offset of card 2 in cards.dat
...
```

The offset points to the first byte of a JSON line.

## `cards_printer.bin`

This file contains variable-length printer records.

Each record is:

```text
[uint32 little-endian payload_length][payload bytes]
```

The payload contains data prepared for the target printer:

- GBK-encoded Chinese text
- CRLF line endings
- Optional ESC/POS commands
- No terminating NUL byte is required

Record layout:

```text
offset + 0 ... 3                  uint32 payload length
offset + 4 ... 4+length-1         printer payload
```

The firmware reads the length, allocates a buffer, reads the payload, and sends it with `uart_write_bytes()`.

The current firmware rejects records larger than 32 KiB.

## `index_printer/cmc_x.idx`

Each file contains offsets into `cards_printer.bin` for one CMC group.

Each entry is:

```text
uint64 little-endian offset
```

The offset points to the **start of the 4-byte length field**, not directly to the payload.

Reading procedure:

```text
1. Read an 8-byte offset from index_printer/cmc_x.idx
2. Seek cards_printer.bin to that offset
3. Read a 4-byte little-endian payload length
4. Read payload_length bytes
5. Send the bytes to the printer
```

## Random lookup procedure

For a selected CMC:

```text
count = index_size / 8
pick = random_number % count
seek index file to pick * 8
read uint64 offset
seek data file to offset
read record
```

The current firmware uses `esp_random()` to select `pick`.

## Relationship between JSON and printer indexes

The builder generates the JSON and printer records during the same merge pass.

When both index directories contain the same count and ordering, the same `pick` value can be used to retrieve:

- A JSON record for display
- The matching printer payload

The current first-stage firmware reads only the printer record. A future version can use a shared pick to display and print the same card.

## Validation rules

Before copying the files to the SD card, verify:

- Every `.idx` file size is a multiple of 8.
- Offsets do not point beyond the corresponding data file.
- Every printer record length fits inside `cards_printer.bin`.
- Printer payload length is not zero.
- Printer payload length does not exceed the firmware limit.
- JSON and printer index counts match for each CMC when paired lookup is required.

## Python inspection examples

Read the first printer index offset:

```python
import struct

with open("index_printer/cmc_5.idx", "rb") as f:
    offset = struct.unpack("<Q", f.read(8))[0]

print(offset)
```

Read the corresponding printer record:

```python
import struct

with open("cards_printer.bin", "rb") as f:
    f.seek(offset)
    length = struct.unpack("<I", f.read(4))[0]
    payload = f.read(length)

print(length)
print(payload[:64].hex(" "))
```

Count cards in an index:

```python
from pathlib import Path

size = Path("index_printer/cmc_5.idx").stat().st_size
assert size % 8 == 0
print(size // 8)
```

## Updating the database

Replace the complete generated set together:

```text
cards.dat
cards_printer.bin
index/
index_printer/
stats.json
```

Do not mix index files from one generation with data files from another generation, because offsets will no longer be valid.
