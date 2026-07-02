# 卡牌数据生成工具

<p>
  <a href="README.md">English</a> |
  <strong>简体中文</strong>
</p>

`card_data_builder.py` 用于将来源卡牌数据转换为适合 ESP32-S3 固件和 GBK/ESC-POS 热敏打印流程使用的紧凑索引文件。

## 脚本功能

该工具会：

1. 读取英文/Scryfall 侧卡牌元数据。
2. 读取简体中文卡牌数据。
3. 按卡牌 UUID 合并记录。
4. 仅保留生物牌。
5. 排除衍生物和不支持的卡面。
6. 按整数 CMC 范围筛选。
7. 生成 UTF-8 JSON Lines 数据。
8. 生成带长度前缀的 GBK 打印记录。
9. 按 CMC 生成文件偏移索引。
10. 输出生成统计信息。

## 环境要求

- 建议使用 Python 3.9 或更高版本。
- 当前脚本不需要安装第三方 Python 包。
- 磁盘需要有足够空间保存来源数据和生成文件。

检查 Python：

```powershell
python --version
```

## 输入文件

脚本需要两个按行存储 JSON 对象的文件。

### Scryfall 侧输入

示例文件名：

```text
scryfall_card.json
```

脚本使用的主要字段包括：

```text
uuid
face_index
type_line
cmc
power
toughness
mana_cost
```

### 简体中文输入

示例文件名：

```text
zhs_card.json
```

脚本使用的主要字段包括：

```text
card_id
name
type_line
text
```

`card_id` 会与 Scryfall 侧的 `uuid` 进行匹配。

## 简体中文卡牌数据来源

项目开发期间使用的简体中文卡牌数据来源于：

