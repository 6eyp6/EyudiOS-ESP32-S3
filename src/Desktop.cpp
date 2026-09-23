#include "Globals.h"

// ============================================================
// === MODERN DESKTOP — EyudiOS S3 Edition (400x300) ===
// ============================================================

// ---- Renk Paleti (Yeşil Tema) ----
#define D_BG        videodisplay.RGB(4,   12,  4)    // Koyu yeşilimsi siyah zemin
#define D_PANEL     videodisplay.RGB(8,   24,  10)   // Status bar zemini
#define D_ACCENT    videodisplay.RGB(0,   220, 80)   // Parlak yeşil vurgu
#define D_ACCENT2   videodisplay.RGB(0,   140, 50)   // Koyu yeşil
#define D_SEL_RING  videodisplay.RGB(80,  255, 120)  // Seçim halkası (açık yeşil)
#define D_HOV       videodisplay.RGB(15,  40,  18)   // Hover zemini
#define D_WHITE     videodisplay.RGB(220, 240, 220)  // Yeşilimsi beyaz metin
#define D_GREY      videodisplay.RGB(80,  110, 85)   // Soluk yeşil metin
#define D_SHADOW    videodisplay.RGB(2,   6,   2)    // Gölge

// ---- İkon renkleri (her uygulama için benzersiz) ----
static uint16_t iconColors[] = {
  /* 0 System  */ EYU_RGB565(40,  180, 80),
  /* 1 Eyudio  */ EYU_RGB565(0,   240, 200),
  /* 2 Setup   */ EYU_RGB565(200, 180, 40),
  /* 3 Files   */ EYU_RGB565(40,  160, 200),
  /* 4 WiFi    */ EYU_RGB565(60,  200, 255),
  /* 5 Info    */ EYU_RGB565(160, 220, 80),
  /* 6 Term    */ EYU_RGB565(0,   220, 100),
  /* 7 Code    */ EYU_RGB565(0,   255, 140),
  /* 8 Blue    */ EYU_RGB565(80,  140, 255),
  /* 9 Logs    */ EYU_RGB565(220, 80,  80),
  /* 10 Browser*/ EYU_RGB565(255, 160, 40),
  /* 11 FTP    */ EYU_RGB565(120, 80,  255),
  /* 12 Disp   */ EYU_RGB565(255, 100, 150),
  /* 13 Serv   */ EYU_RGB565(255, 150, 40),
  /* 14 Doom   */ EYU_RGB565(255, 0,   0),
  /* 15 Market */ EYU_RGB565(255, 0,   255),
};
static const int numIconColors = sizeof(iconColors) / sizeof(iconColors[0]);

// ---- İkon Sembolleri (2 karakter, büyük harf) ----
static const char* iconSymbols[] = {
  "SY", "EY", "CF", "FL",
  "NW", "?",  ">>", "<>",
  "BT", "LG", "WW", "FT",
  "DP", "SR", "DM", "MK"
};




// ============================================================
// Yardımcı: İkon hücresi orta noktası hesapla (grid moduna göre)
// ============================================================
static void getIconXY(int i, int &ix, int &iy) {
  if (desktopGridMode == 1) {
    // Large 4-col grid: 4 columns, 96px cells
    int col = i % 4;
    int row = i / 4;
    ix = 4 + col * 98;
    iy = STATUS_BAR_HEIGHT + 4 + row * 74;
  } else if (desktopGridMode == 2) {
    // Minimal list mode: single column
    ix = 4;
    iy = STATUS_BAR_HEIGHT + 2 + i * 18;
  } else {
    // Normal grid (default GRID_COLS)
    int col = i % GRID_COLS;
    int row = i / GRID_COLS;
    ix = GRID_START_X + col * ICON_CELL;
    iy = GRID_START_Y + row * (ICON_CELL + 14);
  }
}


