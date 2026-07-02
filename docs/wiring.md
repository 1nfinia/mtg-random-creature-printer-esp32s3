# Wiring Guide

<p>
  <strong>English</strong> |
  <a href="wiring_zh-CN.md">简体中文</a>
</p>

This document describes the wiring used by the tested prototype.

## Important safety notes

- Disconnect power before changing wiring.
- Use a common ground between the ESP32-S3, OLED, encoder, SD card, and printer interface.
- Power the thermal printer from an independent supply suitable for its peak current.
- Do not power the thermal printer from the ESP32-S3 3.3 V pin.
- Do not connect a 5 V printer TX signal directly to an ESP32-S3 GPIO.
- Verify the printer connector pin order from its label or manual before connecting it.

## Complete pin table

| Module | Module pin | ESP32-S3 pin | Direction | Notes |
|---|---|---:|---|---|
| SSD1306 OLED | VCC | 3V3 | Power | Use a 3.3 V-compatible module |
| SSD1306 OLED | GND | GND | Power | Common ground |
| SSD1306 OLED | SDA | GPIO4 | Bidirectional | I2C data |
| SSD1306 OLED | SCL | GPIO5 | Output | I2C clock |
| EC11 | A | GPIO6 | Input | Internal pull-up enabled |
| EC11 | B | GPIO7 | Input | Internal pull-up enabled |
| EC11 | SW | GPIO15 | Input | Active low |
| EC11 | C/GND | GND | Power | Encoder common |
| EC11 module | VCC | 3V3 | Power | Only if using a breakout board |
| TF/microSD | SCLK | GPIO12 | Output | Board SPI clock |
| TF/microSD | MOSI | GPIO11 | Output | ESP32 to card |
| TF/microSD | MISO | GPIO13 | Input | Card to ESP32 |
| TF/microSD | CS | GPIO2 | Output | Active-low chip select |
| Thermal printer | RXD | GPIO17 | ESP32 output | Required for printing |
| Thermal printer | TXD | GPIO18 | ESP32 input | Optional status input |
| Thermal printer | GND | GND | Power/reference | Required common ground |
| Thermal printer | VCC | External supply | Power | Follow printer specification |
| Thermal printer | DTR/Busy | Not connected | Input | Not used by current firmware |

## Simplified connection diagram

```text
ESP32-S3                         SSD1306 OLED
---------                        ------------
3V3   -------------------------- VCC
GND   -------------------------- GND
GPIO4 -------------------------- SDA
GPIO5 -------------------------- SCL

ESP32-S3                         EC11
---------                        ----
GPIO6 -------------------------- A
GPIO7 -------------------------- B
GPIO15 ------------------------- SW
GND   -------------------------- C / GND
3V3   -------------------------- VCC (breakout modules only)

ESP32-S3                         TF / microSD
---------                        ------------
GPIO12 ------------------------- SCLK
GPIO11 ------------------------- MOSI
GPIO13 ------------------------- MISO
GPIO2  ------------------------- CS
3V3    ------------------------- VCC
GND    ------------------------- GND

ESP32-S3                         TTL thermal printer
---------                        -------------------
GPIO17 ------------------------> RXD
GPIO18 <------------------------ TXD (optional, only if voltage is safe)
GND    ------------------------- GND
                                 VCC <--- independent printer supply
```

## OLED

The OLED uses I2C:

```text
SDA = GPIO4
SCL = GPIO5
Address = 0x3C
Clock = 400 kHz
```

Some SSD1306 modules use address `0x3D`. Change the firmware address if the display is not detected.

The external OLED intentionally uses GPIO4 and GPIO5 rather than the development board's onboard I2C bus.

## EC11 rotary encoder

The tested configuration is:

```text
A  = GPIO6
B  = GPIO7
SW = GPIO15
C  = GND
```

The inputs use pull-ups. The switch is active low.

If rotation direction is reversed:

- Swap A and B, or
- Reverse the quadrature direction in software.

The current software decoder treats approximately four raw transitions as one detent.

## SD card

The tested development board exposes its TF card through SPI2:

```text
Host = SPI2_HOST
SCLK = GPIO12
MOSI = GPIO11
MISO = GPIO13
CS   = GPIO2
```

Do not initialize the same SPI bus twice. The SD mounting code initializes the bus and attaches the SD-SPI device.

## Thermal printer

Minimum one-way wiring:

```text
ESP32-S3 GPIO17 → printer RXD
ESP32-S3 GND    → printer GND
```

Printer TXD is not required for printing.

The tested serial settings are:

```text
UART1
9600 baud
8 data bits
No parity
1 stop bit
No hardware flow control
```

### Checking printer logic voltage

Measure:

```text
printer TXD relative to printer GND
```

A UART TX line is normally high while idle.

- About 3.3 V: generally compatible with ESP32-S3 RX.
- About 5 V: use a level shifter or divider before connecting to ESP32-S3 RX.
- Only one-way printing is needed: leave printer TXD disconnected.

Measuring printer RXD while it is floating does not reliably identify its logic level.

## Power supply guidance

Thermal printers can draw much more current while heating than while idle.

- Use the voltage specified by the printer manufacturer.
- Choose a supply with adequate peak-current capability.
- Keep power wires short and sufficiently thick.
- Keep printer power separate from the ESP32-S3 3.3 V rail.
- Connect grounds together at a low-impedance point.

Symptoms of insufficient printer power include:

- Printer resets while printing
- Faint output
- Partial lines
- Paper feed starts and stops
- ESP32-S3 resets because of ground or supply disturbance

## Pin conflicts

The current assignment avoids overlap between the main peripherals:

```text
OLED:    GPIO4, GPIO5
EC11:    GPIO6, GPIO7, GPIO15
SD card: GPIO11, GPIO12, GPIO13, GPIO2
Printer: GPIO17, GPIO18
```

Before adding new peripherals, also consider:

- GPIO19 and GPIO20 may be used by native USB.
- GPIO0 is commonly used for boot mode.
- Some pins have board-specific functions or boot strapping behavior.
