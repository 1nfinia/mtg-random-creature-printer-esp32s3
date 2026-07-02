<h1 align="center">ESP32-S3 万智牌随机生物热敏打印机</h1>

<p align="center">
根据法术力值随机抽取一张万智牌生物牌，并通过 TTL 热敏打印机输出中文卡牌信息。
</p>

<p align="center">
  <strong>简体中文</strong> |
  <a href="README.md">English</a>
</p>

---

## 项目简介

这是一个面向万智牌爱好者的 ESP32-S3 随机生物牌热敏打印机项目。

用户旋转 EC11 编码器选择法术力值（CMC），再按下编码器。ESP32-S3 会从 SD 卡中读取预处理后的随机卡牌记录，并将 GBK + ESC/POS 打印数据通过 TTL 串口发送给热敏打印机。

当前原型已经完成并验证以下功能：

- 使用 EC11 编码器选择 CMC
- 使用 SSD1306 OLED 显示运行状态
- 通过 SPI 读取 SD 卡
- 根据 CMC 索引随机读取卡牌
- 通过 TTL UART 热敏打印机正常打印中文卡牌信息

> 本仓库提供固件源码和数据生成工具，不直接提供完整的万智牌卡牌数据库。

## 功能特性

- 支持选择 CMC 1～15
- OLED 显示当前 CMC 和可打印卡牌数量
- 从对应 CMC 分组中随机抽取一张生物牌
- 直接从 SD 卡读取卡牌数据
- 打印预处理后的 GBK + ESC/POS 数据
- JSON 显示数据与打印机数据使用独立索引文件
- SD 卡准备完成后，设备可以脱离电脑独立运行

## 硬件组成

| 模块 | 说明 |
|---|---|
| 主控制器 | ESP32-S3 开发板 |
| 显示器 | 0.96 英寸 SSD1306 I2C OLED |
| 输入设备 | 带按键的 EC11 旋转编码器 |
| 存储设备 | SPI TF/microSD 卡 |
| 打印机 | 支持 ESC/POS 的 TTL 串口热敏打印机 |
| 打印机电源 | 满足打印机功率要求的独立电源 |

## 引脚分配

| 模块 | 信号 | ESP32-S3 GPIO |
|---|---|---:|
| OLED | SDA | GPIO4 |
| OLED | SCL | GPIO5 |
| EC11 | A | GPIO6 |
| EC11 | B | GPIO7 |
| EC11 | SW | GPIO15 |
| SD 卡 | SCLK | GPIO12 |
| SD 卡 | MOSI | GPIO11 |
| SD 卡 | MISO | GPIO13 |
| SD 卡 | CS | GPIO2 |
| 热敏打印机 | ESP32 TX → 打印机 RXD | GPIO17 |
| 热敏打印机 | 打印机 TXD → ESP32 RX | GPIO18，可选 |
| 所有模块 | GND | 共地 |

### 打印机接线注意事项

- 打印机应使用独立电源供电。
- 打印机 GND 必须与 ESP32-S3 GND 相连。
- 只进行单向打印时，只需要连接 `GPIO17 → 打印机 RXD` 和公共地。
- 如果打印机 TXD 输出为 5 V，不要直接连接 ESP32-S3 的 RX 引脚。
- 当前测试使用的串口参数为 `9600 波特率、8 数据位、无校验、1 停止位`。

## 仓库目录结构

```text
mtg-random-creature-printer-esp32s3/
├── firmware/
│   └── esp32s3_magic_printer/
│       ├── CMakeLists.txt
│       ├── sdkconfig.defaults
│       └── main/
│           ├── CMakeLists.txt
│           ├── main.c
│           ├── encoder.c
│           ├── encoder.h
│           ├── ssd1306.c
│           ├── ssd1306.h
│           ├── bsp_oled_codetab.h
│           ├── sd_card_reader.c
│           ├── sd_card_reader.h
│           ├── card_db.c
│           ├── card_db.h
│           ├── thermal_printer.c
│           └── thermal_printer.h
├── tools/
│   └── card_data_builder/
│       ├── README.md
│       └── card_data_builder.py
├── docs/
│   ├── wiring.md
│   ├── sd_card_format.md
│   ├── build_and_flash.md
│   └── images/
├── .gitignore
├── LICENSE
├── NOTICE.md
├── README.md
└── README_zh-CN.md
```

## SD 卡目录结构

将生成的数据文件放在 SD 卡根目录：

```text
SD 卡根目录
├── cards.dat
├── cards_printer.bin
├── index/
│   ├── cmc_1.idx
│   ├── cmc_2.idx
│   └── ...
└── index_printer/
    ├── cmc_1.idx
    ├── cmc_2.idx
    └── ...
```