// ============================================================
// Yardımcı: Tek İkon Çiz
// ============================================================
static void drawIcon(int i, bool selected, bool hovered) {
  int ix, iy;
  getIconXY(i, ix, iy);

  uint16_t baseColor = (i < numIconColors) ? iconColors[i] : D_ACCENT;

  // --- Gölge ---
  videodisplay.fillRoundRect(ix + 3, iy + 3, ICON_SIZE, ICON_SIZE, 8, D_SHADOW);

  // --- Ana ikon kutusu ---
  if (selected) {
    // Seçili: parlak dolgu + kalın halka
    videodisplay.fillRoundRect(ix, iy, ICON_SIZE, ICON_SIZE, 8, baseColor);
    // Dış parlak halka
    videodisplay.drawRoundRect(ix - 2, iy - 2, ICON_SIZE + 4, ICON_SIZE + 4, 10, D_SEL_RING);
    videodisplay.drawRoundRect(ix - 3, iy - 3, ICON_SIZE + 6, ICON_SIZE + 6, 11, D_ACCENT2);
  } else if (hovered) {
    // Hover: yarı saydam kutu
    videodisplay.fillRoundRect(ix, iy, ICON_SIZE, ICON_SIZE, 8, D_HOV);
    videodisplay.drawRoundRect(ix, iy, ICON_SIZE, ICON_SIZE, 8, baseColor);
  } else {
    // Normal: koyu dolgu + renkli çerçeve
    videodisplay.fillRoundRect(ix, iy, ICON_SIZE, ICON_SIZE, 8, D_BG);
    videodisplay.drawRoundRect(ix, iy, ICON_SIZE, ICON_SIZE, 8, baseColor);
    // Sol üst köşeye küçük renk şeridi (derinlik efekti)
    videodisplay.fillRoundRect(ix + 2, iy + 2, ICON_SIZE - 4, 6, 3, baseColor);
  }

  // --- Sembol ---
  const char* sym = (i < (int)(sizeof(iconSymbols)/sizeof(iconSymbols[0]))) ?
                    iconSymbols[i] : "??";
  videodisplay.setTextSize(2);
  uint16_t symColor = selected ? D_BG : (hovered ? baseColor : D_WHITE);
  videodisplay.setTextColor(symColor);
  // Simetrik ortalama: 2x font = 12px karakter genişliği, 2 karakter = 24px
  videodisplay.setCursor(ix + (ICON_SIZE - 24) / 2, iy + (ICON_SIZE - 16) / 2 + 4);
  videodisplay.print(sym);

  // --- Etiket ---
  String name = desktopItems[i].name;
  if (name.length() > 6) name = name.substring(0, 5) + ".";
  videodisplay.setTextSize(1);
  // Etiket ortalama (6px per char approx)
  int labelW = name.length() * 6;
  int labelX = ix + (ICON_SIZE - labelW) / 2;
  videodisplay.setCursor(labelX, iy + ICON_SIZE + 4);
  videodisplay.setTextColor(selected ? D_WHITE : D_GREY);
  videodisplay.print(name.c_str());
}

