# EyuScript Referans Kılavuzu (v2.0)

[![Lang Türkçe](https://img.shields.io/badge/Lang-T%C3%BCrk%C3%A7e-red.svg)](eyuscript_reference.md)
[![Lang English](https://img.shields.io/badge/Lang-English-blue.svg)](../eyuscript_reference.md)

> EyudiOS ESP32-S3 Edition v2.0 — Dahili Yorumlamalı Betik Dili Spesifikasyonu (Language Spec)

---

## 💎 Genel Bakış & Hızlı Komut Spesifikasyonu

| Kategori | Komut | Syntax | Çıktı / Hedef | Açıklama |
|:---|:---|:---|:---|:---|
| **Değişken** | `SET` | `SET <var> <val>` | `<var>` | Değişken ataması yapar. |
| **Aritmetik** | `ADD` / `SUB` | `ADD <var> <val>` | `<var>` | Değeri artırır veya azaltır. |
| **Aritmetik** | `MUL` / `DIV` | `MUL <var> <val>` | `<var>` | Çarpar veya böler. |
| **Matematik** | `SQRT` / `POW` | `SQRT <val>` | `math_result` | Karekök veya üs alır. |
| **Matematik** | `SIN` / `COS` / `TAN` | `SIN <rad>` | `math_result` | Trigonometrik hesaplama yapar. |
| **Grafik** | `CLEAR` | `CLEAR <color>` | Framebuffer | Ekranı belirtilen renkle temizler. |
| **Grafik** | `RECT` / `CIRCLE` | `RECT <x> <y> <w> <h> <c>` | Framebuffer | Çizim şekli ekler. |
| **Grafik** | `PRINT` / `SHOW` | `PRINT <text>` | VGA Display | Ekrana metin yazar ve ekrana basar. |
| **Donanım** | `PINMODE` | `PINMODE <pin> <mode>` | GPIO Hardware | GPIO modunu ayarlar (`OUTPUT`, `INPUT`). |
| **Donanım** | `DIGITALWRITE` | `DIGITALWRITE <pin> <val>` | GPIO Hardware | Pin voltaj seviyesini değiştirir. |
| **Donanım** | `DIGITALREAD` | `DIGITALREAD <pin>` | `lastread` | Dijital pin durumunu okur. |
| **Akış** | `IF` / `ELSE` | `IF <cond> ... ENDIF` | Flow Control | Koşullu dallanma. |
| **Akış** | `FOR` / `WHILE` | `FOR i 1 10 ... NEXT` | Flow Control | Döngü blokları. |
| **IPC** | `IPC` | `IPC <key> <val>` | `ipcQueue` | Arka plandan ön plana non-blocking mesaj. |

---

## 🎨 Renk Paleti ve Format Rehberi (RGB565)

EyudiOS VGA sürücüsü 16-bit **RGB565** renk formatı kullanır:

| Renk Kodu (Decimal) | Hex Kodu | Renk Adı | Görsel Karşılık |
|:---|:---|:---|:---|
| `0` | `0x0000` | Siyah (Black) | ⬛ |
| `63488` | `0xF800` | Kırmızı (Red) | 🟥 |
| `2016` | `0x07E0` | Yeşil (Green) | 🟩 |
| `31` | `0x001F` | Mavi (Blue) | 🟦 |
| `65504` | `0xFFE0` | Sarı (Yellow) | 🟨 |
| `65535` | `0xFFFF` | Beyaz (White) | ⬜ |
| `2047` | `0x07FF` | Turkuaz (Cyan) | 🟦 |
| `63519` | `0xF81F` | Mor (Magenta) | 🟪 |

---

## 🧮 Shunting-Yard Matematik ve Dönüş Değerleri

Matematiksel fonksiyonlar sonuçları özel sistem değişkenlerinde tutar:

| Fonksiyon | Örnek | Hedef Değişken | Örnek Çıktı |
|:---|:---|:---|:---|
| `RANDOM` | `RANDOM 1 100` | `math_result` | `"42"` |
| `SQRT` | `SQRT 16` | `math_result` | `"4.0000"` |
| `SIN` / `COS` | `SIN 1.57` | `math_result` | `"1.0000"` |
| `ABS` | `ABS -10` | `math_result` | `"10.0000"` |
| `POW` | `POW 2 8` | `math_result` | `"256.0000"` |
| `DIGITALREAD` | `DIGITALREAD 4` | `lastread` | `"1"` (HIGH) |
| `ANALOGREAD` | `ANALOGREAD 1` | `lastread` | `"2048"` |

---

## 🔤 Değişkenler, Türler ve Aritmetik

EyuScript dinamik tip yönetimini ve PSRAM tabanlı değişken saklamayı destekler. Değişkenler `SET` komutu ile tanımlanır.

```basic
SET x 100
SET y 50
SET isim "EyudiOS"

ADD x 25          # x = 125
SUB y 10          # y = 40
MUL x 2           # x = 250
DIV y 2           # y = 20
MOD x 10          # x = 0
INC x             # x = 251
DEC y             # y = 19
```

### Dahili Sistem Değişkenleri ve Sabitler
* `{sw}` — Ekran Genişliği (400px)
* `{sh}` — Ekran Yüksekliği (300px)
* `{fps}` — Anlık VGA kare hızı
* `{mouseX}` — Fare X koordinatı
* `{mouseY}` — Fare Y koordinatı
* `{millis}` — Çalışma süresi (milisaniye)
* `R0` .. `R99` — Yüksek hızlı kayıtçılar (VM Registers)

---

## 🎨 Grafik ve Ekran Komutları (Ön Plan)

> **Not:** Çizim komutları `systemMutex` ile korumalıdır ve sadece Ön Plan (Foreground) scriptlerinde çalışır.

* `CLEAR <renk_kodu>` — Ekranı belirtilen renk kodu ile temizler. (0: Siyah, 64: Koyu Yeşil vb.)
* `RECT <x> <y> <w> <h> <renk_kodu>` — İçi dolu dikdörtgen çizer.
* `RECTOUT <x> <y> <w> <h> <renk_kodu>` — İçi boş çerçeve çizer.
* `CIRCLE <x> <y> <r> <renk_kodu>` — Daire çizer.
* `LINE <x1> <y1> <x2> <y2> <renk_kodu>` — Çizgi çizer.
* `SETCURSOR <x> <y>` — Metin yazma imlecini ayarlar.
* `PRINT <metin_veya_degisken>` — Ekrana metin yazdırır.
* `PRINTLN <metin_veya_degisken>` — Metin yazdırıp alt satıra geçer.
* `SHOW` — Framebuffer içeriğini VGA ekranına yansıtır (Double Buffer Flush).

---

## 🔀 Mantıksal Akış ve Kontrol Yapıları

### Koşullu İfadeler (`IF / ELSE / ENDIF`)
```basic
IF {x} > 100
  PRINTLN "x 100'den buyuk"
ELSE
  PRINTLN "x 100 veya daha kucuk"
ENDIF
```

### Döngüler (`FOR / NEXT` ve `WHILE / WEND`)
```basic
FOR i 1 10
  PRINTLN "Sayi: " + i
NEXT

WHILE {y} > 0
  SUB y 1
WEND
```

### Etiketler ve Dallanma (`GOTO`, `GOSUB`, `RETURN`)
```basic
GOSUB alt_program
END

:alt_program
  PRINTLN "Subroutine calisti"
RETURN
```

---

## 🔌 Donanım ve GPIO Kontrolü

* `PINMODE <pin>, <mode>` — Pin modunu ayarlar (`OUTPUT`, `INPUT`, `INPUT_PULLUP`).
* `DIGITALWRITE <pin>, <HIGH|LOW>` — Dijital çıkış verir.
* `DIGITALREAD <pin>` — Dijital giriş okur (sonuç -> `lastread`).
* `ANALOGREAD <pin>` — Analog giriş okur (sonuç -> `lastread`).

---

## ⚡ Arka Plan (BG) ve IPC Komutları

* `WAIT <ms>` — Görevi milisaniye cinsinden bekletir (`vTaskDelay`).
* `YIELD` — FreeRTOS task işlemci payını devreder.
* `IPC <key> <val>` — Arka plandan ön plana non-blocking veri aktarımı yapar (`ipcQueue`).
* `LOG <metin>` — Seri port konsoluna log basar.
