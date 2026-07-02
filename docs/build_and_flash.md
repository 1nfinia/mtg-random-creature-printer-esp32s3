# Build and Flash

<p>
  <strong>English</strong> |
  <a href="build_and_flash_zh-CN.md">简体中文</a>
</p>

This guide explains how to build, flash, and monitor the ESP32-S3 firmware.

## Tested environment

- Target: ESP32-S3
- Framework: ESP-IDF v6.0.1
- Build system: CMake + Ninja
- Tested host: Windows 10/11 with PowerShell
- Firmware directory: `firmware/esp32s3_magic_printer`

Other recent ESP-IDF versions may work, but the project was developed and tested with v6.0.1.

## 1. Install ESP-IDF

Install ESP-IDF by following Espressif's official installation guide.

After installation, open an **ESP-IDF Terminal**. A normal PowerShell window may not recognize `idf.py` until the ESP-IDF environment has been loaded.

Verify the environment:

```powershell
idf.py --version
```

### Loading ESP-IDF manually on Windows

```powershell
. "E:\esp\v6.0.1\esp-idf\export.ps1"
```

Replace the path with your actual ESP-IDF installation path. The leading dot and space are required.

## 2. Clone the repository

```powershell
git clone https://github.com/1nfinia/mtg-random-creature-printer-esp32s3.git
cd mtg-random-creature-printer-esp32s3\firmware\esp32s3_magic_printer
```

## 3. Set the target

```powershell
idf.py set-target esp32s3
```

If the environment contains an old `IDF_TARGET` value such as `esp32`, remove it first:

```powershell
Remove-Item Env:IDF_TARGET -ErrorAction SilentlyContinue
idf.py set-target esp32s3
```

## 4. Build the firmware

```powershell
idf.py build
```

A successful build generates firmware images in the local `build/` directory. The directory is intentionally excluded from Git.

## 5. Find the serial port

On Windows, open **Device Manager → Ports (COM & LPT)** and find the ESP32-S3 serial port.

You can also list serial ports in PowerShell:

```powershell
[System.IO.Ports.SerialPort]::GetPortNames()
```

Assume the board appears as `COM3` in the examples below.

## 6. Flash and monitor

```powershell
idf.py -p COM3 flash monitor
```

Replace `COM3` with the actual port.

Exit the serial monitor with:

```text
Ctrl + ]
```

You can also flash and monitor separately:

```powershell
idf.py -p COM3 flash
idf.py -p COM3 monitor
```

## 7. Clean rebuild

When source files, components, or target settings change, perform a clean rebuild:

```powershell
Remove-Item -Recurse -Force .\build -ErrorAction SilentlyContinue
idf.py set-target esp32s3
idf.py build
```

Alternatively:

```powershell
idf.py fullclean
idf.py set-target esp32s3
idf.py build
```

Run `idf.py` from:

```text
firmware/esp32s3_magic_printer/
```

Do not run it from the `main/` subdirectory.

## 8. Prepare the SD card

The firmware expects the generated database files in the SD card root directory:

```text
cards.dat
cards_printer.bin
index/
index_printer/
```

See:

- [`sd_card_format.md`](sd_card_format.md)
- [`../tools/card_data_builder/README.md`](../tools/card_data_builder/README.md)

The firmware mounts the card at `/sdcard`.

## 9. Expected startup log

A normal startup should include messages similar to:

```text
printer uart init done
SD card mounted at /sdcard
FATFS total: ...
FATFS free : ...
CMC  1: json=..., printer=...
Stage 1 printer version ready
```

After rotating the encoder and pressing it, the log should include:

```text
Button pressed, print CMC=...
PRINTER CMC ... payload_len=...
Print OK
```

## Common problems

### `idf.py` is not recognized

Use an ESP-IDF Terminal or load `export.ps1` manually.

### Target mismatch

Example:

```text
Target 'esp32s3' ... is not consistent with target 'esp32' in the environment
```

Fix:

```powershell
Remove-Item Env:IDF_TARGET -ErrorAction SilentlyContinue
Remove-Item -Recurse -Force .\build -ErrorAction SilentlyContinue
Remove-Item -Force .\sdkconfig -ErrorAction SilentlyContinue
idf.py set-target esp32s3
```

### Missing `driver/uart.h`

Make sure `main/CMakeLists.txt` includes:

```cmake
esp_driver_uart
```

in the `REQUIRES` list. The same rule applies to I2C, SPI, GPIO, and SD-SPI drivers.

### Flashing cannot open the COM port

- Close other serial monitors.
- Confirm the selected COM port.
- Reconnect the USB cable.
- Use a USB data cable rather than a charge-only cable.
- Press and hold BOOT if the board cannot enter download mode automatically.

### SD card mount fails

- Insert the card before resetting the board.
- Use FAT32 for initial testing.
- Verify the card data directory structure.
- Confirm that no other component initializes the same SPI bus twice.

### Printer initializes but does not print

- Connect ESP32-S3 `GPIO17` to printer `RXD`.
- Connect both grounds.
- Use a sufficient independent printer power supply.
- Confirm the printer baud rate matches `thermal_printer.h`.
- Do not send UTF-8 Chinese text directly; use the generated GBK printer payload.