// ============================================================
// Status Bar  (400px genişlik, 20px yükseklik)
// Sol:  [EyudiOS S3]          boşluk 8px
// Orta: boş (yer ayrılmış)
// Sağ:  [uptime] [WiFi/NoWF] [BT/--]   her biri sabit genişlik
// ============================================================
static void drawStatusBar() {
  videodisplay.setFont(NULL);
  videodisplay.setTextSize(1);

  // Zemin
  videodisplay.fillRect(0, 0, SCREEN_WIDTH, STATUS_BAR_HEIGHT, D_PANEL);
  // Alt çizgi — line() kullanıyoruz (drawFastHLine VGA wrapper'da yok)
  videodisplay.line(0, STATUS_BAR_HEIGHT - 1, SCREEN_WIDTH - 1, STATUS_BAR_HEIGHT - 1, D_ACCENT2);

  // === SOL: Logo ===
  // "EyudiOS" = 7karakter × 6px = 42px
  videodisplay.setTextColor(D_ACCENT);
  videodisplay.setCursor(6, 6);
  videodisplay.print("EyudiOS");
  // " S3" (ayraç)
  videodisplay.setTextColor(D_GREY);
  videodisplay.setCursor(50, 6);
  videodisplay.print("S3");
  // Dikey ayraç çizgisi
  videodisplay.line(68, 3, 68, STATUS_BAR_HEIGHT - 3, D_ACCENT2);

  // === SAĞ: Durum elemanları (sabit piksel pozisyon) ====
  // Her eleman için sağdan sola: BT | WiFi | Uptime
  // BT: 2 veya 2 karakter = ~12px, sağdan 6px pay
  int x = SCREEN_WIDTH - 7;

  // BLE göstergesi ("BT" veya "--"), 12px genişlik
  videodisplay.setTextColor(ble_connected ? videodisplay.RGB(80,200,255) : D_GREY);
  x -= 12;
  videodisplay.setCursor(x, 6);
  videodisplay.print(ble_connected ? "BT" : "--");

  // Ayraç
  x -= 7;
  videodisplay.setTextColor(D_ACCENT2);
  videodisplay.setCursor(x, 6);
  videodisplay.print("|");

  // WiFi göstergesi ("Wi-Fi" veya "NoWF"), ~30px genişlik
  x -= 30;
  videodisplay.setTextColor(wifiConnected ? D_ACCENT : D_GREY);
  videodisplay.setCursor(x, 6);
  videodisplay.print(wifiConnected ? "Wi-Fi" : " NoWF");

  // Ayraç
  x -= 7;
  videodisplay.setTextColor(D_ACCENT2);
  videodisplay.setCursor(x, 6);
  videodisplay.print("|");

  // Uptime: max "9999s" = 5 karakter = 30px
  char uptimeBuf[8];
  uint32_t s = millis() / 1000;
  snprintf(uptimeBuf, sizeof(uptimeBuf), "%4us", s < 9999u ? s : 9999u);
  x -= 30;
  videodisplay.setTextColor(D_ACCENT2);
  videodisplay.setCursor(x, 6);
  videodisplay.print(uptimeBuf);
}

// ============================================================
// External Plugin Application Hooks (Weak symbols for clean extension)
// ============================================================
__attribute__((weak)) void registerExternalApps() {}
__attribute__((weak)) bool launchExternalApp(const String &appName) { (void)appName; return false; }

// ============================================================
// initializeDesktop
// ============================================================
void initializeDesktop() {
  DesktopItem items[] = {
    { "System",   "[SYS]", true, 0 },
    { "Settings", "[CFG]", true, 2 },
    { "Files",    "[DIR]", true, 3 },
    { "WiFi",     "[NET]", true, 4 },
    { "Info",     "[INF]", true, 5 },
    { "Term",     "[CMD]", true, 6 },
    { "Code",     "[IDE]", true, 7 },
    { "Blue",     "[BT]",  true, 8 },
    { "Logs",     "[LOG]", true, 9 },
    { "Browser",  "[WEB]", true, 10 },
    { "FTP",      "[FTP]", true, 11 },
    { "Display",  "[DSP]", true, 12 },
    { "Services", "[SRV]", true, 13 },
    { "Doom",     "[DM]",  true, 14 },
    { "Market",   "[MK]",  true, 15 },
  };
  int totalItems = sizeof(items) / sizeof(items[0]);
  desktopItemCount = min(totalItems, 20);
  for (int i = 0; i < desktopItemCount; i++) {
    desktopItems[i] = items[i];
  }
  registerExternalApps();
}



