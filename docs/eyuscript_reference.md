# EyuScript Language Reference Guide (v2.0)

[![Lang English](https://img.shields.io/badge/Lang-English-blue.svg)](eyuscript_reference.md)
[![Lang Türkçe](https://img.shields.io/badge/Lang-T%C3%BCrk%C3%A7e-red.svg)](tr/eyuscript_reference.md)

> EyudiOS ESP32-S3 Edition v2.0 — Built-in Interpreted Scripting Language Specification

---

## 💎 Overview & Quick Spec Table

| Category | Command | Syntax | Target Variable / Dest | Description |
|:---|:---|:---|:---|:---|
| **Variables** | `SET` | `SET <var> <val>` | `<var>` | Assigns a variable value. |
| **Arithmetic** | `ADD` / `SUB` | `ADD <var> <val>` | `<var>` | Increments or decrements a variable. |
| **Arithmetic** | `MUL` / `DIV` | `MUL <var> <val>` | `<var>` | Multiplies or divides a variable. |
| **Math** | `SQRT` / `POW` | `SQRT <val>` | `math_result` | Calculates square root or power. |
| **Math** | `SIN` / `COS` / `TAN` | `SIN <rad>` | `math_result` | Trigonometric calculations. |
| **Graphics** | `CLEAR` | `CLEAR <color>` | Framebuffer | Clears screen with specified color. |
| **Graphics** | `RECT` / `CIRCLE` | `RECT <x> <y> <w> <h> <c>` | Framebuffer | Draws graphics primitive. |
| **Graphics** | `PRINT` / `SHOW` | `PRINT <text>` | VGA Display | Prints text & flushes buffer. |
| **Hardware** | `PINMODE` | `PINMODE <pin> <mode>` | GPIO Hardware | Sets GPIO mode (`OUTPUT`, `INPUT`). |
| **Hardware** | `DIGITALWRITE` | `DIGITALWRITE <pin> <val>` | GPIO Hardware | Writes GPIO output state. |
| **Hardware** | `DIGITALREAD` | `DIGITALREAD <pin>` | `lastread` | Reads digital pin state. |
| **Control** | `IF` / `ELSE` | `IF <cond> ... ENDIF` | Flow Control | Conditional branching. |
| **Control** | `FOR` / `WHILE` | `FOR i 1 10 ... NEXT` | Flow Control | Loop blocks. |
| **IPC** | `IPC` | `IPC <key> <val>` | `ipcQueue` | Non-blocking background to UI msg. |

---

## 🎨 Color Palette & Format Guide (RGB565)

The EyudiOS VGA driver uses 16-bit **RGB565** color format:

| Color Code (Decimal) | Hex Code | Color Name | Visual |
|:---|:---|:---|:---|
| `0` | `0x0000` | Black | ⬛ |
| `63488` | `0xF800` | Red | 🟥 |
| `2016` | `0x07E0` | Green | 🟩 |
| `31` | `0x001F` | Blue | 🟦 |
| `65504` | `0xFFE0` | Yellow | 🟨 |
| `65535` | `0xFFFF` | White | ⬜ |
| `2047` | `0x07FF` | Cyan | 🟦 |
| `63519` | `0xF81F` | Magenta | 🟪 |

---

## 🧮 Shunting-Yard Math Engine & Return Values

Mathematical and hardware functions store their results in dedicated system variables:

| Function | Example | Target Variable | Output Format |
|:---|:---|:---|:---|
| `RANDOM` | `RANDOM 1 100` | `math_result` | `"42"` |
| `SQRT` | `SQRT 16` | `math_result` | `"4.0000"` |
| `SIN` / `COS` | `SIN 1.57` | `math_result` | `"1.0000"` |
| `ABS` | `ABS -10` | `math_result` | `"10.0000"` |
| `POW` | `POW 2 8` | `math_result` | `"256.0000"` |
| `DIGITALREAD` | `DIGITALREAD 4` | `lastread` | `"1"` (HIGH) |
| `ANALOGREAD` | `ANALOGREAD 1` | `lastread` | `"2048"` |

---

## 🔤 Variables, Types & Arithmetic

EyuScript supports dynamic typing and PSRAM variable storage. Variables are defined using the `SET` command.

### Basic Arithmetic Commands
```basic
SET x 100
SET y 50
SET name "EyudiOS"

ADD x 25          # x = 125
SUB y 10          # y = 40
MUL x 2           # x = 250
DIV y 2           # y = 20
MOD x 10          # x = 0
INC x             # x = 251
DEC y             # y = 19
```

### Built-in System Variables & Constants
* `{sw}` — Screen Width (400px)
* `{sh}` — Screen Height (300px)
* `{fps}` — Real-time VGA frames per second
* `{mouseX}` — Mouse X coordinate
* `{mouseY}` — Mouse Y coordinate
* `{millis}` — System uptime in milliseconds
* `R0` .. `R99` — High-speed VM Registers

---

## 🎨 Graphics & Display Commands (Foreground Only)

> **Note:** Drawing primitives require `systemMutex` protection and execute only in Foreground scripts.

* `CLEAR <color_code>` — Clears the display with the specified RGB565 color (0: Black, 64: Dark Green, etc.).
* `RECT <x> <y> <w> <h> <color_code>` — Draws a filled rectangle.
* `RECTOUT <x> <y> <w> <h> <color_code>` — Draws an outlined rectangle.
* `CIRCLE <x> <y> <r> <color_code>` — Draws a circle.
* `LINE <x1> <y1> <x2> <y2> <color_code>` — Draws a line.
* `SETCURSOR <x> <y>` — Sets the text cursor position.
* `PRINT <text_or_var>` — Prints text to the framebuffer.
* `PRINTLN <text_or_var>` — Prints text and moves to the next line.
* `SHOW` — Flushes framebuffer contents to the VGA screen (Double Buffer Flush).

---

## 🔀 Control Flow & Logic

### Conditionals (`IF / ELSE / ENDIF`)
```basic
IF {x} > 100
  PRINTLN "x is greater than 100"
ELSE
  PRINTLN "x is 100 or less"
ENDIF
```

### Loops (`FOR / NEXT` and `WHILE / WEND`)
```basic
FOR i 1 10
  PRINTLN "Count: " + i
NEXT

WHILE {y} > 0
  SUB y 1
WEND
```

### Labels & Branching (`GOTO`, `GOSUB`, `RETURN`)
```basic
GOSUB sub_routine
END

:sub_routine
  PRINTLN "Subroutine executed"
RETURN
```

---

## 🔌 Hardware & GPIO Control

* `PINMODE <pin>, <mode>` — Sets pin mode (`OUTPUT`, `INPUT`, `INPUT_PULLUP`).
* `DIGITALWRITE <pin>, <HIGH|LOW>` — Writes digital state.
* `DIGITALREAD <pin>` — Reads digital state (stored in `lastread`).
* `ANALOGREAD <pin>` — Reads analog value (stored in `lastread`).

---

## ⚡ Background (BG) & IPC Commands

* `WAIT <ms>` — Delays execution in milliseconds (`vTaskDelay`).
* `YIELD` — Yields CPU execution to other FreeRTOS tasks.
* `IPC <key> <val>` — Non-blocking data transfer from background to UI (`ipcQueue`).
* `LOG <text>` — Prints log message to Serial console.
