# EyudiOS ESP32-S3 Edition v2.0 — System Architecture & Concurrency Notes

[![Lang English](https://img.shields.io/badge/Lang-English-blue.svg)](architecture.md)
[![Lang Türkçe](https://img.shields.io/badge/Lang-T%C3%BCrk%C3%A7e-red.svg)](tr/architecture.md)

> EyudiOS S3 Edition v2.0 — Kernel, Input Architecture, Memory (PSRAM), Task Management & Hardware Drivers

---

## 🏗️ Task and Dual-Core Allocation Map

EyudiOS v2.0 optimizes the ESP32-S3 dual-core (Xtensa LX7 @ 240MHz) hardware architecture using FreeRTOS tasks:

```text
┌─────────────────────────────────── Core 1 (UI & System) ───────────────────────┐
│                                                                                │
│   loop() — Main UI Loop                                Priority: 1             │
│   ├── processUSBInput()          → USB Keyboard & Mouse events (CH375/Native)   │
│   ├── processBLEInput()          → NimBLE Gamepad & Wireless Keyboard reading   │
│   ├── IPC Message Dispatch       → Process non-blocking Queue (16 slots)        │
│   └── Render (systemMutex protected)                                            │
│       ├── runningScript → ESC overlay and foreground ScriptEngine rendering      │
│       ├── showFileManager  → drawFileManager()                                  │
│       ├── showEyuditor     → drawEyuditor()                                     │
│       ├── showEsdos        → drawEsdos()                                        │
│       └── Desktop/Windows  → AppRegistry + renderDesktop()                      │
│                                                                                │
│   runEyuScriptTask()             Foreground Script    Priority: 1 (Core 1)    │
│   └── Framebuffer: systemMutex protected drawing                                │
│                                                                                │
└────────────────────────────────────────────────────────────────────────────────┘

┌─────────────────────────────────── Core 0 (Background Tasks) ──────────────────┐
│                                                                                │
│   bgScriptTask[0]               BG Script Slot 0      Priority: 0 (Background) │
│   bgScriptTask[1]               BG Script Slot 1      Priority: 0              │
│   bgScriptTask[2]               BG Script Slot 2      Priority: 0              │
│                                                                                │
│   ✓ All BG tasks perform cooperative yield via vTaskDelay(1)                     │
│   ✓ NO direct access to Framebuffer / Display                                    │
│   ✓ SD Card access protected via sdMutex                                         │
│   ✓ Data transferred to UI via FreeRTOS Queue using IPC command                  │
│                                                                                │
└────────────────────────────────────────────────────────────────────────────────┘
```

---

## ⌨️ Multi-Source Input Driver Architecture

EyudiOS v2.0 supports 6 distinct hardware & software input layers:

1. **Native ESP32-S3 USB OTG Host Driver:**
   - Direct USB HID Keyboard (8-byte reports) & USB HID Mouse (delta X/Y, clicks) via ESP32-S3 USB D+/D- pins.
2. **CH375 Hardware USB Coprocessor Driver (UART):**
   - Secondary hardware USB Host interface via UART (GPIO 3 RX / GPIO 46 TX).
3. **NimBLE Bluetooth / BLE Gamepad & Controller Engine:**
   - Wireless Bluetooth Gamepad, Controllers, and Keyboards via NimBLE-Arduino.
4. **Leonardo / PSX Controller Driver:**
   - External Leonardo USB & PSX controller driver integration.
5. **Graphical Virtual Keyboard (VKB):**
   - Interactive on-screen mouse/touch driven keyboard.
6. **GPIO Interrupt (ISR) Bridge:**
   - FreeRTOS `vTaskNotifyGiveFromISR` supported hardware button interrupt handling.

---

## 🧠 Memory & RAM Management (OPI PSRAM Integration)

EyudiOS v2.0 utilizes 8MB / 16MB OPI PSRAM to eliminate RAM constraints:

1. **PSRAM Framebuffer & Double Buffering:**
   - VGA driver (`ESP32-S3-VGA`) allocates the 400x300 display buffer in PSRAM (protected via `systemMutex`).
2. **EyuScript Turbo Stack Pool:**
   - Deep mathematical calculation stacks (Shunting-Yard Stack) run in a dedicated PSRAM pool (`vmValuePool`, `vmOpPool`) instead of DRAM.
3. **SpiRamJsonDocument:**
   - JSON parsing and HTTP payloads use `SpiRamAllocator` directly in PSRAM.

---

## 🔒 Mutex & Synchronization Matrix

| Resource / Structure | Protection | Type | Description |
|:---|:---|:---|:---|
| **Framebuffer / Display** | `systemMutex` | Recursive Mutex | Serializes display rendering between UI loop and foreground scripts. |
| **SD Card (EyuFS)** | `sdMutex` | Binary Semaphore | Protects file access between foreground script, background task, and EsDOS. |
| **USB Host Buffer** | `systemMutex` | Mutex | CH375 and Native USB driver keyboard/mouse buffer protection. |
| **IPC Message Queue** | `ipcQueue` | FreeRTOS Queue (16 slots) | Non-blocking one-way `xQueueSend` communication from background to UI. |

---

## 🧩 Clean Plugin Architecture (`AppDescriptor` & `AppRegistry`)

EyudiOS v2.0 uses a modular `AppDescriptor` structure for application registration without `#ifdef` code pollution:

```cpp
typedef struct {
    const char* id;          // Application ID (e.g. "FileManager")
    const char* title;       // Display title
    const char* symbol;      // 2-character icon symbol
    uint16_t color;          // Icon theme color (RGB565)
    bool enabled;            // Enabled status
    void (*launch)(void);    // Launcher callback function
} AppDescriptor;
```

This clean structure allows external/private modules to be injected into the system via the `registerExternalApps()` weak symbol hook.

---

## ⚖️ License Protection (GPL-3.0 with Linking Exception)

The EyudiOS v2.0 kernel is licensed under **GNU General Public License v3.0 with Additional Linking Exception**.
This exception allows custom proprietary application modules built on top of EyudiOS to be executed without requiring their source code to be disclosed.