void renderDesktop() {
  if (showBootMenu || inMenu) return;

  // Akıllı temizleme: Desktop redraw istendiğinde (app çıkışları dahil) 
  // live wallpaper sistemini 1 kez tam temizliğe zorla.
  if (desktopNeedsRedraw && wallpaperMode != 0) {
    extern void resetWallpaperClear();
    resetWallpaperClear();
  }

  // Hangi ikona hover yapıldığını hesapla
  int newHover = -1;
  for (int i = 0; i < desktopItemCount; i++) {
    int ix, iy;
    getIconXY(i, ix, iy);
    if (mouseX >= ix - 2 && mouseX <= ix + ICON_SIZE + 2 &&
        mouseY >= iy - 2 && mouseY <= iy + ICON_SIZE + 12) {
      newHover = i;
      break;
    }
  }

  // Double buffering'de flicker'ı önlemek için her frame çizim yapılmalı
  hoveredItemIndex = newHover;

  // Arka planı ve status bar'ı çiz
  if (wallpaperMode == 0) {
    videodisplay.fillScreen(D_BG);
  } else {
    extern void renderWallpaper();
    extern void resetWallpaperClear(); // New helper
    renderWallpaper();
  }
  drawStatusBar();

  // İkonlar / Layout modu
  videodisplay.setFont(NULL);
  if (desktopGridMode == 2) {
    // ── Minimal List Mode ──────────────────────────────
    const int MAX_VIS = (SCREEN_HEIGHT - STATUS_BAR_HEIGHT - 12) / 18;
    for (int i = 0; i < desktopItemCount && i < MAX_VIS; i++) {
      int ix, iy;
      getIconXY(i, ix, iy);
      bool sel = (i == selectedDesktopIndex);
      uint16_t col = (i < numIconColors) ? iconColors[i] : D_ACCENT;
      // Row background
      if (sel) {
        videodisplay.fillRect(0, iy - 1, SCREEN_WIDTH, 16, videodisplay.RGB(0, 50, 20));
        videodisplay.setTextColor(D_WHITE);
      } else if (i == hoveredItemIndex) {
        videodisplay.fillRect(0, iy - 1, SCREEN_WIDTH, 16, videodisplay.RGB(0, 20, 8));
        videodisplay.setTextColor(col);
      } else {
        videodisplay.setTextColor(D_GREY);
      }
      // Colored bullet
      videodisplay.fillRect(ix + 2, iy + 3, 6, 6, col);
      // Arrow indicator
      if (sel) { videodisplay.setCursor(ix + 10, iy); videodisplay.setTextColor(D_ACCENT); videodisplay.print(">"); }
      // App name
      videodisplay.setCursor(ix + 20, iy);
      videodisplay.setTextColor(sel ? D_WHITE : D_GREY);
      videodisplay.print(desktopItems[i].name.c_str());
    }
  } else {
    // ── Normal / Large Grid Mode ───────────────────────
    for (int i = 0; i < desktopItemCount; i++) {
      bool selected = (i == selectedDesktopIndex);
      bool hovered = (i == hoveredItemIndex);
      if (desktopGridMode == 1) {
        // Large grid: draw big tiles (90x60px cells)
        int ix, iy;
        getIconXY(i, ix, iy);
        uint16_t baseColor = (i < numIconColors) ? iconColors[i] : D_ACCENT;
        bool sel = selected, hov = hovered;
        videodisplay.fillRoundRect(ix, iy, 92, 62, 6, sel ? baseColor : (hov ? D_HOV : D_BG));
        videodisplay.drawRoundRect(ix, iy, 92, 62, 6, sel ? D_SEL_RING : baseColor);
        if (sel) videodisplay.drawRoundRect(ix-1, iy-1, 94, 64, 7, D_ACCENT);
        // Symbol (large)
        const char* sym = (i < (int)(sizeof(iconSymbols)/sizeof(iconSymbols[0]))) ? iconSymbols[i] : "??";
        videodisplay.setTextSize(2);
        videodisplay.setTextColor(sel ? D_BG : D_WHITE);
        videodisplay.setCursor(ix + (92 - 24) / 2, iy + 14);
        videodisplay.print(sym);
        // Label (full name, up to 10 chars)
        videodisplay.setTextSize(1);
        String nm = desktopItems[i].name;
        if (nm.length() > 10) nm = nm.substring(0, 9) + ".";
        videodisplay.setTextColor(sel ? D_BG : D_GREY);
        videodisplay.setCursor(ix + (92 - nm.length()*6)/2, iy + 42);
        videodisplay.print(nm.c_str());
      } else {
        drawIcon(i, selected, hovered);
      }
    }
  }


  if (showSplash) drawSplashScreen();
  
  desktopNeedsRedraw = false;
}


