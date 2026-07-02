# Card Data Builder

<p>
  <strong>English</strong> |
  <a href="README_zh-CN.md">简体中文</a>
</p>

`card_data_builder.py` converts source card data into compact, indexed files for the ESP32-S3 firmware and the GBK/ESC-POS thermal-printer pipeline.

## What the script does

The builder:

1. Reads English/Scryfall-side card metadata.
2. Reads Simplified Chinese card data.
3. Joins records by card UUID.
4. Keeps creature cards only.
5. Excludes tokens and unsupported card faces.
6. Restricts cards to an integer CMC range.
7. Writes UTF-8 JSON Lines data.
8. Writes GBK printer records with a length prefix.
9. Generates per-CMC offset indexes.
10. Writes generation statistics.

## Requirements

- Python 3.9 or newer is recommended.
- No third-party Python packages are required by the current script.
- Sufficient free disk space for the source exports and generated output.

Check Python:

```powershell
python --version
```

## Input files

The script expects two line-oriented JSON files.

### Scryfall-side input

Example filename:

```text
scryfall_card.json
```

Required or used fields include:

```text
uuid
face_index
type_line
cmc
power
toughness
mana_cost
```

### Simplified Chinese input

Example filename:

```text
zhs_card.json
```

Required or used fields include:

```text
card_id
name
type_line
text
```

`card_id` is matched with the Scryfall-side `uuid`.

## Simplified Chinese data source

The Simplified Chinese card dataset used during development was obtained from:

[HeliumOctahelide/magic-cards-zhs](https://github.com/HeliumOctahelide/magic-cards-zhs)

That project publishes Simplified Chinese Magic card text and identifies its dataset license as **CC BY-SA 4.0**.

This repository does not bundle that complete dataset. Download it from the source project and review its README, release files, attribution requirements, and license before use.

When redistributing generated or modified datasets derived from `magic-cards-zhs`, you should, as applicable:

- Credit the original project and contributors.
- Link to the source repository.
- Identify that changes or filtering were performed.
- Include or link to the CC BY-SA 4.0 license.
- Preserve the same license for adapted material where the license requires it.

This section is practical project guidance, not legal advice.

Magic: The Gathering names, card text, symbols, and related intellectual property remain the property of their respective rights holders.

## Usage

From the repository root:

```powershell
cd tools\card_data_builder
```

Run:

```powershell
python card_data_builder.py `
  --scryfall path\to\scryfall_card.json `
  --zhs path\to\zhs_card.json `
  --out output `
  --cmc-min 1 `
  --cmc-max 15
```

PowerShell uses the backtick character for line continuation. The same command can be written on one line:

```powershell
python card_data_builder.py --scryfall path\to\scryfall_card.json --zhs path\to\zhs_card.json --out output --cmc-min 1 --cmc-max 15
```

On Linux or macOS:

```bash
python3 card_data_builder.py \
  --scryfall path/to/scryfall_card.json \
  --zhs path/to/zhs_card.json \
  --out output \
  --cmc-min 1 \
  --cmc-max 15
```

## Arguments

| Argument | Required | Default | Description |
|---|---:|---:|---|
| `--scryfall` | Yes | — | Path to the Scryfall-side JSON Lines file |
| `--zhs` | Yes | — | Path to the Simplified Chinese JSON Lines file |
| `--out` | No | `output` | Output directory |
| `--cmc-min` | No | `1` | Minimum accepted integer CMC |
| `--cmc-max` | No | `15` | Maximum accepted integer CMC |

## Filtering rules

The current script applies these main rules:

- `face_index` must be `-1` or `0`.
- English `type_line` must contain `Creature`.
- English `type_line` must not contain `Token`.
- CMC must be an integer inside the configured range.
- Missing UUIDs are skipped.
- Duplicate UUIDs are skipped.
- Empty duplicate Chinese records are skipped.
- Chinese and Scryfall records must match by UUID.

Review `stats.json` after generation to understand how many records were retained or skipped.

## Output

```text
output/
├── cards.dat
├── cards_printer.bin
├── stats.json
├── index/
│   ├── cmc_1.idx
│   └── ...
└── index_printer/
    ├── cmc_1.idx
    └── ...
```

### `cards.dat`

UTF-8 JSON Lines file containing compact merged card records.

### `cards_printer.bin`

Length-prefixed GBK printer records:

```text
[uint32 little-endian length][GBK/ESC-POS payload]
```

Characters that cannot be represented in GBK are replaced by the Python encoder because the script uses replacement error handling.

### Index files

Each `.idx` entry is an 8-byte little-endian file offset.

See:

[`../../docs/sd_card_format.md`](../../docs/sd_card_format.md)

## Copying output to the SD card

Copy these items to the SD card root:

```text
cards.dat
cards_printer.bin
index/
index_printer/
```

`stats.json` is optional for firmware operation but recommended for record keeping.

Do not copy the parent `output/` directory itself unless the files still end up in the SD card root as expected by the firmware.

## Example validation

Confirm all index files have valid sizes:

```python
from pathlib import Path

for path in Path("output").glob("index*/cmc_*.idx"):
    size = path.stat().st_size
    assert size % 8 == 0, f"Invalid index size: {path}"
    print(path, size // 8)
```

Confirm JSON and printer counts match:

```python
from pathlib import Path

for cmc in range(1, 16):
    json_idx = Path(f"output/index/cmc_{cmc}.idx")
    print_idx = Path(f"output/index_printer/cmc_{cmc}.idx")

    json_count = json_idx.stat().st_size // 8 if json_idx.exists() else 0
    print_count = print_idx.stat().st_size // 8 if print_idx.exists() else 0

    print(cmc, json_count, print_count)
```

## Customizing the print layout

Printer formatting is generated in the Python script before the records are written.

Look for functions related to:

```text
build_printer_text
build_printer_payload
gbk_encode_for_printer
```

Depending on the script revision, adjust:

- Card name alignment
- Mana cost and CMC line
- Type line
- Power/toughness line
- Divider characters
- Rules-text line breaks
- Feed commands
- ESC/POS font size or alignment commands

Regenerate all data and indexes after changing the format.

## Troubleshooting

### JSON parsing fails

The script prints the source filename, line number, column, and nearby content.

Check whether:

- The file is JSON Lines rather than one large JSON array.
- The line contains malformed escaping.
- The input was partially downloaded.
- The file encoding is UTF-8 or UTF-8 with BOM.

### Very few cards are generated

Check:

- UUID fields match between the two sources.
- The Scryfall-side type line uses `Creature`.
- The selected CMC range.
- `stats.json` skip counters.

### Chinese characters print as `?`

The character is probably unavailable in GBK or in the printer's built-in font.

Possible options:

- Replace the character during preprocessing.
- Use a printer font mode that supports it.
- Render text as a bitmap instead of using the built-in GBK font.

### ESP32 reports invalid printer record length

Do not mix files from different generation runs. Replace together:

```text
cards_printer.bin
index_printer/
```

Also confirm that the index offset points to the 4-byte record length field.

## Data and repository policy

Generated card databases are intentionally excluded from this firmware repository.

The Git repository should contain:

- Firmware source
- Builder source
- Format documentation
- Small synthetic test samples, if desired

It should not automatically include the complete generated card database unless all relevant licenses, attribution obligations, third-party terms, and intellectual-property considerations have been reviewed.
