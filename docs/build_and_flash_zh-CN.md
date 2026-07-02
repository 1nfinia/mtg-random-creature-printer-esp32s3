# 编译与烧录

<p>
  <a href="build_and_flash.md">English</a> |
  <strong>简体中文</strong>
</p>

本文说明如何编译、烧录并监视 ESP32-S3 固件。

## 已测试环境

- 目标芯片：ESP32-S3
- 开发框架：ESP-IDF v6.0.1
- 构建系统：CMake + Ninja
- 已测试主机：Windows 10/11 + PowerShell
- 固件目录：`firmware/esp32s3_magic_printer`

其他较新的 ESP-IDF 版本可能也能运行，但本项目是在 v6.0.1 环境中开发和验证的。

## 1. 安装 ESP-IDF

按照 Espressif 官方说明安装 ESP-IDF。

安装完成后，请打开 **ESP-IDF Terminal**。普通 PowerShell 默认不一定能识别 `idf.py`，因为尚未加载 ESP-IDF 环境。

检查环境：

```powershell
idf.py --version
```

### 在普通 PowerShell 中手动加载环境

```powershell
. "E:\esp\v6.0.1\esp-idf\export.ps1"
```

请将路径替换为你的 ESP-IDF 实际安装位置。命令最前面的点和空格不能省略。

## 2. 克隆仓库

```powershell
git clone https://github.com/1nfinia/mtg-random-creature-printer-esp32s3.git
cd mtg-random-creature-printer-esp32s3\firmware\esp32s3_magic_printer
```

## 3. 设置目标芯片

```powershell
idf.py set-target esp32s3
```

如果环境变量中残留了 `esp32` 等旧目标，先清除：

```powershell
Remove-Item Env:IDF_TARGET -ErrorAction SilentlyContinue
idf.py set-target esp32s3
```

## 4. 编译固件

```powershell
idf.py build
```

编译成功后，固件文件会生成在本地 `build/` 目录。该目录属于可重新生成的构建输出，因此已通过 `.gitignore` 排除。

## 5. 查找串口号

在 Windows 中打开：

```text
设备管理器 → 端口（COM 和 LPT）
```

也可以在 PowerShell 中执行：

```powershell
[System.IO.Ports.SerialPort]::GetPortNames()
```

下文以 `COM3` 为例。

## 6. 烧录并打开串口监视器

```powershell
idf.py -p COM3 flash monitor
```

请将 `COM3` 替换为实际端口。

退出串口监视器：

```text
Ctrl + ]
```

也可以分开执行：

```powershell
idf.py -p COM3 flash
idf.py -p COM3 monitor
```

## 7. 完全清理后重新编译

```powershell
Remove-Item -Recurse -Force .\build -ErrorAction SilentlyContinue
idf.py set-target esp32s3
idf.py build
```

也可以使用：

```powershell
idf.py fullclean
idf.py set-target esp32s3
idf.py build
```

应在以下目录执行 `idf.py`：

```text
firmware/esp32s3_magic_printer/
```

不要在 `main/` 子目录中执行。

## 8. 准备 SD 卡

固件要求 SD 卡根目录中存在：

```text
cards.dat
cards_printer.bin
index/
index_printer/
```

详细说明参见：

- [`sd_card_format_zh-CN.md`](sd_card_format_zh-CN.md)
- [`../tools/card_data_builder/README_zh-CN.md`](../tools/card_data_builder/README_zh-CN.md)

当前固件将 SD 卡挂载到 `/sdcard`。

## 9. 正常启动日志

正常启动时，串口应出现类似内容：

```text
printer uart init done
SD card mounted at /sdcard
FATFS total: ...
FATFS free : ...
CMC  1: json=..., printer=...
Stage 1 printer version ready
```

旋转编码器并按下后，应看到：

```text
Button pressed, print CMC=...
PRINTER CMC ... payload_len=...
Print OK
```

## 常见问题

### PowerShell 无法识别 `idf.py`

请使用 ESP-IDF Terminal，或手动执行 `export.ps1`。

### 目标芯片冲突

例如：

```text
Target 'esp32s3' ... is not consistent with target 'esp32' in the environment
```

处理方法：

```powershell
Remove-Item Env:IDF_TARGET -ErrorAction SilentlyContinue
Remove-Item -Recurse -Force .\build -ErrorAction SilentlyContinue
Remove-Item -Force .\sdkconfig -ErrorAction SilentlyContinue
idf.py set-target esp32s3
```

### 找不到 `driver/uart.h`

确认 `main/CMakeLists.txt` 的 `REQUIRES` 中包含：

```cmake
esp_driver_uart
```

I2C、SPI、GPIO 和 SD-SPI 等驱动也需要声明对应组件依赖。

### 无法打开 COM 端口

- 关闭其他串口监视程序。
- 确认选择了正确端口。
- 重新连接 USB。
- 使用支持数据传输的 USB 线。
- 如果自动下载失败，可按住 BOOT 后重新烧录。

### SD 卡挂载失败

- 在复位前插入 SD 卡。
- 初次测试建议使用 FAT32。
- 检查 SD 卡数据目录结构。
- 确认没有其他模块重复初始化同一条 SPI 总线。

### 打印机初始化成功但不打印

- 将 ESP32-S3 `GPIO17` 接到打印机 `RXD`。
- ESP32-S3 与打印机必须共地。
- 打印机应使用足够功率的独立电源。
- 确认打印机波特率与 `thermal_printer.h` 一致。
- 不要直接发送 UTF-8 中文字符串，应发送脚本生成的 GBK 打印数据。