// ============================================================
// handleDesktopInput
// ============================================================
void handleDesktopInput(ParsedKey key) {
  if (key.normalizedKey == "LEFT" || (key.isArrow && key.arrowDirection == 2)) {
    if (selectedDesktopIndex > 0) { selectedDesktopIndex--; desktopNeedsRedraw = true; }
  }
  else if (key.normalizedKey == "RIGHT" || (key.isArrow && key.arrowDirection == 3)) {
    if (selectedDesktopIndex < desktopItemCount - 1) { selectedDesktopIndex++; desktopNeedsRedraw = true; }
  }
  else if (key.normalizedKey == "UP" || (key.isArrow && key.arrowDirection == 0)) {
    if (selectedDesktopIndex >= GRID_COLS) { selectedDesktopIndex -= GRID_COLS; desktopNeedsRedraw = true; }
  }
  else if (key.normalizedKey == "DOWN" || (key.isArrow && key.arrowDirection == 1)) {
    if (selectedDesktopIndex + GRID_COLS < desktopItemCount) { selectedDesktopIndex += GRID_COLS; desktopNeedsRedraw = true; }
  }
  else if (key.isEnter) {
    openApplication(desktopItems[selectedDesktopIndex].name);
    desktopNeedsRedraw = true;
  }

  if (key.isEscape) {
    if (showVirtualKeyboard && !vkbActive) closeVirtualKeyboard();
    else if (focusedWindowIndex != -1) closeWindow(focusedWindowIndex);
    desktopNeedsRedraw = true;
  }

  if (leftButton && !prevLeftButton) {
    if (showVirtualKeyboard && !vkbActive) handleVirtualKeyboardClick();

    for (int i = 0; i < desktopItemCount; i++) {
      int ix, iy;
      getIconXY(i, ix, iy);
      if (mouseX >= ix - 4 && mouseX <= ix + ICON_SIZE + 4 &&
          mouseY >= iy - 4 && mouseY <= iy + ICON_SIZE + 14) {
        selectedDesktopIndex = i;
        openApplication(desktopItems[i].name);
        desktopNeedsRedraw = true;
        return;
      }
    }
  }

  // Note: window dragging and focus are handled in processUSBInput (InputManager.cpp)
}

// ============================================================
// openApplication
// ============================================================
void openApplication(String appName) {
  if (appName == "Blue") {
    enterMenu("Bluetooth");
  } else if (appName == "Settings") {
    enterMenu("Settings");
  } else if (appName == "WiFi") {
    enterMenu("WiFi");
  } else if (appName == "Logs") {
    toggleSystemLogs();
  } else if (appName == "Term") {
    // EsDOS tam ekran
    showEsdos = true;
    currentApp = "esdos";
    esdosOutputCount = 0;
    addEsdosOutput("=== EsDOS Shell v2.0 ===");
    addEsdosOutput("Merhaba! EyuScript icin 'run <dosya.eyu>' yazin.");
    addEsdosOutput("Komutlar: ls, cd, run, open, httpget, help, exit");
    addEsdosOutput("---");
    esdosCommand = "";
    esdosNeedsRedraw = true;
    staticBackgroundDrawn = false;
  } else if (launchExternalApp(appName)) {
    // App launched by external plugin module
  } else if (appName == "Code") {


    // Eyuditor tam ekran — dosya seç (şimdilik yeni boş dosya)
    openEyuditor("/untitled.eyu");
  } else if (appName == "Files") {
    // Dosya yöneticisi tam ekran
    openFileManager();
  } else if (appName == "Info") {
    createWindow("Info", "System Info", 20, 24, 360, 255);
  } else if (appName == "Browser") {
    webBrowserURL = "http://";
    webBrowserContent = "";
    createWindow("Browser", "Web Browser", 10, 24, 380, 260);
  } else if (appName == "FTP") {
    createWindow("FTP", "FTP Server", 30, 30, 340, 180);
  } else if (appName == "Display") {
    createWindow("Display", "Display Settings", 40, 40, 260, 180);
  } else if (appName == "Tasks") {
    taskManagerTabGlobal = 0;  // Always start on FreeRTOS tab
    createWindow("Tasks", "Task Manager", 2, 20, SCREEN_WIDTH - 4, SCREEN_HEIGHT - 22);

  } else if (appName == "Market") {
    scanMarketScripts();
    createWindow("Market", "EyuMarket", 40, 40, 300, 220);
  } else if (appName == "Services") {
    createWindow("Services", "Services Management", 60, 60, 240, 150);
  } else if (appName == "Doom") {
    createWindow("Doom", "EyeDoom v1.0", 5, 24, 330, 240);
    extern void openDoom(String wadPath);
    openDoom(""); // Start engine
  } else {
    // Diğer uygulamalar pencere olarak açılır
    createWindow(appName, appName, 20 + (windowCount * 12), 24 + (windowCount * 12), 260, 180);
  }
  
  // App açılırken wallpaper temizlik bayrağını resetle ki dönüşte temizlesin
  extern void resetWallpaperClear();
  resetWallpaperClear();
}


