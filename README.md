<div align="center">

# 🚀 EyudiOS ESP32-S3 Edition v2.0

### Advanced Dual-Core Operating System & Interpreted Scripting Runtime for ESP32-S3

[![Lang English](https://img.shields.io/badge/Lang-English-blue.svg)](README.md)
[![Lang Türkçe](https://img.shields.io/badge/Lang-T%C3%BCrk%C3%A7e-red.svg)](README_TR.md)
[![License: GPL v3 with Linking Exception](https://img.shields.io/badge/License-GPLv3%20with%20Linking%20Exception-green.svg)](LICENSE)
[![PlatformIO](https://img.shields.io/badge/PlatformIO-ESP32--S3-orange.svg)](https://platformio.org/)
[![Framework](https://img.shields.io/badge/Framework-Arduino%20%2F%20FreeRTOS-teal.svg)](https://www.espressif.com/)

---

</div>

**EyudiOS ESP32-S3 Edition v2.0** is a high-performance open-source operating system kernel and runtime environment created by **Eyüp SAĞLAM (Eyudio)**. It features dual-core FreeRTOS task scheduling, direct 400x300 VGA display output, multi-source input drivers (Native USB OTG Host, CH375 UART USB Host, NimBLE BLE Gamepad, Virtual Keyboard), the `EsDOS` command shell, the `EyuScript Turbo v3` scripting engine, and a clean modular application registry (`AppRegistry`).

---

## 🌟 Key Features (v2.0)

- 🖥️ **VGA & Display Engine:** Bitluni VGA integration providing 400x300 double-buffered graphics interface.
- ⌨️ **Multi-Source Input Architecture:**
  - **Native ESP32-S3 USB OTG Host:** Direct USB HID Keyboard & Mouse support.
  - **CH375 Hardware USB Co-processor:** Secondary UART USB Host interface (GPIO 3 RX / GPIO 46 TX).
  - **NimBLE Bluetooth/BLE Engine:** Wireless Bluetooth Gamepad, Keyboard, and Controller driver support.
  - **PSX & Gamepad Drivers:** External Leonardo / PSX Controller interface.
  - **Graphical Virtual Keyboard (VKB):** On-screen mouse/touch driven interactive keyboard.
  - **GPIO ISR Interrupt Bridge:** Hardware interrupt button handling.
- ⚡ **Dual-Core Concurrency (FreeRTOS):**
  - **Core 1:** Foreground UI, windowing system, desktop render loop, and foreground script execution.
  - **Core 0:** Background script tasks (`bgScriptTask[0..2]`), FreeRTOS IPC message queues, and non-blocking I/O.
- 🧩 **Clean Plugin Architecture (`AppRegistry`):**
  - System apps (`FileManager`, `Eyuditor`, `EsDOS`, `Doom`, `Settings`, etc.) register via a unified `AppDescriptor` structure without `#ifdef` code pollution.
- 🐚 **EsDOS Terminal Shell:** UNIX/DOS-like command-line interface (`ls`, `cd`, `cat`, `run`, `runbg`, `ps`, `killbg`, `wifi`, `httpget`, `temp`, etc.).
- 📜 **EyuScript Turbo v3 Engine:**
  - Variables, loops (`FOR/NEXT`, `WHILE/WEND`), branching (`GOTO`, `GOSUB`), drawing primitives (`RECT`, `CIRCLE`, `LINE`, `PRINT`).
  - PSRAM-backed Shunting-Yard math engine (`SQRT`, `SIN`, `COS`, `TAN`, `ABS`, `POW`).
  - FreeRTOS IPC queue for non-blocking background-to-foreground data transfer.
- 🎮 **Built-in Apps & Ports:**
  - `Doom Engine` (Integrated GPL port).
  - `Eyuditor` Graphical text editor.
  - `File Manager` SD card file explorer.
  - `Audio Engine` I2S-based MP3/WAV playback support.

---

## 🔌 Hardware Requirements & Pinout

### Hardware:
* **MCU:** ESP32-S3 (YD-ESP32-S3 / ESP32-S3-DevKitC-1)
* **RAM/Flash:** 8MB / 16MB OPI PSRAM + 16MB Flash
* **Input Devices:**
  - USB HID Keyboard & Mouse (Native USB D+/D- or CH375 Module)
  - Bluetooth BLE Gamepad / Wireless Controller
  - PSX Controller (Leonardo Driver)
* **Storage:** MicroSD Card Module (SPI Mode)

### Pin Mapping (`SystemConfig.h`):

| Component | Signal | ESP32-S3 Pin |
|:---|:---|:---|
| **VGA Output** | RED | GPIO 4 |
| | GREEN | GPIO 5 |
| | BLUE | GPIO 6 |
| | HSYNC | GPIO 7 |
| | VSYNC | GPIO 15 |
| **SD Card (SPI)** | CS | GPIO 10 |
| | SCK | GPIO 12 |
| | MISO | GPIO 13 |
| | MOSI | GPIO 11 |
| **CH375 USB Host** | RX | GPIO 3 |
| | TX | GPIO 46 |
| **Peripherals** | VBUS Enable | GPIO 21 |
| | NeoPixel LED | GPIO 48 |

---

## 🛠️ Building & Flashing (PlatformIO)

1. Install Visual Studio Code and the [PlatformIO IDE](https://platformio.org/) extension.
2. Clone or download the repository:
   ```bash
   git clone https://github.com/6eyp6/EyudiOS-ESP32S3.git
   ```
3. Open the project folder in VS Code.
4. Verify `platformio.ini` settings for your ESP32-S3 board (16MB Flash + OPI PSRAM).
5. Click **Build & Upload** to flash your microcontroller.

---

## 📚 Documentation (`docs/`)

Language Selection: 🇬🇧 **English** | 🇹🇷 [Türkçe](README_TR.md)

- 📖 [docs/architecture.md](docs/architecture.md) ([TR](docs/tr/architecture.md)) — Core Architecture, Input Drivers & Task Map.
- 🐚 [docs/esdos_reference.md](docs/esdos_reference.md) ([TR](docs/tr/esdos_reference.md)) — EsDOS Command Reference.
- 📜 [docs/eyuscript_reference.md](docs/eyuscript_reference.md) ([TR](docs/tr/eyuscript_reference.md)) — EyuScript Language Reference.
- ⚡ [docs/eyuscript_turbo_v3.md](docs/eyuscript_turbo_v3.md) ([TR](docs/tr/eyuscript_turbo_v3.md)) — EyuScript Turbo v3 & IPC Engine.

---

## 📄 License & Copyright

EyudiOS ESP32-S3 Edition v2.0 is Copyright (C) 2026 **Eyüp SAĞLAM (Eyudio)** <https://github.com/6eyp6>.
Licensed under the **GNU General Public License v3.0 with Additional Linking Exception** (GPL-3.0 with Linking Exception). See [LICENSE](LICENSE) for details.