[HeliumOctahelide/magic-cards-zhs](https://github.com/HeliumOctahelide/magic-cards-zhs)

该项目提供万智牌简体中文卡牌文本，并在仓库中将数据许可证标注为 **CC BY-SA 4.0**。

本仓库不直接附带完整数据集。请从来源项目下载，并在使用前阅读其 README、Release 文件、署名要求和许可证。

如果重新发布由 `magic-cards-zhs` 生成或修改的数据集，应根据实际适用情况：

- 署名原项目及贡献者。
- 提供来源仓库链接。
- 说明进行了筛选、合并或其他修改。
- 附带或链接 CC BY-SA 4.0 许可证。
- 在许可证要求适用时，以相同许可证发布改编数据。

本节仅提供项目实践建议，不构成法律意见。

Magic: The Gathering 的名称、卡牌文字、符号和相关知识产权仍归对应权利人所有。

## 使用方法

在仓库根目录执行：

```powershell
cd tools\card_data_builder
```

运行：

```powershell
python card_data_builder.py `
  --scryfall path\to\scryfall_card.json `
  --zhs path\to\zhs_card.json `
  --out output `
  --cmc-min 1 `
  --cmc-max 15
```

PowerShell 使用反引号作为换行连接符。也可以写为一行：

```powershell
python card_data_builder.py --scryfall path\to\scryfall_card.json --zhs path\to\zhs_card.json --out output --cmc-min 1 --cmc-max 15
```

Linux 或 macOS：

```bash
python3 card_data_builder.py \
  --scryfall path/to/scryfall_card.json \
  --zhs path/to/zhs_card.json \
  --out output \
  --cmc-min 1 \
  --cmc-max 15
```

## 参数说明

| 参数 | 必需 | 默认值 | 说明 |
|---|---:|---:|---|
| `--scryfall` | 是 | — | Scryfall 侧 JSON Lines 文件路径 |
| `--zhs` | 是 | — | 简体中文 JSON Lines 文件路径 |
| `--out` | 否 | `output` | 输出目录 |
| `--cmc-min` | 否 | `1` | 接受的最小整数 CMC |
| `--cmc-max` | 否 | `15` | 接受的最大整数 CMC |

## 筛选规则

当前脚本主要应用以下规则：

- `face_index` 必须为 `-1` 或 `0`。
- 英文 `type_line` 必须包含 `Creature`。
- 英文 `type_line` 不能包含 `Token`。
- CMC 必须是配置范围内的整数。
- 跳过缺少 UUID 的记录。
- 跳过重复 UUID。
- 跳过中文数据中的空重复记录。
- 中文数据与 Scryfall 数据必须通过 UUID 匹配。

生成后请查看 `stats.json`，了解保留和跳过的记录数量。

## 输出文件

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

UTF-8 JSON Lines 文件，包含精简后的合并卡牌记录。

### `cards_printer.bin`

带长度前缀的 GBK 打印记录：

```text
[uint32 little-endian 长度][GBK/ESC-POS payload]
```

脚本使用替换错误处理，因此 GBK 无法表示的字符会被 Python 编码器替换。

### 索引文件

每个 `.idx` 索引项为 8 字节 little-endian 文件偏移量。

详细格式见：

[`../../docs/sd_card_format_zh-CN.md`](../../docs/sd_card_format_zh-CN.md)

## 复制到 SD 卡

将以下内容复制到 SD 卡根目录：

```text
cards.dat
cards_printer.bin
index/
index_printer/
```

固件运行不强制要求 `stats.json`，但建议保留用于记录和检查。

不要直接把父级 `output/` 目录复制到 SD 卡后再多套一层目录，除非最终文件仍位于固件所要求的 SD 卡根目录。

## 生成结果校验

检查所有索引文件大小是否合法：

```python
from pathlib import Path

for path in Path("output").glob("index*/cmc_*.idx"):
    size = path.stat().st_size
    assert size % 8 == 0, f"Invalid index size: {path}"
    print(path, size // 8)
```

检查 JSON 和打印记录数量是否一致：

```python
from pathlib import Path

for cmc in range(1, 16):
    json_idx = Path(f"output/index/cmc_{cmc}.idx")
    print_idx = Path(f"output/index_printer/cmc_{cmc}.idx")

    json_count = json_idx.stat().st_size // 8 if json_idx.exists() else 0
    print_count = print_idx.stat().st_size // 8 if print_idx.exists() else 0

    print(cmc, json_count, print_count)
```

## 修改打印排版

打印排版在 Python 脚本生成记录时完成。

可以查找以下相关函数：

```text
build_printer_text
build_printer_payload
gbk_encode_for_printer
```

不同脚本版本中的具体函数名可能略有差异。可修改：

- 卡名对齐
- 法术力费用和 CMC 行
- 类型栏
- 力量/防御力行
- 分隔线
- 规则叙述换行
- 走纸指令
- ESC/POS 字号和对齐指令

修改格式后，必须重新生成全部数据和索引。

## 常见问题

### JSON 解析失败

脚本会输出来源文件名、行号、列号和错误附近内容。

请检查：

- 文件是否为 JSON Lines，而不是一个大型 JSON 数组。
- 是否存在错误转义。
- 文件是否下载不完整。
- 文件编码是否为 UTF-8 或带 BOM 的 UTF-8。

### 生成的卡牌数量很少

检查：

- 两个来源中的 UUID 是否能对应。
- Scryfall 侧类型栏是否使用 `Creature`。
- CMC 范围是否正确。
- `stats.json` 中的跳过计数。

### 中文打印为 `?`

该字符可能不在 GBK 或打印机内置字库中。

可选处理方式：

- 在预处理阶段替换该字符。
- 使用打印机支持的其他字库模式。
- 不使用内置字库，将文字渲染为位图后打印。

### ESP32 报告打印记录长度无效

不要混用不同生成批次的文件。以下内容必须一起替换：

```text
cards_printer.bin
index_printer/
```

还要确认索引偏移量指向每条记录的 4 字节长度字段。

## 数据与仓库策略

生成后的完整卡牌数据库应继续通过 `.gitignore` 排除在固件仓库之外。

Git 仓库建议仅包含：

- 固件源码
- 生成工具源码
- 格式文档
- 少量自行构造的测试数据（如有）

除非已经完整审查相关许可证、署名义务、第三方使用条款和知识产权问题，否则不应自动提交完整生成数据库。
