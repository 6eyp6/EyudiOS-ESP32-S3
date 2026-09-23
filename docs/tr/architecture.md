# EyudiOS ESP32-S3 Edition v2.0 — Sistem Mimarisi & Concurrency Notları

[![Lang Türkçe](https://img.shields.io/badge/Lang-T%C3%BCrk%C3%A7e-red.svg)](architecture.md)
[![Lang English](https://img.shields.io/badge/Lang-English-blue.svg)](../architecture.md)

> EyudiOS S3 Edition v2.0 — Çekirdek (Kernel), Girdi (Input) Mimarisi, Bellek (PSRAM) ve Task Yönetimi

---

## 🏗️ Görev (Task) ve Çekirdek (Core) Eşleşmesi

EyudiOS v2.0, ESP32-S3'ün çift çekirdekli (Xtensa LX7 @ 240MHz) donanım mimarisini FreeRTOS görevleri ile optimize eder:

```text
┌─────────────────────────────────── Core 1 (UI & System) ───────────────────────┐
│                                                                                │
│   loop() — Ana UI Döngüsü                              Priority: 1             │
│   ├── processUSBInput()          → USB Klavye & Fare olayları (CH375/Native)   │
│   ├── processBLEInput()          → NimBLE Gamepad & Kablosuz Klavye okuma       │
│   ├── IPC mesaj işle             → Arka plandan gelen Queue (16 slot) paketleri  │
│   └── Render (systemMutex korumalı)                                            │
│       ├── runningScript → ESC overlay ve ön plan ScriptEngine çizimi           │
│       ├── showFileManager  → drawFileManager()                                 │
│       ├── showEyuditor     → drawEyuditor()                                    │
│       ├── showEsdos        → drawEsdos()                                       │
│       └── Desktop/Windows  → AppRegistry + renderDesktop()                     │
│                                                                                │
│   runEyuScriptTask()             Foreground Script    Priority: 1 (Core 1)    │
│   └── Framebuffer: systemMutex ile korumalı çizim                              │
│                                                                                │
└────────────────────────────────────────────────────────────────────────────────┘

┌─────────────────────────────────── Core 0 (Background Tasks) ──────────────────┐
│                                                                                │
│   bgScriptTask[0]               BG Script Slot 0      Priority: 0 (Arka Plan)  │
│   bgScriptTask[1]               BG Script Slot 1      Priority: 0              │
│   bgScriptTask[2]               BG Script Slot 2      Priority: 0              │
│                                                                                │
│   ✓ Tüm BG task'lar vTaskDelay(1) ile kooperatif yield gerçekleştirir          │
│   ✓ Framebuffer / Çizim ekranına doğrudan erişim YOKTUR                        │
│   ✓ SD kart erişimi sdMutex ile korumalıdır                                     │
│   ✓ IPC komutu ile FreeRTOS Queue üzerinden ön plana veri taşır                │
│                                                                                │
└────────────────────────────────────────────────────────────────────────────────┘
```

---

## ⌨️ Çok Katmanlı Girdi (Input) Sürücü Mimarisi

EyudiOS v2.0 donanım ve yazılım seviyesinde 6 farklı girdi katmanını destekler:

1. **Yerleşik ESP32-S3 USB OTG Host Sürücüsü:**
   - ESP32-S3 çipinin dahili USB D+/D- pinleri üzerinden doğrudan USB HID Klavye (8-byte rapor) ve USB HID Fare (delta X/Y, sol/sağ tık) desteği.
2. **CH375 Donanımsal USB Sürücüsü (UART):**
   - İkincil coprocessor olarak çalışan CH375 modülü üzerinden seri iletişimle (GPIO 3 RX / GPIO 46 TX) donanımsal klavye ve fare desteği.
3. **NimBLE Bluetooth / BLE Oyun Kolu & Klavye Sürücüsü:**
   - NimBLE-Arduino kütüphanesi ile kablosuz Bluetooth Gamepad, Oyun Kolları ve Kablosuz Klavyelerden gelen tuş girdilerini işleme.
4. **Leonardo / PSX Controller Sürücüsü:**
   - Harici Leonardo ve PSX gamepad sürücü entegrasyonu.
5. **Grafik Sanal Ekran Klavyesi (VKB):**
   - Ekran üzerinde fare ve dokunmatik ile kullanılabilen grafik klavye.
6. **GPIO Kesme (ISR) Köprüsü:**
   - Donanımsal kesme butonları için FreeRTOS `vTaskNotifyGiveFromISR` destekli GPIO kesme fonksiyonları.

---

## 🧠 Bellek ve RAM Yönetimi (OPI PSRAM Entegrasyonu)

EyudiOS v2.0, 8MB / 16MB OPI PSRAM entegrasyonu sayesinde RAM kıtlığını ortadan kaldırır:

1. **PSRAM Framebuffer & Double Buffering:**
   - VGA sürücüsü (`ESP32-S3-VGA`) 400x300 çözünürlükteki ekran ara belleğini PSRAM üzerinde tutar (`systemMutex` ile erişilir).
2. **EyuScript Turbo Stack Pool:**
   - Derin matematiksel hesaplama yığınları (Shunting-Yard Stack) DRAM yerine PSRAM üzerinde ayrılmış havuzda (`vmValuePool`, `vmOpPool`) yürütülür.
3. **SpiRamJsonDocument:**
   - JSON verileri ve HTTP paketleri `SpiRamAllocator` ile doğrudan PSRAM üzerinde işlenir.

---

## 🔒 Mutex ve Senkronizasyon Matrisi

| Kaynak / Yapı | Koruma Mekanizması | Tür | Açıklama |
|:---|:---|:---|:---|
| **Framebuffer / Ekran** | `systemMutex` | Recursive Mutex | UI döngüsü ile ön plan scriptlerinin eş zamanlı ekran çizimini serialize eder. |
| **SD Kart (EyuFS)** | `sdMutex` | Binary Semaphore | Ön plan script, arka plan task ve EsDOS dosya erişimlerini çakışmalara karşı korur. |
| **USB Host Buffer** | `systemMutex` | Mutex | CH375 ve Native USB sürücüsü klavye/fare tampon belleği koruması. |
| **IPC Mesaj Kuyruğu** | `ipcQueue` | FreeRTOS Queue (16 slot) | Arka plandan ön plana tek yönlü non-blocking `xQueueSend` ile haberleşme sağlar. |

---

## 🧩 Temiz Modüler Uygulama Yapısı (`AppDescriptor` & `AppRegistry`)

EyudiOS v2.0, `#ifdef` kod kirliliği olmadan tüm sistem ve harici uygulamaların kayıt edilebilmesi için modüler `AppDescriptor` yapısını kullanır:

```cpp
typedef struct {
    const char* id;          // Uygulama kimliği (Örn: "FileManager")
    const char* title;       // Masaüstünde görünecek ad
    const char* symbol;      // 2 karakterlik ikon ikili sembolü
    uint16_t color;          // İkon tema rengi (RGB565)
    bool enabled;            // Durum
    void (*launch)(void);    // Çalıştırma fonksiyon göstericisi (Function pointer)
} AppDescriptor;
```

Bu yapı sayesinde bağımsız veya private modüller `registerExternalApps()` zayıf bağlama fonksiyonu ile sisteme enjekte edilebilir.

---

## ⚖️ Lisanslama ve Yasal Haklar (GPL-3.0 with Linking Exception)

EyudiOS v2.0 çekirdeği **GPL-3.0 ile Birlikte Özel Bağlama İstisnası (Additional Linking Exception)** lisans koşullarına sahiptir.
Bu istisna sayesinde, EyudiOS çekirdeği üzerine yazılan veya bağlanan özel (proprietary) modüller kaynak kodlarını açıklamak zorunda olmadan güvenle dağıtılabilir.
