# SD 卡数据格式

<p>
  <a href="sd_card_format.md">English</a> |
  <strong>简体中文</strong>
</p>

本文定义固件使用的 SD 卡目录结构和二进制格式。

## 文件系统

初次测试建议将 SD 卡格式化为 FAT32。

固件挂载点：

```text
/sdcard
```

生成的数据文件必须放在 SD 卡根目录。

## 目录结构

```text
SD 卡根目录
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

`stats.json` 可用于检查生成结果，但当前固件运行时不要求该文件存在。

## 字节序

索引文件和打印二进制文件中的所有整数均使用 **little-endian（小端序）**。

## `cards.dat`

`cards.dat` 是 UTF-8 编码的 JSON Lines 文件。

- 每行保存一张卡牌。
- 每一行都是独立 JSON 对象。
- 规则叙述中的换行以 JSON 转义形式保存。
- 索引偏移量指向对应 JSON 行的第一个字节。

示例：

```json
{"card_id":"931345ae-39ab-5fd2-a6d7-51620e7db176","name":"犄牙兽","type_line":"生物～野兽","text":"当犄牙兽进战场时，你获得5点生命。\n当犄牙兽离开战场时，派出一个3/3绿色野兽衍生生物。","face_index":-1,"cmc":5,"power":"5","toughness":"3","mana_cost":"{4}{G}"}
```

当前保留字段：

| 字段 | 含义 |
|---|---|
| `card_id` | 用于合并两个数据源的卡牌 UUID |
| `name` | 简体中文卡名 |
| `type_line` | 简体中文类型栏 |
| `text` | 简体中文规则叙述 |
| `face_index` | 卡面选择信息 |
| `cmc` | 整数法术力值 |
| `power` | 力量 |
| `toughness` | 防御力 |
| `mana_cost` | 法术力费用字符串 |

当前只打印版本不会在打印时解析该文件，但仍保留它用于调试，以及未来在 OLED 上显示卡名和内容。

## `index/cmc_x.idx`

每个文件保存某个 CMC 分组在 `cards.dat` 中的偏移量。

每个索引项为：

```text
uint64 little-endian offset
```

每项大小：

```text
8 字节
```

因此：

```text
卡牌数量 = 索引文件大小 / 8
```

示例布局：

```text
字节 0..7    → cards.dat 中第 0 张卡的偏移量
字节 8..15   → cards.dat 中第 1 张卡的偏移量
字节 16..23  → cards.dat 中第 2 张卡的偏移量
...
```

偏移量指向 JSON 行的第一个字节。

## `cards_printer.bin`

该文件保存可变长度打印记录。

每条记录格式：

```text
[uint32 little-endian payload_length][payload 字节]
```

payload 是为目标打印机预处理的内容：

- GBK 编码中文
- CRLF 换行
- 可选 ESC/POS 指令
- 不要求以 `\0` 结尾

记录布局：

```text
offset + 0 ... 3                  uint32 数据长度
offset + 4 ... 4+length-1         打印数据
```

ESP32 会先读取长度、分配缓冲区、读取 payload，再通过 `uart_write_bytes()` 发送。

当前固件拒绝超过 32 KiB 的单条打印记录。

## `index_printer/cmc_x.idx`

每个文件保存某个 CMC 分组在 `cards_printer.bin` 中的偏移量。

每个索引项为：

```text
uint64 little-endian offset
```

偏移量指向 **4 字节长度字段的起始位置**，而不是直接指向 payload。

读取流程：

```text
1. 从 index_printer/cmc_x.idx 读取 8 字节偏移量
2. 将 cards_printer.bin 定位到该 offset
3. 读取 4 字节 little-endian payload 长度
4. 读取 payload_length 字节
5. 将数据发送给打印机
```

## 随机读取流程

用户选择 CMC 后：

```text
count = 索引文件大小 / 8
pick = 随机数 % count
将索引文件定位到 pick * 8
读取 uint64 offset
将数据文件定位到 offset
读取记录
```

当前固件使用 `esp_random()` 生成 `pick`。

## JSON 索引与打印索引的对应关系

数据生成工具在同一次合并过程中生成 JSON 记录和打印记录。

当两个索引目录中的记录数量和顺序一致时，可以使用同一个 `pick` 同时读取：

- 用于显示的 JSON 记录
- 对应的打印 payload

当前第一阶段固件只读取打印记录。后续版本可以共享同一个随机索引，从而保证 OLED 显示与打印内容为同一张牌。

## 校验规则

复制到 SD 卡前建议检查：

- 每个 `.idx` 文件大小均为 8 的整数倍。
- 所有偏移量均未超出对应数据文件范围。
- 每条打印记录长度未超出 `cards_printer.bin`。
- 打印 payload 长度不为 0。
- 打印 payload 长度不超过固件限制。
- 如果要配对读取，同一 CMC 的 JSON 和打印索引数量必须一致。

## Python 检查示例

读取打印索引的第一项：

```python
import struct

with open("index_printer/cmc_5.idx", "rb") as f:
    offset = struct.unpack("<Q", f.read(8))[0]

print(offset)
```

读取对应打印记录：

```python
import struct

with open("cards_printer.bin", "rb") as f:
    f.seek(offset)
    length = struct.unpack("<I", f.read(4))[0]
    payload = f.read(length)

print(length)
print(payload[:64].hex(" "))
```

统计索引中的卡牌数量：

```python
from pathlib import Path

size = Path("index_printer/cmc_5.idx").stat().st_size
assert size % 8 == 0
print(size // 8)
```

## 更新数据库

更新时应整体替换以下文件：

```text
cards.dat
cards_printer.bin
index/
index_printer/
stats.json
```

不要混用不同生成批次的索引文件和数据文件，否则偏移量将失效。
