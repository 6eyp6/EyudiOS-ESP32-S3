# EyuScript Turbo v3 Motoru Mimarisi

[![Lang Türkçe](https://img.shields.io/badge/Lang-T%C3%BCrk%C3%A7e-red.svg)](eyuscript_turbo_v3.md)
[![Lang English](https://img.shields.io/badge/Lang-English-blue.svg)](../eyuscript_turbo_v3.md)

> EyudiOS ESP32-S3 Edition v2.0 — Yüksek Başarımlı Betik Yorumlayıcısı & IPC

`EyuScript Turbo v3`, EyudiOS v2.0 çekirdeğinde betik yürütme performansını artırmak, bellek sızıntılarını önlemek ve FreeRTOS çift çekirdek mimarisinden en yüksek verimi almak için tasarlanmış yeni nesil yorumlayıcı motorudur.

---

## ⚡ v3 Performans ve Mimari Yenilikleri

1. **Tokenizasyon ve AST Benzeri Hızlı İndeksleme:**
   - Betik dosyası bellek içine yüklendikten sonra komut satırları ve etiket konumları (`:label`) tek seferde indekslenir. `GOTO` ve `GOSUB` işlemlerinde dosya başından arama maliyeti sıfıra indirilmiştir.

2. **PSRAM Üzerinde Zero-Copy Ön Bellekleme ve Stack Havuzu:**
   - Betikler ve Shunting-Yard matematik yığınları (`vmValuePool`, `vmOpPool`) OPI PSRAM belleğine kopyalanır ve doğrudan RAM üzerinden yorumlanır. DRAM bellek tasarrufu sağlanır.

3. **Cooperative Multitasking & Drop Logging IPC:**
   - Arka plan task'ları (`bgScriptTask[0..2]`) `vTaskDelay(1)` ile otomatik yield gerçekleştirerek ESP32-S3 Core 0 watchdog reset'lerini önler.
   - IPC mesajları 16 slotlu `ipcQueue` üzerinden aktarılır. Kuyruk doluysa kilitlenme yaşanmaz (Drop Logged IPC).

---

## 📡 IPC (Inter-Process Communication) Akış Şeması

```text
┌────────────────────────────────────────────────────────┐
│  Arka Plan Task (Core 0)                               │
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
│  Ön Plan UI / System Engine (Core 1)                   │
│  sensor_temp değişkenini günceller ve ekrana yansıtır  │
└────────────────────────────────────────────────────────┘
```
