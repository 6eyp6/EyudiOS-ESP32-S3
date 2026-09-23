#include "Globals.h"
#include <soc/lcd_cam_struct.h>
#include <esp_private/gdma.h>

// --- S3VGAWrapper Implementation ---
bool S3VGAWrapper::init(const Mode &m, int rPin, int gPin, int bPin, int hsPin, int vsPin) {
  PinConfig pins(-1, -1, -1, -1, rPin, -1, -1, -1, -1, -1, gPin, -1, -1, -1, -1, bPin, hsPin, vsPin);
  
  base.bufferCount = useDoubleBuffering ? 2 : 1;
  base.usePsram    = true; 
  
  bool ok = base.init(pins, m, 8);
  if (ok) base.start();
  
  SCREEN_WIDTH = m.hRes;
  SCREEN_HEIGHT = m.vRes;
  
  return ok;
}

void changeResolution(int modeIndex, bool doubleBuffer) {
  xSemaphoreTakeRecursive(systemMutex, portMAX_DELAY);
  
  useDoubleBuffering = doubleBuffer;
  currentResolutionIndex = modeIndex;
  
  const Mode* modes[] = { 
    &Mode::MODE_320x200x70, &Mode::MODE_320x240x60, 
    &Mode::MODE_400x300x60, &Mode::MODE_640x400x70,
    &Mode::MODE_640x480x60, &Mode::MODE_800x600x56,
    &Mode::MODE_800x600x60, &Mode::MODE_1024x768x60
  };
  if (modeIndex < 0 || modeIndex > 7) modeIndex = 2; // Default 400x300
  
  const Mode& m = *modes[modeIndex];
  
  // Stop current VGA
  LCD_CAM.lcd_user.lcd_start = 0;
  LCD_CAM.lcd_user.lcd_update = 1;
  
  // Free old buffer manually to prevent PSRAM leak
  if (videodisplay.base.dmaBuffer != nullptr) {
    uint32_t before = ESP.getFreePsram();
    delete videodisplay.base.dmaBuffer;
    videodisplay.base.dmaBuffer = nullptr;
    logToFile("[SYSTEM] PSRAM Released: " + String((ESP.getFreePsram() - before) / 1024) + " KB");
  }

  // Release the DMA channel to prevent leak
  if (videodisplay.base.dmaChannel != 0) {
    gdma_del_channel((gdma_channel_handle_t)videodisplay.base.dmaChannel);
    videodisplay.base.dmaChannel = 0;
  }
  
  vTaskDelay(pdMS_TO_TICKS(100)); // Stability delay
  
  videodisplay.init(m, redPin, greenPin, bluePin, hsyncPin, vsyncPin);
  
  // Ekranı temizle
  videodisplay.clear(COLOR_BACKGROUND);
  videodisplay.show();
  
  xSemaphoreGiveRecursive(systemMutex);
  logToFile("[SYSTEM] Resolution changed to " + String(m.hRes) + "x" + String(m.vRes) + " (DB:" + String(doubleBuffer) + ")");
}


void S3VGAWrapper::clear(uint16_t color) {
  // Decode RGB565 to components and recode to native via base.rgb
  base.clear(base.rgb((color >> 8) & 0xF8, (color >> 3) & 0xFC, (color << 3) & 0xF8));
}

uint16_t S3VGAWrapper::RGB(uint8_t r, uint8_t g, uint8_t b) {
  // Always return 16-bit RGB565 for Adafruit_GFX consistency
  return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}

// --- Status LED Functions ---
void setStatusLED(uint32_t color) {
  statusLED.setPixelColor(0, color);
  statusLED.show();
}

void turnOffStatusLED() {
  setStatusLED(EYU_LED_OFF);
}

void blinkStatusLED(uint32_t color, int times, int delayMs) {
  for (int i = 0; i < times; i++) {
    setStatusLED(color);
    delay(delayMs);
    turnOffStatusLED();
    delay(delayMs);
  }
}

void showStartupPhase(const char* phaseName, uint32_t color) {
  setStatusLED(color);
  logToFile(String("[STARTUP] ") + phaseName);
}

// --- Common UI Drawing ---
void drawRect(int x, int y, int width, int height, uint16_t color) {
  videodisplay.line(x, y, x + width - 1, y, color);
  videodisplay.line(x, y + height - 1, x + width - 1, y + height - 1, color);
  videodisplay.line(x, y, x, y + height - 1, color);
  videodisplay.line(x + width - 1, y, x + width - 1, y + height - 1, color);
}

void drawCursor() {
  if (lastMouseActivity == 0 || (millis() - lastMouseActivity > 5000)) return;

  // Arrow cursor colors
  uint16_t tip, body;
  if      (leftButton)   { tip = videodisplay.RGB(255, 255, 255); body = videodisplay.RGB(220, 80, 80); }
  else if (middleButton) { tip = videodisplay.RGB(255, 255, 255); body = videodisplay.RGB(220, 200, 60); }
  else if (rightButton)  { tip = videodisplay.RGB(255, 255, 255); body = videodisplay.RGB(80, 220, 100); }
  else                   { tip = videodisplay.RGB(255, 255, 255); body = videodisplay.RGB(40, 220, 100); }

  int x = mouseX, y = mouseY;
  // Draw simple arrow: diagonal left edge + fill
  // Row 0: 1px tip
  videodisplay.drawPixel(x,   y,   tip);
  // Rows 1-4: widening body
  videodisplay.drawPixel(x,   y+1, body); videodisplay.drawPixel(x+1, y+1, tip);
  videodisplay.drawPixel(x,   y+2, body); videodisplay.drawPixel(x+1, y+2, body); videodisplay.drawPixel(x+2, y+2, tip);
  videodisplay.drawPixel(x,   y+3, body); videodisplay.drawPixel(x+1, y+3, body); videodisplay.drawPixel(x+2, y+3, body); videodisplay.drawPixel(x+3, y+3, tip);
  // Row 4-6: start narrowing (pointer tip)
  videodisplay.drawPixel(x,   y+4, body); videodisplay.drawPixel(x+1, y+4, body); videodisplay.drawPixel(x+2, y+4, tip);
  videodisplay.drawPixel(x,   y+5, body); videodisplay.drawPixel(x+2, y+5, tip); videodisplay.drawPixel(x+3, y+5, tip);
  videodisplay.drawPixel(x,   y+6, body); videodisplay.drawPixel(x+3, y+6, tip); videodisplay.drawPixel(x+4, y+6, tip);

  oldMouseX = x; oldMouseY = y;
}

void clearCursorArea() {
  // Double buffering'de ghost silmeye gerek yok, her kare temiz basılıyor.
}


