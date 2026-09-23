#ifndef SYSTEM_CONFIG_H
#define SYSTEM_CONFIG_H

#include <Arduino.h>
#include <ESP32S3VGA.h>
#include <Adafruit_GFX.h>
#include <Fonts/FreeSans9pt7b.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_system.h>
#include <esp_heap_caps.h>
#include <SD.h>
static fs::SDFS& RealSD = SD;
#include "EyuFS.h"
#ifdef File
#undef File
#endif
#define File EyuFile
#define SD EyuFS

#include <SPI.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <EEPROM.h>
#include <math.h>
#include <Adafruit_NeoPixel.h>

// ============================================================
// === PİN TANIMLARI (GRUPLANMIŞ) ===
// ============================================================
// VGA Grubu (Sol Üst)
const int redPin   = 4;
const int greenPin = 5;
const int bluePin  = 6;
const int hsyncPin = 7;
const int vsyncPin = 15;

// SD Kart Grubu (Sol Alt - Blok)
#define SD_CS   10
#define SD_SCK  12
#define SD_MISO 13
#define SD_MOSI 11

// CH375 USB Host Grubu
#define CH375_RX 3
#define CH375_TX 46


// Diğer Donanımlar (Sağ Taraf)
#define VBUS_ENABLE_PIN 21
#define AUDIO_PDM_PIN   18
#define NEOPIXEL_PIN    48
#define NEOPIXEL_COUNT  1

// ============================================================
// === APP SABİTLERİ ===
// ============================================================
#define APP_NONE -1
#define APP_FILEMANAGER 0
#define APP_SETTINGS 1
#define APP_NETWORK 2
#define APP_SCRIPTS 3
#define APP_CALC 4
#define APP_DISPLAY 5
#define APP_TASKS 6
#define APP_MARKET 8
#define APP_SERVICES 7

// ============================================================
// === UI SABİTLERİ ===
// ============================================================
extern int SCREEN_WIDTH;
extern int SCREEN_HEIGHT;
#define DEFAULT_SCREEN_WIDTH 400
#define DEFAULT_SCREEN_HEIGHT 300
const int STATUS_BAR_HEIGHT = 20;
#define DESKTOP_HEIGHT (SCREEN_HEIGHT - STATUS_BAR_HEIGHT)
const int ICON_SIZE = 44;
const int ICON_GAP = 8;
const int ICON_CELL = ICON_SIZE + ICON_GAP;   // 52px
const int GRID_COLS = 5;
const int GRID_ROWS = 4;
#define GRID_START_X ((SCREEN_WIDTH - GRID_COLS * ICON_CELL) / 2)
#define GRID_START_Y (STATUS_BAR_HEIGHT + 10)
const int MAX_VISIBLE_COLUMNS = GRID_COLS;
const int MAX_VISIBLE_ROWS = GRID_ROWS;
const int MAX_VISIBLE_ICONS = MAX_VISIBLE_COLUMNS * MAX_VISIBLE_ROWS;
// Legacy aliases kept for compatibility
#define ICON_START_X GRID_START_X
#define ICON_START_Y GRID_START_Y

// File Manager
#define FILEMANAGER_X 30
#define FILEMANAGER_Y 40
#define FILEMANAGER_WIDTH 340
#define FILEMANAGER_HEIGHT 220
#define LINE_HEIGHT 18

// Virtual Keyboard
#define KEYBOARD_WIDTH 280
#define KEYBOARD_HEIGHT 90
#define KEY_ROW_HEIGHT 18
#define KEY_CHAR_WIDTH 14

#ifndef EYU_RGB565
#define EYU_RGB565(r, g, b) (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3))
#endif

// ============================================================
// === RENK SABİTLERİ ===
// ============================================================
// Dark Green Background
const uint16_t COLOR_BACKGROUND = EYU_RGB565(0, 64, 0);
// 0x001E32 -> R=0, G=30, B=50 -> 0x00E6
const uint16_t COLOR_STATUS_BAR = EYU_RGB565(0, 30, 50);
// 0x002840 -> R=0, G=40, B=64 -> 0x0148
const uint16_t COLOR_ICON_BG = EYU_RGB565(0, 40, 64);
const uint16_t COLOR_ICON_SELECTED = EYU_RGB565(0, 0, 0);
// 0x0066FF -> R=0, G=102, B=255 -> 0x033F
const uint16_t COLOR_ICON_BORDER = EYU_RGB565(0, 102, 255);
const uint16_t COLOR_ICON_SELECTED_BORDER = EYU_RGB565(255, 255, 255);
const uint16_t COLOR_ICON_HOVER = EYU_RGB565(0, 255, 0);
const uint16_t COLOR_ICON_CURRENT = EYU_RGB565(0, 255, 255);
const uint16_t COLOR_ICON_SELECTED_RED = EYU_RGB565(255, 0, 0);
const uint16_t COLOR_TEXT = EYU_RGB565(255, 255, 255);
// 0xCCCCCC -> R=204, G=204, B=2014 -> 0xCE79
const uint16_t COLOR_TEXT_LIGHT = EYU_RGB565(204, 204, 204);
// 0x00AFFF -> R=0, G=175, B=255 -> 0x057F
const uint16_t COLOR_TITLE = EYU_RGB565(0, 175, 255);
const uint16_t COLOR_ACCENT = EYU_RGB565(0, 200, 255);
const uint16_t COLOR_TEXT_GREY = EYU_RGB565(150, 150, 150);

// LED renk kodları
#define EYU_LED_OFF       0x000000
#define EYU_LED_RED       0xFF0000
#define EYU_LED_YELLOW    0xFFFF00
#define EYU_LED_BLUE      0x0000FF
#define EYU_LED_MAGENTA   0xFF00FF
#define EYU_LED_GREEN     0x00FF00
#define EYU_LED_ORANGE    0xFF8000
#define EYU_LED_CYAN      0x00FFFF

// ============================================================
// === MENU GEOMETRY (Responsive) ===
// ============================================================
#define MENU_W (SCREEN_WIDTH > 400 ? 280 : (SCREEN_WIDTH - 40))
#define MENU_H (SCREEN_HEIGHT > 300 ? 220 : (SCREEN_HEIGHT - 60))
#define MENU_X ((SCREEN_WIDTH - MENU_W) / 2)
#define MENU_Y ((SCREEN_HEIGHT - MENU_H) / 2)
#define MENU_ITEM_Y0 (MENU_Y + 42)
#define MENU_ITEM_H 22

#endif