| 文件 | 用途 |
|---|---|
| `cards.dat` | UTF-8 JSON Lines 数据，供 ESP32 解析以及后续 OLED 卡牌预览 |
| `cards_printer.bin` | 可直接发送给热敏打印机的 GBK + ESC/POS 记录 |
| `index/cmc_x.idx` | 对应 CMC 的 `cards.dat` 文件偏移索引 |
| `index_printer/cmc_x.idx` | 对应 CMC 的 `cards_printer.bin` 文件偏移索引 |

每个索引项使用 8 字节 little-endian 偏移量。

打印记录格式为：

```text
[4 字节 little-endian 数据长度][GBK + ESC/POS 数据]
```

详细说明见 [`docs/sd_card_format.md`](docs/sd_card_format.md)。

## 软件环境

- ESP-IDF v6.0.1
- 编译目标：ESP32-S3
- Git
- Python 3，用于运行卡牌数据生成脚本

## 编译与烧录

打开 ESP-IDF 终端并执行：

```powershell
cd firmware/esp32s3_magic_printer
idf.py set-target esp32s3
idf.py build
idf.py -p COM3 flash monitor
```

请将 `COM3` 替换为开发板实际使用的串口号。

需要完全清理后重新编译时：

```powershell
Remove-Item -Recurse -Force .\build -ErrorAction SilentlyContinue
idf.py set-target esp32s3
idf.py build
```

## 生成卡牌数据

本仓库不直接分发完整的卡牌数据库。

请使用：

```text
tools/card_data_builder/card_data_builder.py
```

生成以下文件：

- `cards.dat`
- `cards_printer.bin`
- `index/cmc_x.idx`
- `index_printer/cmc_x.idx`

生成完成后，按照前文目录结构将文件复制到 SD 卡。

脚本使用方法见 [`tools/card_data_builder/README.md`](tools/card_data_builder/README.md)。

## 工作流程

```text
旋转 EC11
    ↓
选择 CMC
    ↓
OLED 显示 CMC 和可打印卡牌数量
    ↓
按下 EC11
    ↓
从 index_printer/cmc_x.idx 随机读取一个偏移量
    ↓
定位到 cards_printer.bin 中对应记录
    ↓
读取带长度字段的 GBK + ESC/POS 数据
    ↓
通过 UART 发送打印数据
    ↓
热敏打印机输出卡牌信息
```

## 添加项目图片

将实物照片放入 `docs/images/` 后，可以在 README 中使用 Markdown 插入：

```markdown
![打印效果](docs/images/printed_result.jpg)
```

需要控制图片宽度时，可以使用 HTML：

```html
<p align="center">
  <img src="docs/images/printed_result.jpg" width="600" alt="打印效果">
</p>
```

## 常见问题

### 打印机没有启动打印

- 确认 ESP32 `GPIO17` 连接到打印机 `RXD`。
- 确认 ESP32 与打印机已经共地。
- 确认打印机串口波特率。
- 确认打印机供电能力充足。
- 在整合主工程前，先使用独立 UART 打印测试工程验证打印机。

### SD 卡挂载失败

- 确认 SD 卡已经正确插入。
- 初次测试建议使用 FAT32 格式。
- 确认 SPI 和 CS 引脚配置。
- 确认数据文件位于 SD 卡根目录。

### OLED 正常但卡牌数量为 0

- 检查对应的 `cmc_x.idx` 文件是否存在。
- 检查索引文件大小是否为 8 的整数倍。
- 检查 `index_printer/` 是否已经复制到 SD 卡。

## 后续计划

- 解析 `cards.dat` 并在 OLED 上显示卡名
- 使用同一个随机索引同时读取 JSON 和打印数据
- 增加打印机 Busy 或 DTR 流控
- 支持在配置文件中修改打印机波特率
- 添加外壳和 PCB 设计文件
- 适配更多 OLED 和热敏打印机型号

## 许可证

本仓库中原创源码和文档按照 [`LICENSE`](LICENSE) 中的许可证发布。

第三方库、卡牌数据、商标、名称、规则文本、符号及其他知识产权仍归各自权利人所有，并受其相应许可证和使用条款约束。

## 免责声明

本项目是非官方、非商业的粉丝自制项目。

Magic: The Gathering 及相关名称、卡牌文字、符号和知识产权归 Wizards of the Coast 所有。本项目与 Wizards of the Coast 无隶属、赞助、认可或特别批准关系。

用户应自行从合法来源获取并处理卡牌数据，并遵守相关服务条款和法律要求。
