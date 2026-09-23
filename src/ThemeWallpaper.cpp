// ============================================================
// ThemeWallpaper.cpp — EyudiOS Tema & Canlı Duvar Kağıdı
// ============================================================
#include "Globals.h"
#include <esp_task_wdt.h>
#include <EEPROM.h>

// ── Tema Paleti ───────────────────────────────────────────
static const uint16_t themePalettes[THEME_COUNT][5] = {
  { EYU_RGB565(4,12,4),   EYU_RGB565(8,24,10),  EYU_RGB565(0,220,80),  EYU_RGB565(220,240,220), EYU_RGB565(80,110,85) },
  { EYU_RGB565(4,8,18),   EYU_RGB565(8,14,30),  EYU_RGB565(40,140,255), EYU_RGB565(200,220,255), EYU_RGB565(60,80,120) },
  { EYU_RGB565(12,8,0),   EYU_RGB565(22,14,0),  EYU_RGB565(255,160,0), EYU_RGB565(255,240,180), EYU_RGB565(120,90,30) },
  { EYU_RGB565(0,12,14),  EYU_RGB565(0,20,22),  EYU_RGB565(0,240,220), EYU_RGB565(180,255,250), EYU_RGB565(40,100,100) },
};

static const char* themeNames[] = { "Green Dark", "Blue Dark", "Amber Dark", "Cyan Dark" };

void applyTheme(int t) {
  currentTheme = constrain(t, 0, THEME_COUNT - 1);
  themeColorBG     = themePalettes[currentTheme][0];
  themeColorPanel  = themePalettes[currentTheme][1];
  themeColorAccent = themePalettes[currentTheme][2];
  themeColorText   = themePalettes[currentTheme][3];
  themeColorGrey   = themePalettes[currentTheme][4];
  desktopNeedsRedraw = true;
  logToFile("[THEME] " + String(themeNames[currentTheme]));
}

void saveThemeSettings() {
  saveSettings();
}

void loadThemeSettings() {
  loadSettings();
}

// ── Live Wallpaper ────────────────────────────────────────
// Matrix Rain
static int matrixCols[50];
static int matrixY[50];
static bool matrixInit = false;

static void drawMatrixRain() {
  if (!matrixInit) {
    for (int i = 0; i < 50; i++) {
      matrixCols[i] = random(0, 400);
      matrixY[i]    = random(0, 300);
    }
    matrixInit = true;
  }
  
  // Incremental: Sadece eski karakterleri sil
  for (int i = 0; i < 50; i++) {
    // Önceki izi sil (üstteki sönük karakterin yerini komple karart)
    videodisplay.fillRect(matrixCols[i], matrixY[i] - 20, 10, 12, videodisplay.RGB(0, 0, 0));
    
    // Yeni karakteri çiz
    videodisplay.setTextColor(themeColorAccent);
    videodisplay.setCursor(matrixCols[i], matrixY[i]);
    videodisplay.print((char)('!' + random(94)));
    
    if (matrixY[i] > 20) {
      videodisplay.setTextColor(videodisplay.RGB(0, 60, 0));
      videodisplay.setCursor(matrixCols[i], matrixY[i] - 10);
      videodisplay.print((char)('!' + random(94)));
    }
    
    matrixY[i] += 10;
    if (matrixY[i] > 290) { 
      // Başa dönerken o sütunu temizle
      videodisplay.fillRect(matrixCols[i], 20, 10, 280, videodisplay.RGB(0, 0, 0));
      matrixY[i] = 20; 
      matrixCols[i] = random(0, 396); 
    }
  }
}

// Starfield
static int starX[80], starY[80];
static int starSpd[80];
static bool starInit = false;

static void drawStarfield() {
  if (!starInit) {
    for (int i = 0; i < 80; i++) {
      starX[i]   = random(0, 400);
      starY[i]   = random(20, 300);
      starSpd[i] = random(1, 4);
    }
    starInit = true;
  }
  
  for (int i = 0; i < 80; i++) {
    // Eski yıldızı sil
    videodisplay.fillRect(starX[i], starY[i], starSpd[i], starSpd[i], COLOR_BACKGROUND);
    
    starX[i] -= starSpd[i];
    if (starX[i] < 0) { 
      starX[i] = 399; 
      starY[i] = random(20, 290); 
    }
    
    // Yeni yıldızı çiz
    uint8_t br = 80 + starSpd[i] * 40;
    videodisplay.fillRect(starX[i], starY[i], starSpd[i], starSpd[i],
                          videodisplay.RGB(br, br, br));
  }
}

// Plasma
static uint8_t plasmaT = 0;
static void drawPlasma() {
  for (int y = 20; y < 300; y += 4) {
    for (int x = 0; x < 400; x += 4) {
      uint8_t v = (uint8_t)(
        128 + 64 * sin((x + plasmaT) * 0.06f) +
        64  * sin((y + plasmaT * 0.7f) * 0.06f));
      uint16_t color;
      if (currentTheme == 0) color = videodisplay.RGB(0, v >> 1, v >> 2);
      else if (currentTheme == 1) color = videodisplay.RGB(0, v >> 2, v >> 1);
      else if (currentTheme == 2) color = videodisplay.RGB(v >> 1, v >> 2, 0);
      else color = videodisplay.RGB(0, v >> 1, v >> 1);
      videodisplay.fillRect(x, y, 4, 4, color);
    }
  }
  plasmaT += 3;
}

