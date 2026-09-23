#include "Globals.h"
#include "EyudioWiFiSync.h"
#include "EyudioFirebase.h"
#include <esp_task_wdt.h>

#include <Fonts/FreeSans9pt7b.h>
#include "CH375Driver.h"


// Boot screen state
bool bootComplete = false;
String bootLogs[6] = {"", "", "", "", "", ""};

void drawDoomLoadingScreen() {
  if (!doomLoadingActive) return;
  
  static uint32_t lastFrame = 0;
  static int spinnerFrame = 0;
  static bool showLoading = true;
  uint32_t now = millis();
  
  if (now - lastFrame > 300) {
    lastFrame = now;
    spinnerFrame = (spinnerFrame + 1) % 4;
    showLoading = !showLoading;
  }
  
  videodisplay.fillScreen(videodisplay.RGB(10, 10, 30));
  
  if (showLoading) {
    videodisplay.setTextSize(2);
    videodisplay.setTextColor(videodisplay.RGB(0, 200, 255));
    videodisplay.setCursor(100, 80);
    videodisplay.print("DOOM");
    
    videodisplay.setTextSize(1);
    videodisplay.setTextColor(videodisplay.RGB(255, 255, 255));
    videodisplay.setCursor(90, 110);
    
    const char* dots[] = {".", "..", "...", "...."};
    videodisplay.print("Loading");
    videodisplay.print(dots[spinnerFrame]);
  }
  
  videodisplay.setTextColor(videodisplay.RGB(200, 200, 200));
  videodisplay.setCursor(80, 130);
  videodisplay.print(doomLoadingText);
  
  int barW = 240;
  int barH = 12;
  int barX = 80;
  int barY = 150;
  
  videodisplay.drawRect(barX, barY, barW, barH, videodisplay.RGB(100, 100, 100));
  int fillW = (barW - 2) * doomLoadingProgress / 100;
  if (fillW > 0) {
    videodisplay.fillRect(barX + 1, barY + 1, fillW, barH - 2, videodisplay.RGB(0, 200, 100));
  }
  
  videodisplay.setTextColor(videodisplay.RGB(180, 180, 180));
  videodisplay.setCursor(180, 170);
  videodisplay.printf("%d%%", doomLoadingProgress);
  
  videodisplay.show();
}

// Draw boot screen
void drawBootScreen() {
  videodisplay.fillRect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, COLOR_BACKGROUND);
  
  // Title
  videodisplay.setTextSize(2);
  videodisplay.setTextColor(COLOR_TITLE);
  videodisplay.setCursor(80, 25);
  videodisplay.print("EyudiOS S3");
  
  // Version
  videodisplay.setTextSize(1);
  videodisplay.setTextColor(COLOR_TEXT_LIGHT);
  videodisplay.setCursor(110, 45);
  videodisplay.print("v1.0 (Log Mode)");
  
  // Status box
  videodisplay.fillRect(20, 65, 280, 140, COLOR_ICON_BG);
  drawRect(20, 65, 280, 140, COLOR_ICON_BORDER);
  
  videodisplay.setTextColor(COLOR_TEXT);
  videodisplay.setCursor(30, 75);
  videodisplay.print("System Boot Log:");
  
  // Initial draw of existing logs
  for(int i = 0; i < 6; i++) {
    if(bootLogs[i] != "") {
      videodisplay.setCursor(30, 95 + (i * 18));
      videodisplay.setTextColor(COLOR_TITLE);
      videodisplay.print(bootLogs[i].c_str());
    }
  }
}