// ============================================================
// Window Manager
// ============================================================
void createWindow(String appName, String title, int x, int y, int w, int h) {
  if (windowCount >= MAX_WINDOWS) return;
  for (int i = 0; i < windowCount; i++) {
    if (strcmp(windows[i].appName, appName.c_str()) == 0) { focusWindow(i); return; }
  }
  strncpy(windows[windowCount].appName, appName.c_str(), 31);
  windows[windowCount].appName[31] = '\0';
  strncpy(windows[windowCount].title, title.c_str(), 31);
  windows[windowCount].title[31] = '\0';
  windows[windowCount].x = x; windows[windowCount].y = y;
  windows[windowCount].w = w; windows[windowCount].h = h;
  windows[windowCount].prevX = x; windows[windowCount].prevY = y;
  windows[windowCount].prevW = w; windows[windowCount].prevH = h;
  windows[windowCount].dirtyRect = false;
  windows[windowCount].focused = true; windows[windowCount].minimized = false; windows[windowCount].needsRedraw = true;

  focusWindow(windowCount);
  windowCount++;
  desktopNeedsRedraw = true;
}

void closeWindow(int index) {
  if (index < 0 || index >= windowCount) return;
  for (int i = index; i < windowCount - 1; i++) windows[i] = windows[i+1];
  windowCount--;
  if (windowCount > 0) focusWindow(windowCount - 1);
  else focusedWindowIndex = -1;
  desktopNeedsRedraw = true;
}

void focusWindow(int index) {
  if (index < 0 || index >= windowCount) return;
  for (int i = 0; i < windowCount; i++) windows[i].focused = false;
  windows[index].focused = true;
  focusedWindowIndex = index;
  windows[index].needsRedraw = true;
  desktopNeedsRedraw = true;
}

void moveWindow(int index, int dx, int dy) {
  if (index < 0 || index >= windowCount) return;
  windows[index].x = constrain(windows[index].x + dx, 0, SCREEN_WIDTH - 20);
  windows[index].y = constrain(windows[index].y + dy, STATUS_BAR_HEIGHT, SCREEN_HEIGHT - 20);
  windows[index].needsRedraw = true;
  desktopNeedsRedraw = true;
}

void cycleFocus() {
  if (windowCount <= 1) return;
  focusWindow((focusedWindowIndex + 1) % windowCount);
}

int getWindowAt(int x, int y) {
  for (int i = windowCount - 1; i >= 0; i--) {
    if (x >= windows[i].x && x <= windows[i].x + windows[i].w &&
        y >= windows[i].y && y <= windows[i].y + windows[i].h) return i;
  }
  return -1;
}

void calculateDesktopIndices() {}