// Rain Drops (Mode 4)
static int rainX[30], rainY[30], rainLen[30];
static bool rainInit = false;
static void drawRainDrops() {
  if (!rainInit) {
    for (int i = 0; i < 30; i++) {
      rainX[i] = random(0, 400); rainY[i] = random(20, 300); rainLen[i] = random(5, 20);
    }
    rainInit = true;
  }
  // Fade background with semi-transparent fill
  videodisplay.fillRect(0, 20, 400, 280, themeColorBG);
  for (int i = 0; i < 30; i++) {
    uint16_t c = themeColorAccent;
    // Draw a raindrop streak
    for (int j = 0; j < rainLen[i]; j++) {
      int py = rainY[i] - j;
      if (py >= 20 && py < 300) {
        uint8_t alpha = 255 - (j * 255 / rainLen[i]);
        uint8_t r = (alpha * ((c >> 11) & 0x1F) * 8) / 255;
        uint8_t g = (alpha * ((c >> 5) & 0x3F) * 4) / 255;
        uint8_t b = (alpha * (c & 0x1F) * 8) / 255;
        videodisplay.drawPixel(rainX[i], py, videodisplay.RGB(r, g, b));
      }
    }
    rainY[i] += 4;
    if (rainY[i] > 300) {
      rainY[i] = 20; rainX[i] = random(0, 400);
      rainLen[i] = random(5, 25);
    }
  }
}

// ── CUSTOM BMP WALLPAPER (Mode 5) ──────────────────────────
bool loadWallpaperBMP(const char* path) {
  if (!sdCardPresent || !path) return false;
  
  if (xSemaphoreTake(sdMutex, pdMS_TO_TICKS(5000)) != pdTRUE) return false;
  
  wallpaperBufferLoaded = false; // Disable rendering while loading
  
  if (!wallpaperBuffer) {
    wallpaperBuffer = (uint8_t*)heap_caps_malloc(400 * 300, MALLOC_CAP_SPIRAM);
    if (!wallpaperBuffer) { xSemaphoreGive(sdMutex); return false; }
  }
  
  File f = SD.open(path);
  if (!f) { xSemaphoreGive(sdMutex); return false; }
  
  f.seek(18);
  int32_t w, h;
  f.read((uint8_t*)&w, 4); f.read((uint8_t*)&h, 4);
  f.seek(10);
  int32_t offset;
  f.read((uint8_t*)&offset, 4);
  f.seek(offset);
  
  int absW = abs(w), absH = abs(h);
  size_t rowSize = (absW * 3 + 3) & ~3;
  uint8_t* lineBuf = (uint8_t*)malloc(rowSize);
  if (!lineBuf) { f.close(); return false; }
  
  for (int j = 0; j < 300 && j < absH; j++) {
    if (f.read(lineBuf, rowSize) != rowSize) break;
    int py = (h > 0) ? (300 - 1 - j) : j;
    if (py >= 0 && py < 300) {
      for (int i = 0; i < 400 && i < absW; i++) {
        uint8_t b = lineBuf[i*3], g = lineBuf[i*3+1], r = lineBuf[i*3+2];
        wallpaperBuffer[py * 400 + i] = videodisplay.base.rgb(r, g, b);
      }
    }
    if (j % 32 == 0) { yield(); vTaskDelay(1); }
  }
  free(lineBuf);
  f.close();
  xSemaphoreGive(sdMutex);
  wallpaperBufferLoaded = true;
  wallpaperMode = 5;
  desktopNeedsRedraw = true;
  return true;
}

static void drawWallpaperBuffer() {
  if (!wallpaperBuffer || !wallpaperBufferLoaded) return;
  uint8_t* fb = (uint8_t*)videodisplay.base.backBuffer;
  if (fb) {
    // 20. pixelden sonrası masaüstü (status bar hariç)
    // 400x280'lik kısmı kopyala
    for (int y = 20; y < 300; y++) {
      memcpy(fb + y * 400, wallpaperBuffer + y * 400, 400);
    }
  }
}

static bool wallpaperClearPending = false;
static int lastWallpaperMode = -1;

void resetWallpaperClear() {
  wallpaperClearPending = true;
}

void renderWallpaper() {
  if (wallpaperMode == 0) return;

  if (windowCount > 0) {
    // Draw static background when windows are open to prevent glitches and save CPU
    videodisplay.fillRect(0, STATUS_BAR_HEIGHT, SCREEN_WIDTH, SCREEN_HEIGHT - STATUS_BAR_HEIGHT, themeColorBG);
    return;
  }

  // Clear desktop background once when requested or when wallpaper mode changes
  if (wallpaperClearPending || lastWallpaperMode != wallpaperMode) {
    videodisplay.fillRect(0, STATUS_BAR_HEIGHT, SCREEN_WIDTH, SCREEN_HEIGHT - STATUS_BAR_HEIGHT, themeColorBG);
    wallpaperClearPending = false;
    lastWallpaperMode = wallpaperMode;
  }

  unsigned long now = millis();
  int interval = (wallpaperMode == 3) ? 80 : 120;
  if (now - wallpaperLastUpdate < (unsigned long)interval) return;
  wallpaperLastUpdate = now;

  // Mutex koruma ile çiz
  if (!systemMutex || xSemaphoreTakeRecursive(systemMutex, pdMS_TO_TICKS(10)) != pdTRUE) return;

  videodisplay.setFont(NULL);
  videodisplay.setTextSize(1);
  switch (wallpaperMode) {
    case 1: drawMatrixRain(); break;
    case 2: drawStarfield();  break;
    case 3: drawPlasma();     break; // Plasma hala full redraw gerektirir
    case 4: drawRainDrops();  break;
    case 5: drawWallpaperBuffer(); break;
  }
  xSemaphoreGiveRecursive(systemMutex);
}