void updateBootStatus(String status) {
  if (systemMutex && xSemaphoreTakeRecursive(systemMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
    // Shift logs up
    for(int i = 0; i < 5; i++) bootLogs[i] = bootLogs[i+1];
    bootLogs[5] = "> " + status;

    if (!bootComplete && vgaInitialized) {
      // Only clear the log area inside the box
      videodisplay.fillRect(21, 90, 278, 114, COLOR_ICON_BG);
      videodisplay.setTextSize(1);
      for(int i = 0; i < 6; i++) {
        if(bootLogs[i] != "") {
          videodisplay.setCursor(30, 95 + (i * 18));
          videodisplay.setTextColor(COLOR_TITLE);
          videodisplay.print(bootLogs[i].c_str());
        }
      }
      videodisplay.show();
    }
    xSemaphoreGiveRecursive(systemMutex);
  }
}

void updateSystemInfo() {
  freeHeap = ESP.getFreeHeap();
  minFreeHeap = ESP.getMinFreeHeap();
  maxAllocHeap = ESP.getMaxAllocHeap();
  totalHeap = ESP.getHeapSize();
  freePsram = ESP.getFreePsram();
  totalPsram = ESP.getPsramSize();
  cpuFreq = ESP.getCpuFreqMHz();
  uptime = millis() / 1000;
  
  esp_chip_info_t chip_info;
  esp_chip_info(&chip_info);
  chipModel = chip_info.model;
  chipRevision = chip_info.revision;
  chipCores = chip_info.cores;
}

void systemUpdateTask(void* pvParameters) {
  while(1) {
    updateSystemInfo();
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}

#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"

bool initSDCardAutoFreq() {
  uint32_t freqs[] = { 40000000, 30000000, 25000000, 20000000, 16000000 };
  pinMode(SD_MISO, INPUT_PULLUP);
  pinMode(SD_CS, OUTPUT);
  digitalWrite(SD_CS, HIGH);

  for (uint32_t freq : freqs) {
    delay(20);
    SPI.end();
    delay(20);
    SPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
    delay(20);
    if (SD.begin(SD_CS, SPI, freq)) {
      sdCardPresent = true;
      setStatusLED(EYU_LED_GREEN);
      Serial.printf("[SD] BAGLANDI! Maksimum Otomatik Frekans: %d MHz (%u Hz)\n", freq / 1000000, freq);
      return true;
    }
  }
  sdCardPresent = false;
  Serial.println("[SD] BAGLANAMADI! Tum frekanslar basarisiz oldu.");
  return false;
}

void setup() {
  systemMutex = xSemaphoreCreateRecursiveMutex();
  sdMutex     = xSemaphoreCreateBinary();
  xSemaphoreGive(sdMutex);   // Başlangıçta alınabilir
  ipcQueue    = xQueueCreate(16, sizeof(IpcMsg));
  // BG task slotlarını sıfırla
  for (int i = 0; i < MAX_BG_TASKS; i++) {
    bgTasks[i].active     = false;
    bgTasks[i].handle     = NULL;
    bgTasks[i].msgPending = false;
  }
  Serial.begin(115200);   // USB Debug Port
  allocatePSRAMGlobals();
  
  // Arduino UART USB Host Port (RX=3, TX=46)
  Serial1.begin(115200, SERIAL_8N1, 3, 46);
  
  delay(200);
  EEPROM.begin(512);
  loadSettings();
  pinMode(buzzerPin, OUTPUT);
  digitalWrite(buzzerPin, LOW);

  // CRITICAL: Detach pins (10, 11, 12, 13, 14, 15) so they can be used for SD and VGA
  // USB pins (19, 20) will be managed by the USB Host driver
  for(int p : {10, 11, 12, 13, 14, 15}) {
      gpio_reset_pin((gpio_num_t)p);
  }
  
  // Initialize LED
  statusLED.begin();
  statusLED.setBrightness(50);
  setStatusLED(EYU_LED_RED);

  // 1. SD Card Init (Auto detect 40MHz -> 16MHz)
  Serial.println("[SD] SPI Otomatik Frekans Taramasi (40MHz -> 16MHz)...");
  if (!initSDCardAutoFreq()) {
    Serial.println("[SD] USB Host deneniyor...");
    CH375FS.begin(CH375_RX, CH375_TX);
    
    int retries = 5;
    usbPresent = false;
    while (retries > 0) {
      if (CH375FS.mount()) {
        usbPresent = true;
        setStatusLED(EYU_LED_CYAN);
        Serial.println("[USB] CH375 USB Bellek Algilandi ve Monte Edildi!");
        break;
      }
      Serial.println("[USB] Leonardo hazir degil, tekrar deneniyor...");
      delay(1000);
      retries--;
    }
    if (!usbPresent) {
      Serial.println("[USB] CH375 USB Bellek Algilanamadi!");
    }
  }

  // Load JSON settings if SD card or USB is mounted
  if (sdCardPresent || usbPresent) {
    loadSettings();
  }

  // --- 2. VGA Init ---
  Serial.println("[VGA] Baslatiliyor...");
  esp_task_wdt_deinit();
  disableLoopWDT();
  initSprites();
  
  if (videodisplay.init(Mode::MODE_400x300x60, redPin, greenPin, bluePin, hsyncPin, vsyncPin)) {
    vgaInitialized = true;
    videodisplay.setFont(&FreeSans9pt7b);
    drawBootScreen();
    if (!sdCardPresent && !usbPresent) {
      showSystemError("Depolama Hatasi", "Sistem SD kart veya USB bellek algilayamadi.\nLutfen baglantiyi kontrol edin.");
    }
    videodisplay.show();
    updateBootStatus("VGA Ready");
    if(sdCardPresent) updateBootStatus("SD Card found!"); else updateBootStatus("No SD Card");
  } else {
    Serial.println("[VGA] Baslatma HATASI!");
  }
  
  setStatusLED(EYU_LED_YELLOW);

  // 3b. USB VBUS Power Enable (CRITICAL for some S3 boards)
#ifdef VBUS_ENABLE_PIN
  Serial.printf("[USB] Enabling VBUS Power (Pin %d)...\n", VBUS_ENABLE_PIN);
  pinMode(VBUS_ENABLE_PIN, OUTPUT);
  digitalWrite(VBUS_ENABLE_PIN, HIGH); 
  delay(300); // Increased to 300ms for even better stability
#endif

  // 4. USB & Bluetooth Drivers
  initUSBHost();
  initBluetoothHost();
  
  initializeDesktop();
  
  loadWiFiCredentials();
  if (wifiAutoConnect && wifiSSID.length() > 0) {
    Serial.println("[WIFI] Otomatik baglaniyor...");
    connectToWiFi();
  }
  
  // Load Theme & Wallpaper
  extern void loadThemeSettings();
  loadThemeSettings();

  lastFrameTime = millis();
  
  // Transition: Clear both buffers from boot screen artifacts
  if (vgaInitialized) {
    if (systemMutex && xSemaphoreTakeRecursive(systemMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
      videodisplay.fillScreen(themeColorBG); videodisplay.show();
      videodisplay.fillScreen(themeColorBG); videodisplay.show();
      xSemaphoreGiveRecursive(systemMutex);
    }
  }

  bootComplete = true; 

  // ── Autorun Scripts (/autorun/ → foreground, /autorun/bg/ → background)
  if (sdCardPresent) {
    // BG scripts: /autorun/bg/*.bgeyu or *.eyu  -- run in background
    if (SD.exists("/autorun/bg")) {
      File bgDir = SD.open("/autorun/bg");
      if (bgDir && bgDir.isDirectory()) {
        updateBootStatus("BG autorun...");
        File f = bgDir.openNextFile();
        while (f) {
        String name = String(f.name());
        if (!f.isDirectory() && (name.endsWith(".eyu") || name.endsWith(".bgeyu"))) {
          String fullPath = "/autorun/bg/" + name.substring(name.lastIndexOf('/') + 1);
          int slot = runBgScript(fullPath);
          Serial.printf("[AUTORUN-BG] %s -> slot %d\n", fullPath.c_str(), slot);
        }
        f = bgDir.openNextFile();
      }
      bgDir.close();
    }
  }
    // FG script: /autorun/startup.eyu runs in foreground (one file)
    delay(200);
    if (SD.exists("/autorun/startup.eyu")) {
      updateBootStatus("Startup script...");
      delay(100);
      runEyuScript("/autorun/startup.eyu");
    }
  }
}

void loop() {
  if (gsodPending) {
    gsodPending = false;
    showSystemError(gsodTitle, gsodMessage);
  }

  if (systemInErrorState) {
    esp_task_wdt_reset();
    vTaskDelay(pdMS_TO_TICKS(100));
    return;
  }
  
  unsigned long currentTime = millis();
  
  // 0. Seri porttan gelen UART paketlerini oku (Serial1: GPIO 3/46) - Non-Blocking
  static String serialBuffer = "";
  while (Serial1.available()) {
    char c = Serial1.read();
    if (c == '\n') {
      serialBuffer.trim();
      if (serialBuffer.length() > 0) {
        if (serialBuffer.startsWith("UR,")) {
          lastUsbResponse = serialBuffer;
          usbResponseReady = true;
        } else {
          processSerialInput(serialBuffer);
        }
      }
      serialBuffer = "";
    } else if (c != '\r') {
      serialBuffer += c;
      if (serialBuffer.length() > 256) {
        serialBuffer = ""; // Safety reset
      }
    }
  }

  // 1. Girdileri işle (Mouse, Klavye, BLE)
  processUSBInput();
  eyudioSync.update();
  eyudioFirebase.update();

  static uint32_t lastSystemDiagPrint = 0;
  if (debugMode && (currentTime - lastSystemDiagPrint > 10000)) {
    lastSystemDiagPrint = currentTime;
    logDebugf("📊 [DEBUG-SYS] Free Heap: %u KB | Min Free Heap: %u KB | Free PSRAM: %u KB | Wi-Fi: %s\n",
              ESP.getFreeHeap() / 1024,
              ESP.getMinFreeHeap() / 1024,
              ESP.getFreePsram() / 1024,
              wifiConnected ? "CONNECTED" : "OFFLINE");
  }



  // 1b. BG → Foreground IPC mesajlarını işle
  IpcMsg msg;
  while (ipcQueue && xQueueReceive(ipcQueue, &msg, 0) == pdTRUE) {
    setScriptVariableByName(String(msg.key), String(msg.val));
  }
  // Tamamlanmış BG task slotlarını temizle
  for (int i = 0; i < MAX_BG_TASKS; i++) {
    if (bgTasks[i].active && bgTasks[i].handle == NULL) {
      bgTasks[i].active = false;
    }
  }
  // Browser yükleme animasyonu için sürekli yenile
  if (webBrowserLoading) desktopNeedsRedraw = true;


  // 2. Sistemi çiz (Double Buffering için merkezi kontrol)
  if (systemMutex && xSemaphoreTakeRecursive(systemMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
    // Eğer script çalışıyorsa veya BMP önizleme açıksa tüm UI devre dışı
    if (runningScript || bmpPreviewActive) {
        // Do NOT call show() here; the script engine handles its own show()
        // Calling it here causes buffer flipping mid-render and deadlocks.
        esp_task_wdt_reset();
        vTaskDelay(pdMS_TO_TICKS(10)); // Be nice to the script task
    } else if (inputDriverWaitingForFlash) {
        drawInputDriverWaitingScreen();
        videodisplay.show();
    } else if (showBootMenu) {
        renderBootMenu();
    } else if (inMenu) {
        renderMenu();
    } else if (showEyuditor) {
        drawEyuditor();
        drawCursor();
        eyuditorNeedsRedraw = false;
        videodisplay.show();
    } else if (showFileManager) {
        drawFileManager();
        drawCursor();
        videodisplay.show();
    } else if (showEsdos) {
        drawEsdos();
        drawCursor();
        videodisplay.show();
    } else if (showVirtualKeyboard) {
        renderDesktop();
        if (windowCount > 0) renderApplication();
        drawVirtualKeyboard();
        drawCursor();
        videodisplay.show();
    } else if (showDoom) {
        // Emergency exit: Shift+Backspace on keyboard, or SELECT+START on gamepad
        if (((heldKeys[0xE1] || heldKeys[0xE5]) && heldKeys[0x2A]) || (gamepadButtons[0][12] && gamepadButtons[0][13])) {
            extern void closeDoom();
            closeDoom();
        }
        static uint32_t lastVgaFPSPrint = 0;
        static int vgaFrameCount = 0;
        if (doomLoadingActive) {
            drawDoomLoadingScreen();
        } else if (doomFrameReady && doomFrameBuffer) {
            int b = videodisplay.base.backBuffer;
            if (videodisplay.base.dmaBuffer) {
                for (int y = 0; y < 300; y++) {
                    uint8_t* lineDest = videodisplay.base.dmaBuffer->getLineAddr8(y, b);
                    if (lineDest) {
                        memcpy(lineDest, doomFrameBuffer + y * 400, 400);
                    }
                }
                videodisplay.base.dmaBuffer->flush(b);
                videodisplay.show();
            }
            vgaFrameCount++;
            doomFrameReady = false;
        }
        
        if (millis() - lastVgaFPSPrint > 1000) {
            float fps = vgaFrameCount * 1000.0f / (millis() - lastVgaFPSPrint);
            Serial.printf("[DOOM-DEBUG] VGA Copy Loop FPS: %.2f\n", fps);
            vgaFrameCount = 0;
            lastVgaFPSPrint = millis();
        }
        renderDoomFrame();
    } else {
        renderDesktop();
        if (windowCount > 0) renderApplication();
        drawCursor();
        videodisplay.show();
    }
    xSemaphoreGiveRecursive(systemMutex);
  }
}

void logToFile(String message) {
  if (message.length() == 0) return;
  Serial.println(message);
  
  if (ftpDebugMode) addEsdosOutput(message);
  
  if (systemMutex) {
    addSystemLog(message);
    if (!bootComplete) updateBootStatus(message);
  }
}


void retrySDCard() {
  if (initSDCardAutoFreq()) {
    usbPresent = false;
    Serial.println("SD Retry OK");
  } else {
    Serial.println("SD Retry Failed, checking USB...");
    if (CH375FS.begin(CH375_RX, CH375_TX) && CH375FS.mount()) {
      usbPresent = true;
      Serial.println("USB Mount Retry OK");
    } else {
      usbPresent = false;
      Serial.println("USB Mount Retry Failed");
    }
  }
}
