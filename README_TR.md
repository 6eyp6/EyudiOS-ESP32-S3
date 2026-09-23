<div align="center">

# 🚀 EyudiOS ESP32-S3 Edition v2.0

### ESP32-S3 İçin Gelişmiş Çift Çekirdek İşletim Sistemi ve Betik Çalıştırma Ortamı

[![Lang Türkçe](https://img.shields.io/badge/Lang-T%C3%BCrk%C3%A7e-red.svg)](README_TR.md)
[![Lang English](https://img.shields.io/badge/Lang-English-blue.svg)](README.md)
[![EyudiOS Flasher](https://img.shields.io/badge/Web%20Flasher-EyudiOS%20Flasher-purple.svg)](https://github.com/6eyp6/EyudiOS-Flasher)
[![License: GPL v3 with Linking Exception](https://img.shields.io/badge/License-GPLv3%20with%20Linking%20Exception-green.svg)](LICENSE)
[![PlatformIO](https://img.shields.io/badge/PlatformIO-ESP32--S3-orange.svg)](https://platformio.org/)
[![Framework](https://img.shields.io/badge/Framework-Arduino%20%2F%20FreeRTOS-teal.svg)](https://www.espressif.com/)

---

</div>

**EyudiOS ESP32-S3 Edition v2.0**, **Eyüp SAĞLAM (Eyudio)** tarafından geliştirilmiş, ESP32-S3 mikrodenetleyicisi üzerinde çalışmak üzere tasarlanmış, çift çekirdek FreeRTOS task yönetimi, doğrudan 400x300 VGA ekran çıktısı, çok katmanlı girdi sürücüleri, `EsDOS` komut satırı kabuğu, `EyuScript Turbo v3` betik dili motoru ve modüler uygulama kayıt yapısına (`AppRegistry`) sahip açık kaynaklı gelişmiş bir işletim sistemi çekirdeğidir.

> ⚡ **Kod Derlemeden Hızlı Kurulum:** EyudiOS'u bilgisayarınızda PlatformIO derleme ortamı kurmadan doğrudan tarayıcı üzerinden ESP32-S3 kartınıza yüklemek için resmi [EyudiOS Flasher](https://github.com/6eyp6/EyudiOS-Flasher) aracını kullanabilirsiniz!

---

## 🌟 Öne Çıkan Özellikler (v2.0)

- 🖥️ **VGA & Ekran Sürücüsü:** Bitluni VGA kütüphanesi entegrasyonu ile 400x300 çözünürlükte double-buffered grafik arayüzü.
- ⌨️ **Çok Katmanlı Girdi (Input) Mimarisi:**
  - **Yerleşik USB OTG Host:** USB D+/D- pinlerinden doğrudan HID Klavye & Fare.
  - **CH375 / Coprocessor USB Host (UART):** Arduino (Leonardo/Uno) coprocessor üzerinden klavye, fare, PS2/3/4 ve Xbox kumanda desteği.
  - **NimBLE Bluetooth (BLE):** Kablosuz BLE Gamepad ve klavye desteği.
  - **Grafik Sanal Ekran Klavyesi (VKB):** Fare/dokunmatik destekli ekran klavyesi.
  - **GPIO ISR Köprüsü:** FreeRTOS ISR donanımsal kesme butonları.
- ⚡ **Çift Çekirdek Concurrency (FreeRTOS):**
  - **Core 1:** Ön plan UI, pencereler, masaüstü çizim döngüsü ve ön plan betik yürütme.
  - **Core 0:** Arka plan betik iş parçacıkları (`bgScriptTask[0..2]`), FreeRTOS IPC mesaj kuyruğu ve non-blocking I/O.
- 🧩 **Temiz Modüler Uygulama Yapısı (`AppRegistry`):**
  - Sistem uygulamaları (`FileManager`, `Eyuditor`, `EsDOS`, `Doom`, `Settings` vb.) `AppDescriptor` yapısı ile sisteme bağlanır. `#ifdef` kod kirliliği barındırmaz.
- 🐚 **EsDOS Terminal Kabuğu:** UNIX/DOS benzeri komut satırı arayüzü (`ls`, `cd`, `cat`, `run`, `runbg`, `ps`, `killbg`, `wifi`, `httpget`, `temp` vb.).
- 📜 **EyuScript Turbo v3 Motoru:**
  - Değişkenler, döngüler (`FOR/NEXT`, `WHILE/WEND`), dallanmalar (`GOTO`, `GOSUB`), ekran çizim komutları (`RECT`, `CIRCLE`, `LINE`, `PRINT`).
  - PSRAM tabanlı Shunting-Yard matematik motoru (`SQRT`, `SIN`, `COS`, `TAN`, `ABS`, `POW`).
  - Arka plan iş yükleri için FreeRTOS tabanlı IPC ile ana ekrana non-blocking veri aktarımı.
- 🎮 **Dahili Portlar & Uygulamalar:**
  - `Doom Engine` (GPLv2/v3 entegre port).
  - `Eyuditor` Grafik metin editörü.
  - `File Manager` Dosya yöneticisi & SD kart tarayıcısı.
  - `Audio Engine` I2S tabanlı MP3/WAV ses ve müzik çalma desteği.

---

## ⚡ Hızlı Kurulum ([EyudiOS Flasher](https://github.com/6eyp6/EyudiOS-Flasher))

PlatformIO veya kod derleme ile uğraşmadan EyudiOS'u yüklemek isterseniz:
1. [EyudiOS Flasher Reposunu](https://github.com/6eyp6/EyudiOS-Flasher) ziyaret edin.
2. ESP32-S3 kartınızı USB ile bilgisayarınıza bağlayın.
3. EyudiOS Flasher uygulamasını çalıştırarak hazır derlenmiş firmware ve partition şemalarını saniyeler içinde yükleyin!

---

## 🔌 Donanım Gereksinimleri & Pin Eşleşmesi

### Donanım:
* **MCU:** ESP32-S3 (YD-ESP32-S3 / ESP32-S3-DevKitC-1)
* **RAM/Flash:** Minimum 8MB / 16MB OPI PSRAM + 16MB Flash
* **Girdi Cihazları:**
  - USB HID Klavye ve Fare (Yerleşik USB D+/D- veya CH375 / USB Host Shield 2.0)
  - PlayStation 2 / 3 / 4, Xbox Kumandaları & Bluetooth Dongle (Leonardo / Uno Coprocessor)
  - Bluetooth BLE Kablosuz Oyun Kolu & Klavye
* **Depolama:** MicroSD Kart Modülü (SPI Modu)

### Pin Haritası (`SystemConfig.h`):

| Bileşen | Sinyal | ESP32-S3 Pin |
|:---|:---|:---|
| **VGA Çıktısı** | RED | GPIO 4 |
| | GREEN | GPIO 5 |
| | BLUE | GPIO 6 |
| | HSYNC | GPIO 7 |
| | VSYNC | GPIO 15 |
| **SD Kart (SPI)** | CS | GPIO 10 |
| | SCK | GPIO 12 |
| | MISO | GPIO 13 |
| | MOSI | GPIO 11 |
| **CH375 USB Host** | RX | GPIO 3 |
| | TX | GPIO 46 |
| **Gelişmiş** | VBUS Enable | GPIO 21 |
| | NeoPixel LED | GPIO 48 |

---

## 🛠️ Kurulum & Derleme (PlatformIO)

1. Visual Studio Code ve [PlatformIO IDE](https://platformio.org/) eklentisini yükleyin.
2. Projeyi klonlayın veya indirin:
   ```bash
   git clone https://github.com/6eyp6/EyudiOS-ESP32-S3.git
   ```
3. VS Code içinden projeyi açın.
4. `platformio.ini` dosyasının kartınıza (16MB Flash + OPI PSRAM) uygun olduğunu doğrulayın.
5. **Build & Upload** butonuna basarak kartınıza yükleyin.

---

## 📚 Dokümantasyon (`docs/`)

Dil Seçimi: 🇹🇷 **Türkçe** | 🇬🇧 [English](README.md)

- 📖 [docs/tr/architecture.md](docs/tr/architecture.md) ([EN](docs/architecture.md)) — Çekirdek Mimarisi & Girdi Sürücüleri.
- 🐚 [docs/tr/esdos_reference.md](docs/tr/esdos_reference.md) ([EN](docs/esdos_reference.md)) — EsDOS Kabuk Referansı.
- 📜 [docs/tr/eyuscript_reference.md](docs/tr/eyuscript_reference.md) ([EN](docs/eyuscript_reference.md)) — EyuScript Dil Kılavuzu.
- ⚡ [docs/tr/eyuscript_turbo_v3.md](docs/tr/eyuscript_turbo_v3.md) ([EN](docs/eyuscript_turbo_v3.md)) — EyuScript Turbo v3 & IPC Motoru.

---

## 📄 Lisans & Telif Hakkı

EyudiOS ESP32-S3 Edition v2.0 Telif Hakkı (C) 2026 **Eyüp SAĞLAM (Eyudio)** <https://github.com/6eyp6>.
**Özel Bağlama İstisnası İçeren GNU General Public License v3.0** (GPL-3.0 with Linking Exception) altında lisanslanmıştır. Detaylar için [LICENSE](LICENSE) dosyasına bakabilirsiniz.
