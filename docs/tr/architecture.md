# EyudiOS ESP32-S3 Edition v2.0 — Sistem Mimarisi & Concurrency Notları

[![Lang Türkçe](https://img.shields.io/badge/Lang-T%C3%BCrk%C3%A7e-red.svg)](architecture.md)
[![Lang English](https://img.shields.io/badge/Lang-English-blue.svg)](../architecture.md)
[![EyudiOS Flasher](https://img.shields.io/badge/Web%20Flasher-EyudiOS%20Flasher-purple.svg)](https://github.com/6eyp6/EyudiOS-Flasher)

> EyudiOS S3 Edition v2.0 — Çekirdek (Kernel), Girdi (Input) Mimarisi, Bellek (PSRAM) ve Task Yönetimi

---

## 🏗️ Görev (Task) ve Çekirdek (Core) Eşleşmesi

EyudiOS v2.0, ESP32-S3'ün çift çekirdekli (Xtensa LX7 @ 240MHz) donanım mimarisini FreeRTOS görevleri ile optimize eder:

```text
┌─────────────────────────────────── Core 1 (UI & System) ───────────────────────┐
│                                                                                │
│   loop() — Ana UI Döngüsü                              Priority: 1             │
│   ├── processUSBInput()          → USB Klavye & Fare olayları (CH375/Native)   │
│   ├── processBLEInput()          → NimBLE Gamepad & Kablosuz Klavye okuma      │
│   ├── IPC mesaj işle             → Arka plandan gelen Queue (16 slot) paketleri│
│   └── Render (systemMutex korumalı)                                            │
│       ├── runningScript → ESC overlay ve ön plan ScriptEngine çizimi           │
│       ├── showFileManager  → drawFileManager()                                 │
│       ├── showEyuditor     → drawEyuditor()                                    │
│       ├── showEsdos        → drawEsdos()                                       │
│       └── Desktop/Windows  → AppRegistry + renderDesktop()                     │
│                                                                                │
│   runEyuScriptTask()             Foreground Script    Priority: 1 (Core 1)     │
│   └── Framebuffer: systemMutex ile korumalı çizim                              │
│                                                                                │
└────────────────────────────────────────────────────────────────────────────────┘

┌─────────────────────────────────── Core 0 (Background Tasks) ──────────────────┐
│                                                                                │
│   bgScriptTask[0]               BG Script Slot 0      Priority: 0 (Arka Plan)  │
│   bgScriptTask[1]               BG Script Slot 1      Priority: 0              │
│   bgScriptTask[2]               BG Script Slot 2      Priority: 0              │
│                                                                                │
│     Tüm BG task'lar vTaskDelay(1) ile kooperatif yield gerçekleştirir          │
│     Framebuffer / Çizim ekranına doğrudan erişim YOKTUR                        │
│    SD kart erişimi sdMutex ile korumalıdır                                     │
│     IPC komutu ile FreeRTOS Queue üzerinden ön plana veri taşır                │
│                                                                                │
└────────────────────────────────────────────────────────────────────────────────┘
```

---

## ⌨️ Çok Katmanlı Girdi (Input) Sürücü Mimarisi

1. **Yerleşik ESP32-S3 USB OTG Host:** USB D+/D- pinleri üzerinden doğrudan HID Klavye & Fare desteği.
2. **CH375 / Coprocessor USB Host (UART):** Arduino (Leonardo/Uno) coprocessor üzerinden (GPIO 3/46) donanımsal klavye, fare, PS2/3/4 ve Xbox kumanda desteği.
3. **NimBLE Bluetooth (BLE):** Kablosuz BT Gamepad ve klavye desteği.
4. **Sanal Ekran Klavyesi (VKB):** Fare/dokunmatik destekli grafik klavye.
5. **GPIO ISR Köprüsü:** Donanımsal kesme butonları için `vTaskNotifyGiveFromISR` desteği.

### 🕹️ Arduino Leonardo Coprocessor Kurulum & Haberleşme Mimarisi

EyudiOS v2.0, harici USB cihazları (PS2/PS3/PS4/Xbox kumandaları ve Bluetooth Dongle) yönetmek için Arduino Leonardo/UNO üzerinde bulunan işlemcisini yardımcı sürücü olarak kullanır:

* **Sürücü Dosyaları:**
  - `leonardo_drivers/usb psx/usb_controller_driver/usb_controller_driver.ino` (Kablolu USB & PS2 kollar)
  - `leonardo_drivers/bluetooth/bluetooth_dongle_driver.ino` (USB Bluetooth Dongle ile kablosuz kollar)
* **Bellek Optimizasyonu:** Leonardo'nun 28KB kısıtlı hafızası nedeniyle `.ino` içindeki `#define SUPPORT_PS4`, `#define SUPPORT_PS2` bayrakları ile sadece kullanılan kollar aktifleştirilir.
* **El Sıkışma (Handshake) & UART:** Leonardo açıldığında ESP32-S3'e `SYS,DRV:...` sinyali gönderir. ESP32-S3'ten `SYS,ACK` yanıtı geldiğinde LED sabiten yanar ve 2 oyuncu için buton (`P1,PRESS:X`) / analog (`P1,ANALOG:...`) verileri 115200 Baud hızında aktarılmaya başlar.
* **Bağlantı:** Leonardo RX:8 $\rightarrow$ ESP32-S3 GPIO 46 (TX), Leonardo TX:7 $\rightarrow$ ESP32-S3 GPIO 3 (RX).

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

## ⚡ Yazılım Yükleme Aracı ([EyudiOS Flasher](https://github.com/6eyp6/EyudiOS-Flasher))

Hazır derlenmiş ikili dosyaları ve sistem bölümlerini web ortamından tek tıkla ESP32-S3 kartlarına yüklemek için geliştirilen açık kaynaklı python tabanlı yükleme aracıdır.

---

## ⚖️ Lisanslama ve Yasal Haklar (GPL-3.0 with Linking Exception)

EyudiOS v2.0 çekirdeği **GPL-3.0 ile Birlikte Özel Bağlama İstisnası (Additional Linking Exception)** lisans koşullarına sahiptir.
Bu istisna sayesinde, EyudiOS çekirdeği üzerine yazılan veya bağlanan özel (proprietary) modüller kaynak kodlarını açıklamak zorunda olmadan güvenle dağıtılabilir.
