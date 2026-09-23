# EsDOS v2.1 Shell Reference & Command Directory

[![Lang English](https://img.shields.io/badge/Lang-English-blue.svg)](esdos_reference.md)
[![Lang Türkçe](https://img.shields.io/badge/Lang-T%C3%BCrk%C3%A7e-red.svg)](tr/esdos_reference.md)

> EyudiOS ESP32-S3 Edition v2.0 — Built-in Command Line Shell (CLI)

---

## 🧠 Kernel & Hardware Execution Architecture

> [!IMPORTANT]
> **Dual-Core Task Distribution:**
> - **Core 1 (Foreground REPL & UI):** Foreground `run` commands, USB input processing (CH375 USB Keyboard/Mouse), and 400x300 VGA rendering.
> - **Core 0 (FreeRTOS Background Slots):** Background `runbg` scripts (`bgScriptTask[0..2]`), non-blocking I/O, and 16-slot `ipcQueue` management.

---

## 📊 Quick Reference Table (Cheatsheet / TL;DR)

| Command | Category | Description | Example Usage |
|:---|:---|:---|:---|
| `ls` / `dir` | File | Lists directory contents | `ls /scripts` |
| `cd` | File | Changes working directory | `cd /system` |
| `pwd` | File | Prints current directory path | `pwd` |
| `cat` / `type` | File | Displays text file contents | `cat /test.eyu` |
| `rm` / `del` | File | Deletes a file | `rm /file.txt` |
| `mkdir` | File | Creates a new directory | `mkdir /data` |
| `edit` | Editor | Opens file in Eyuditor editor | `edit /clock.eyu` |
| `run` | Script | Runs script in foreground (Core 1) | `run /app.eyu` |
| `runbg` | Script | Runs script in background (Core 0) | `runbg /clock.eyu` |
| `bglist` / `ps` | Task | Lists active background tasks | `bglist` |
| `killbg` | Task | Terminates a background task slot | `killbg 0` |
| `wifi` | Network | Wi-Fi scan / connect / status | `wifi status` |
| `ip` | Network | Prints local IP address | `ip` |
| `ping` | Network | Pings a remote host | `ping google.com` |
| `httpget` | Network | Sends HTTP GET request | `httpget http://api.com` |
| `temp` | Hardware | Reads ESP32-S3 chip temperature (°C) | `temp` |
| `sysinfo` / `info` | System | Displays CPU, RAM, PSRAM & Uptime | `sysinfo` |
| `ver` | System | Displays kernel version | `ver` |
| `reset` / `reboot` | System | Restarts ESP32-S3 MCU | `reset` |
| `clear` / `cls` | Console | Clears console screen | `clear` |
| `help` | Console | Displays command summary | `help` |
| `exit` | Console | Returns to Graphical Desktop | `exit` |

---

## 📂 File and Directory Management

### `ls` / `dir`
Lists files and directories in the specified or current directory.
* **Usage:** `ls [directory_path]`
* **Examples:** `ls /`, `ls /scripts`

### `cd`
Changes the current working directory.
* **Usage:** `cd <directory_path>`
* **Examples:** `cd /system`, `cd ..`

### `pwd`
Prints the current working directory path.

### `cat` / `type`
Displays the contents of a text file.
* **Usage:** `cat <file_path>`
* **Example:** `cat /test.eyu`

### `rm` / `del`
Deletes the specified file.
* **Usage:** `rm <file_path>`

### `mkdir`
Creates a new directory.
* **Usage:** `mkdir <directory_path>`

### `edit`
Opens the specified file in the `Eyuditor` graphical text editor.
* **Usage:** `edit <file_path>`
* **Example:** `edit /clock.eyu`

---

## 📜 Script & Task Execution

### `run`
Executes an EyuScript file in the foreground (Core 1).
* **Usage:** `run <script_path>`
* **Example:** `run /demo.eyu`

### `runbg`
Executes an EyuScript file as a background task (Core 0).
* **Usage:** `runbg <script_path>`
* **Example:** `runbg /clock.eyu`

### `bglist` / `ps`
Lists active background script task slots.
* **Sample Output:**
  ```text
  BG[0] /clock.eyu (Active)
  BG[1] Empty
  BG[2] Empty
  ```

### `killbg`
Terminates a running background script task slot.
* **Usage:** `killbg <slot_number>`
* **Example:** `killbg 0`

---

## 🌐 Network & Internet Commands

### `wifi`
Manages Wi-Fi connections and network scanning.
* **Commands:**
  - `wifi scan` — Scans nearby Wi-Fi networks and displays SSIDs with RSSI values.
  - `wifi status` — Displays current Wi-Fi status and local IP address.
  - `wifi off` / `wifi disconnect` — Disconnects from Wi-Fi.
  - `wifi <SSID> <PASSWORD>` — Connects to a Wi-Fi network (with a non-blocking spinner, max 15s timeout).

### `ip`
Prints the current Wi-Fi local IP address.

### `ping`
Sends ICMP/TCP packets to a specified target.
* **Usage:** `ping <host>`
* **Example:** `ping google.com`

### `httpget`
Sends an HTTP GET request to a specified URL and prints the response.
* **Usage:** `httpget <URL>`
* **Example:** `httpget http://worldtimeapi.org/api/ip`

---

## 🛠️ System Monitoring & Tools

### `temp`
Reads the ESP32-S3 internal chip temperature sensor (°C).

### `ver` / `version`
Displays kernel version and CPU operating frequency:
```text
EyudiOS S3 Edition - EsDOS v2.1
ESP32-S3 @ 240 MHz
```

### `sysinfo` / `info`
Displays system resource usage: CPU frequency, Flash size, available PSRAM, Heap status, and Uptime.

### `reset` / `reboot`
Restarts the ESP32-S3 microcontroller.

### `clear` / `cls`
Clears the EsDOS console screen.

### `help`
Displays a summary of available shell commands.

### `exit`
Exits the EsDOS shell and returns to the Graphical Desktop UI.
