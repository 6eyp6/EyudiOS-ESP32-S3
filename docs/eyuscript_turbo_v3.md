# EyuScript Turbo v3 Engine Architecture

[![Lang English](https://img.shields.io/badge/Lang-English-blue.svg)](eyuscript_turbo_v3.md)
[![Lang Türkçe](https://img.shields.io/badge/Lang-T%C3%BCrk%C3%A7e-red.svg)](tr/eyuscript_turbo_v3.md)

> EyudiOS ESP32-S3 Edition v2.0 — High-Performance Script Interpreter & IPC Architecture

`EyuScript Turbo v3` is the next-generation interpreter engine designed for EyudiOS v2.0 kernel to maximize script execution performance, eliminate memory leaks, and get peak efficiency from the ESP32-S3 dual-core FreeRTOS hardware.

---

## ⚡ v3 Performance & Architectural Innovations

1. **Tokenization & AST-like Rapid Indexing:**
   - Once a script is loaded into memory, command lines and label positions (`:label`) are indexed in a single pass. Search overhead for `GOTO` and `GOSUB` is reduced to zero.

2. **PSRAM Zero-Copy Caching & Stack Pooling:**
   - Scripts and Shunting-Yard math calculation stacks (`vmValuePool`, `vmOpPool`) are allocated in OPI PSRAM and interpreted directly from RAM, saving DRAM memory.

3. **Cooperative Multitasking & Drop-Logged IPC:**
   - Background tasks (`bgScriptTask[0..2]`) perform cooperative yielding via `vTaskDelay(1)` to prevent ESP32-S3 Core 0 watchdog resets.
   - IPC messages are transferred over a 16-slot `ipcQueue`. If the queue is full, non-blocking drop-logging prevents system deadlocks.

---

## 📡 IPC (Inter-Process Communication) Diagram

```text
┌────────────────────────────────────────────────────────┐
│  Background Task (Core 0)                              │
│  IPC sensor_temp {read_val}                            │
└───────────────────────────┬────────────────────────────┘
                            │ xQueueSend (Non-blocking Queue)
                            ▼
┌────────────────────────────────────────────────────────┐
│  FreeRTOS Queue (ipcQueue - 16 Slots)                  │
└───────────────────────────┬────────────────────────────┘
                            │ xQueueReceive (UI Loop)
                            ▼
┌────────────────────────────────────────────────────────┐
│  Foreground UI / System Engine (Core 1)                │
│  Updates sensor_temp variable and renders to display   │
└────────────────────────────────────────────────────────┘
```
