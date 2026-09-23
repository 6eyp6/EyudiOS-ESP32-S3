# EsDOS v2.1 Kabuk Referansı & Komut Dizini

[![Lang Türkçe](https://img.shields.io/badge/Lang-T%C3%BCrk%C3%A7e-red.svg)](esdos_reference.md)
[![Lang English](https://img.shields.io/badge/Lang-English-blue.svg)](../esdos_reference.md)

> EyudiOS ESP32-S3 Edition v2.0 — Dahili Metin Kabuğu (CLI)

---

## 🧠 Çekirdek (Kernel) & Donanım Yürütme Mimarisi

> [!IMPORTANT]
> **Çift Çekirdek Görev Dağılımı:**
> - **Core 1 (Foreground REPL & UI):** Ön plan `run` komutları, kullanıcı girdi analizi (CH375 USB Klavye/Fare) ve 400x300 VGA çizimleri.
> - **Core 0 (FreeRTOS Background Slots):** Arka plan `runbg` scriptleri (`bgScriptTask[0..2]`), non-blocking I/O ve 16-slotlu `ipcQueue` yönetimi.

---

## 📊 Hızlı Komut Referansı (Cheatsheet / TL;DR)

| Komut | Kategori | Açıklama | Örnek Kullanım |
|:---|:---|:---|:---|
| `ls` / `dir` | Dosya | Dizin içeriğini listeler | `ls /scripts` |
| `cd` | Dosya | Çalışma dizinini değiştirir | `cd /system` |
| `pwd` | Dosya | Aktif dizin yolunu gösterir | `pwd` |
| `cat` / `type` | Dosya | Metin dosyası içeriğini okur | `cat /test.eyu` |
| `rm` / `del` | Dosya | Dosyayı siler | `rm /file.txt` |
| `mkdir` | Dosya | Yeni dizin oluşturur | `mkdir /data` |
| `edit` | Editör | Eyuditor grafik editöründe açar | `edit /saat.eyu` |
| `run` | Betik | Betiği ön planda (Core 1) çalıştırır | `run /app.eyu` |
| `runbg` | Betik | Betiği arka planda (Core 0) çalıştırır | `runbg /saat.eyu` |
| `bglist` / `ps` | Görev | Aktif arka plan slotlarını listeler | `bglist` |
| `killbg` | Görev | Arka plan betiğini durdurur | `killbg 0` |
| `wifi` | Ağ | Wi-Fi tarama/bağlanma/durum | `wifi status` |
| `ip` | Ağ | Local IP adresini yazdırır | `ip` |
| `ping` | Ağ | Sunucuya ping paketi atar | `ping google.com` |
| `httpget` | Ağ | URL'ye HTTP GET isteği gönderir | `httpget http://api.com` |
| `temp` | Donanım | ESP32-S3 çip sıcaklığını (°C) okur | `temp` |
| `sysinfo` / `info` | Sistem | CPU, RAM, PSRAM ve Uptime gösterir | `sysinfo` |
| `ver` | Sistem | Çekirdek sürüm bilgisi | `ver` |
| `reset` / `reboot` | Sistem | ESP32-S3'ü yeniden başlatır | `reset` |
| `clear` / `cls` | Konsol | Konsol ekranını temizler | `clear` |
| `help` | Konsol | Yardım özetini basar | `help` |
| `exit` | Konsol | Grafik Masaüstüne döner | `exit` |

---

## 📂 Dosya ve Dizin Yönetim Komutları

### `ls` / `dir`
Mevcut dizindeki dosya ve klasörleri listeler.
* **Kullanım:** `ls [dizin_yolu]`
* **Örnek:** `ls /`, `ls /scripts`

### `cd`
Çalışma dizinini değiştirir.
* **Kullanım:** `cd <dizin_yolu>`
* **Örnek:** `cd /system`, `cd ..`

### `pwd`
Mevcut dizin yolunu ekrana yazdırır.

### `cat` / `type`
Metin dosyasının içeriğini konsola yazdırır.
* **Kullanım:** `cat <dosya_yolu>`
* **Örnek:** `cat /test.eyu`

### `rm` / `del`
Belirtilen dosyayı siler.
* **Kullanım:** `rm <dosya_yolu>`

### `mkdir`
Yeni bir dizin oluşturur.
* **Kullanım:** `mkdir <dizin_yolu>`

### `edit`
`Eyuditor` grafik metin editöründe dosyayı açar.
* **Kullanım:** `edit <dosya_yolu>`
* **Örnek:** `edit /saat.eyu`

---

## 📜 Betik ve Görev Çalıştırma Komutları

### `run`
EyuScript betiğini ön planda (Foreground - Core 1) çalıştırır.
* **Kullanım:** `run <betik_yolu>`
* **Örnek:** `run /demo.eyu`

### `runbg`
EyuScript betiğini arka planda (Background Task - Core 0) çalıştırır.
* **Kullanım:** `runbg <betik_yolu>`
* **Örnek:** `runbg /saat.eyu`

### `bglist` / `ps`
Arka planda çalışan aktif betik slotlarını listeler.
* **Çıktı Örneği:**
  ```text
  BG[0] /saat.eyu (Aktif)
  BG[1] Boş
  BG[2] Boş
  ```

### `killbg`
Arka planda çalışan betik görevini durdurur.
* **Kullanım:** `killbg <slot_no>`
* **Örnek:** `killbg 0`

---

## 🌐 Ağ ve İnternet Komutları

### `wifi`
Wi-Fi bağlantısını yönetir ve tarama yapar.
* **Komutlar:**
  - `wifi scan` — Çevredeki Wi-Fi ağlarını tarar ve RSSI değerleriyle listeler.
  - `wifi status` — Mevcut Wi-Fi bağlantı durumunu ve local IP adresini gösterir.
  - `wifi off` / `wifi disconnect` — Ağ bağlantısını keser.
  - `wifi <SSID> <SIFRE>` — Ağ bağlanır (Spinner animasyonu ile max 15s bekler).

### `ip`
Mevcut Wi-Fi IP adresini gösterir.

### `ping`
Belirtilen sunucuya ICMP/TCP ping paketi gönderir.
* **Kullanım:** `ping <host>`
* **Örnek:** `ping google.com`

### `httpget`
Belirtilen URL'ye HTTP GET isteği gönderir ve yanıtı konsola basar.
* **Kullanım:** `httpget <URL>`
* **Örnek:** `httpget http://worldtimeapi.org/api/ip`

---

## 🛠️ Sistem İzleme ve Donanım Araçları

### `temp`
ESP32-S3 çipinin dahili sıcaklık sensöründen sıcaklığı (°C) okur.

### `ver` / `version`
Çekirdek sürümünü ve CPU çalışma frekansını gösterir:
```text
EyudiOS S3 Edition - EsDOS v2.1
ESP32-S3 @ 240 MHz
```

### `sysinfo` / `info`
Sistem kaynak kullanımını gösterir: CPU frekansı, Flash boyutu, kullanılabilir PSRAM, Heap durumu ve Uptime bilgisi.

### `reset` / `reboot`
ESP32-S3 sistemini yeniden başlatır.

### `clear` / `cls`
EsDOS konsol ekranını temizler.

### `help`
Tüm kullanılabilir komutların özetini ekrana basar.

### `exit`
EsDOS kabuğundan çıkıp grafik masaüstü arayüzüne (Desktop UI) döner.
