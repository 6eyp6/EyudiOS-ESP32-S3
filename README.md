<div align="center">

# 🚀 EyudiOS ESP32-S3 Edition v2.0

### Advanced Dual-Core Operating System & Interpreted Scripting Runtime for ESP32-S3

[![Lang English](https://img.shields.io/badge/Lang-English-blue.svg)](README.md)
[![Lang Türkçe](https://img.shields.io/badge/Lang-T%C3%BCrk%C3%A7e-red.svg)](README_TR.md)
[![EyudiOS Flasher](https://img.shields.io/badge/Web%20Flasher-EyudiOS%20Flasher-purple.svg)](https://github.com/6eyp6/EyudiOS-Flasher)
[![License: GPL v3 with Linking Exception](https://img.shields.io/badge/License-GPLv3%20with%20Linking%20Exception-green.svg)](LICENSE)
[![PlatformIO](https://img.shields.io/badge/PlatformIO-ESP32--S3-orange.svg)](https://platformio.org/)
[![Framework](https://img.shields.io/badge/Framework-Arduino%20%2F%20FreeRTOS-teal.svg)](https://www.espressif.com/)

---

</div>

**EyudiOS ESP32-S3 Edition v2.0** is an open-source advanced operating system kernel created by **Eyüp SAĞLAM (Eyudio)**. Designed to run on the ESP32-S3 microcontroller, it features dual-core FreeRTOS task scheduling, direct 400x300 VGA display output, multi-layer input drivers, the `EsDOS` command shell, the `EyuScript Turbo v3` scripting engine, and a modular application registry (`AppRegistry`).

> ⚡ **Fast Installation without Compiling:** You can flash pre-compiled EyudiOS firmware binaries directly from your web browser using the official [EyudiOS Flasher](https://github.com/6eyp6/EyudiOS-Flasher) tool!

---

## 🌟 Key Features (v2.0)

- 🖥️ **VGA & Display Engine:** Bitluni VGA integration providing 400x300 double-buffered graphics interface.
- ⌨️ **Multi-Layer Input Architecture:**
  - **Native USB OTG Host:** Direct USB HID Keyboard & Mouse via USB D+/D- pins.
  - **CH375 / Coprocessor USB Host (UART):** Hardware Keyboard, Mouse, PS2/3/4 & Xbox controller support via Arduino coprocessor (GPIO 3/46).
  - **NimBLE Bluetooth (BLE):** Wireless BLE Gamepads and keyboard input.
  - **Graphical Virtual Keyboard (VKB):** On-screen mouse/touch driven interactive keyboard.
  - **GPIO ISR Bridge:** FreeRTOS ISR hardware button handling.
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

## ⚡ Quick Installation ([EyudiOS Flasher](https://github.com/6eyp6/EyudiOS-Flasher))

If you want to install EyudiOS on your ESP32-S3 board without setting up a PlatformIO build environment:
1. Visit the [EyudiOS Flasher Repository](https://github.com/6eyp6/EyudiOS-Flasher).
2. Connect your ESP32-S3 board to your PC via USB.
3. Launch EyudiOS Flasher to automatically flash pre-compiled firmware binaries and partition schemes in seconds!

---

## 🔌 Hardware Requirements & Pinout

### Hardware:
* **MCU:** ESP32-S3 (YD-ESP32-S3 / ESP32-S3-DevKitC-1)
* **RAM/Flash:** 8MB / 16MB OPI PSRAM + 16MB Flash
* **Input Devices:**
  - USB HID Keyboard & Mouse (Native USB D+/D- or CH375 / USB Host Shield 2.0)
  - PlayStation 2 / 3 / 4, Xbox Controllers & Bluetooth Dongles (Leonardo / Uno coprocessor)
  - Bluetooth BLE Wireless Gamepad & Keyboard
* **Storage:** MicroSD Card Module (SPI Mode) & CH375B USB Disk (UART)

### 🔌 EyudiOS S3 Complete Pinout Table

| Hardware / Unit | Pin Name / Function | ESP32-S3 GPIO Number | Description |
| :--- | :--- | :--- | :--- |
| **VGA Monitor Output** | Red | **GPIO 4** | VGA Analog Red Signal |
| | Green | **GPIO 5** | VGA Analog Green Signal |
| | Blue | **GPIO 6** | VGA Analog Blue Signal |
| | H-Sync | **GPIO 7** | Horizontal Synchronization Signal |
| | V-Sync | **GPIO 15** | Vertical Synchronization Signal |
| **Micro SD Card (SPI)** | CS (Chip Select) | **GPIO 10** | SD Card Select Signal |
| | MOSI (Data In) | **GPIO 11** | SPI Master Out Slave In |
| | SCK (Clock) | **GPIO 12** | SPI Clock Signal |
| | MISO (Data Out) | **GPIO 13** | SPI Master In Slave Out |
| **CH375B Storage (UART)** | CH375B TX $\rightarrow$ ESP RX | **GPIO 1** | USB Disk Read Signal |
| | CH375B RX $\leftarrow$ ESP TX | **GPIO 2** | USB Disk Write Signal |
| **USB Host Keyboard/Mouse (External)** | Serial1 RX (ESP RX) | **GPIO 3** | External USB Host MCU (TX) Input (Leonardo RX:8, TX:7) |
| | Serial1 TX (ESP TX) | **GPIO 46** | External USB Host MCU (RX) Output |
| **USB Host Keyboard/Mouse (Native)** | USB D- (Data -) | **GPIO 19** | ESP32-S3 Native USB Host Signal |
| | USB D+ (Data +) | **GPIO 20** | ESP32-S3 Native USB Host Signal |
| **Power Management** | VBUS Enable | **GPIO 21** | 5V Power Switch for USB Port |
| **Status LED** | NeoPixel LED | **GPIO 48** | WS2812 Addressable Status LED |
| **Audio Unit** | PDM Audio / Buzzer | **GPIO 18** | Audio Output / Buzzer Signal |

---

## 🛠️ Building & Flashing (PlatformIO)

1. Install Visual Studio Code and the [PlatformIO IDE](https://platformio.org/) extension.
2. Clone or download the repository:
   ```bash
   git clone https://github.com/6eyp6/EyudiOS-ESP32-S3.git
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
