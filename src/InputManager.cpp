#include "Globals.h"
#include "EyudioApps.h"
#include "DoomEngine.h"


extern "C" {
    typedef enum { ev_keydown_m, ev_keyup_m } evtype_m_t;
    typedef struct { evtype_m_t type; int data1; int data2; int data3; } event_m_t;
    void D_PostEvent(const event_m_t* ev);
}

// Protototipler
void handleMouseClick();
void handleRightClick();

// ===== PENCERE SÜRÜKLEME DURUMU =====
static int  wm_drag_idx    = -1;   // Sürüklenen pencere indisi (-1 = yok)
static int  wm_drag_ox     = 0;    // Fare - pencere x farkı
static int  wm_drag_oy     = 0;
static bool wm_click_used  = false; // Bu frame'de tıklama kullanıldı mı?

// ── USB Host Native (State Management) ──────────────────────────
// Note: We skip hub_install because it is often hidden/internal in Arduino-ESP32
// Instead, we will poll for devices or wait for the Hub to auto-init if possible.

static TaskHandle_t daemon_task_hdl = NULL;
static usb_host_client_handle_t client_hdl = NULL;
static usb_device_handle_t dev_hdl = NULL;
static usb_transfer_t *hid_transfer = NULL;
static bool dev_opened = false;
static uint8_t hid_iface_idx = 0;

// Re-add the report parser
void handle_usb_hid_report(uint8_t *data, size_t len, uint8_t dev_addr) {
    if (len < 3) return;
    if (len == 4 || len == 3) { // Mouse
        onMouseEvent(data[0], (int8_t)data[1], (int8_t)data[2], 0, 0);
    } 
    else if (len == 8) { // Keyboard
        uint8_t mods = data[0];
        for (int i = 2; i < 8; i++) {
            if (data[i] != 0) onKeyboardEvent(mods, data[i]);
        }
    }
}

void usb_transfer_cb(usb_transfer_t *transfer) {
    if (transfer->status == USB_TRANSFER_STATUS_COMPLETED) {
        handle_usb_hid_report(transfer->data_buffer, transfer->actual_num_bytes, 0);
    }
    // Re-submit for polling
    if (dev_opened) usb_host_transfer_submit(transfer);
}

void client_event_cb(const usb_host_client_event_msg_t *event_msg, void *arg) {
    if (event_msg->event == USB_HOST_CLIENT_EVENT_NEW_DEV) {
        if (!dev_opened) {
            esp_err_t err = usb_host_device_open(client_hdl, event_msg->new_dev.address, &dev_hdl);
            if (err == ESP_OK) {
                dev_opened = true;
                Serial.printf("[USB] Device Connected (Addr %d)\n", event_msg->new_dev.address);
                
                // Simplified: Try to claim first interface and find first IN endpoint
                usb_host_interface_claim(client_hdl, dev_hdl, 0, 0);
                
                usb_host_transfer_alloc(8, 0, &hid_transfer);
                hid_transfer->device_handle = dev_hdl;
                hid_transfer->bEndpointAddress = 0x81; // Typical HID IN
                hid_transfer->callback = usb_transfer_cb;
                hid_transfer->num_bytes = 8;
                usb_host_transfer_submit(hid_transfer);
            }
        }
    } else if (event_msg->event == USB_HOST_CLIENT_EVENT_DEV_GONE) {
        Serial.println("[USB] Device Disconnected.");
        dev_opened = false;
        if (hid_transfer) { usb_host_transfer_free(hid_transfer); hid_transfer = NULL; }
        if (dev_hdl) { usb_host_device_close(client_hdl, dev_hdl); dev_hdl = NULL; }
    }
}

void usb_lib_task(void *arg) {
    while (1) {
        uint32_t event_flags;
        esp_err_t err = usb_host_lib_handle_events(pdMS_TO_TICKS(10), &event_flags);
        if (err != ESP_OK && err != ESP_ERR_TIMEOUT) {
            Serial.printf("[USB-ERR] Lib Event Error: 0x%x\n", err);
        }
        
        if (client_hdl) {
            err = usb_host_client_handle_events(client_hdl, pdMS_TO_TICKS(10));
            if (err != ESP_OK && err != ESP_ERR_TIMEOUT) {
                Serial.printf("[USB-ERR] Client Event Error: 0x%x\n", err);
            }
        }
        
        if (event_flags & USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS) {
            usb_host_device_free_all();
        }
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

void initUSBHost() {
    Serial.println("[USB] Native USB Host initializing (Pins 19/20)...");
    
    usb_host_config_t host_config = {
        .skip_phy_setup = false,
        .intr_flags = ESP_INTR_FLAG_LEVEL1,
    };
    
    if (usb_host_install(&host_config) == ESP_OK) {
        // We skip hub_install() to avoid linker errors.
        // In many Arduino-ESP32 versions, the hub driver is internal or initialized elsewhere.
        
        usb_host_client_config_t client_config = {
            .is_synchronous = false,
            .max_num_event_msg = 5,
            .async = {
                .client_event_callback = client_event_cb,
                .callback_arg = NULL,
            }
        };
        usb_host_client_register(&client_config, &client_hdl);
        
        xTaskCreatePinnedToCore(usb_lib_task, "usb_events", 4096, NULL, 10, &daemon_task_hdl, 0);
        Serial.println("[USB] Host Stack (+ Hub + Client) Installed OK.");
    }
}

// ============================================================
// processUSBInput — Her loop iterasyonunda çağrılır
// ============================================================
void processUSBInput() {
  // === BUTON DURUM GÜNCELLEMESi ===
  // ÖNCE prev'i kaydet (eski değerden)
  prevLeftButton   = leftButton;
  prevRightButton  = rightButton;
  prevMiddleButton = middleButton;
  // SONRA volatile BLE kaynağından oku
  // (BLE task usb_left_button'u ISR gibi set ediyor)
  leftButton   = usb_left_button;
  rightButton  = usb_right_button;
  middleButton = usb_middle_button;

  // === FARE DELTA UYGULA ===
  bool mouseMoved = (usb_mouse_dx != 0 || usb_mouse_dy != 0);
  if (mouseMoved) {
    mouseX = constrain(mouseX + usb_mouse_dx, 0, SCREEN_WIDTH  - 1);
    mouseY = constrain(mouseY + usb_mouse_dy, 0, SCREEN_HEIGHT - 1);
    usb_mouse_dx = 0;
    usb_mouse_dy = 0;
    lastMouseActivity = millis();
    // Desktop modunda ikon hover güncellenmesi için full redraw
    // Menüde bunu renderMenu zaten yapıyor
    // File Manager context menu hover
    if (showFileManager && showFileMenu) {
      const int MW = 120, MH = 68;
      int mx = fmMenuX, my = fmMenuY;
      if (mx + MW > SCREEN_WIDTH) mx = SCREEN_WIDTH - MW;
      if (my + MH > SCREEN_HEIGHT) my = SCREEN_HEIGHT - MH;
      if (mouseX >= mx && mouseX <= mx + MW && mouseY >= my && mouseY <= my + MH) {
        int sel = (mouseY - my - 2) / 11;
        if (sel >= 0 && sel < 6 && sel != fmMenuSelection) {
          fmMenuSelection = sel;
          fileManagerNeedsRedraw = true;
        }
      }
    }

    // File Manager list hover selection (only if menu is NOT showing)
    if (showFileManager && !showFileMenu) {
      const int LIST_Y0 = 28, LINE_H = 10;
      if (mouseY >= LIST_Y0 && mouseY < SCREEN_HEIGHT - 12) {
        int clicked = (mouseY - LIST_Y0) / LINE_H;
        int maxVis = (SCREEN_HEIGHT - LIST_Y0 - 12) / LINE_H;
        int startIdx = max(0, selectedFile - maxVis + 1);
        if (selectedFile < startIdx) startIdx = selectedFile;
        int realIdx = startIdx + clicked;
        // Allow hover on ALL items (files and folders)
        if (realIdx >= 0 && realIdx < fileCount) {
          if (realIdx != fmHoverIndex) {
            fmHoverIndex = realIdx;
            fileManagerNeedsRedraw = true;
          }
        } else if (fmHoverIndex != -1) {
          fmHoverIndex = -1;
          fileManagerNeedsRedraw = true;
        }
      } else if (fmHoverIndex != -1) {
        fmHoverIndex = -1;
        fileManagerNeedsRedraw = true;
      }
    }
  }

  // === PENCERE SÜRÜKLEME (continuous, fillScreen YOK) ===
  if (leftButton && wm_drag_idx >= 0 && wm_drag_idx < windowCount) {
    Window &dw = windows[wm_drag_idx];
    // Eski konumu kaydet (ghost silme için)
    dw.prevX = dw.x;  dw.prevY = dw.y;
    dw.prevW = dw.w;  dw.prevH = dw.h;
    // Yeni konum
    dw.x = constrain(mouseX - wm_drag_ox, 0, SCREEN_WIDTH  - dw.w);
    dw.y = constrain(mouseY - wm_drag_oy, STATUS_BAR_HEIGHT, SCREEN_HEIGHT - 20);
    dw.needsRedraw = true;
    dw.dirtyRect   = true;   // fillScreen olmadan sadece pencere yenile
    // desktopNeedsRedraw YOK — fillScreen tetiklenmesin!
  } else if (!leftButton) {
    if (wm_drag_idx >= 0) {
      // Drag bitti: tam ekran yenile (ghost temizle)
      desktopNeedsRedraw = true;
    }
    wm_drag_idx = -1;
  }

  // === BUTON KENAR ALGILAMA ===
  wm_click_used = false; // Bu frame'de tıklama kullanıldı mı?

  if (leftButton && !prevLeftButton)    handleMouseClick();

  if (middleButton && !prevMiddleButton) {
    if (inMenu)                        exitMenu();
    else if (showVirtualKeyboard)      closeVirtualKeyboard();
    else if (showFileManager) {
      // File Manager'da bir üst dizine çık (cd ..)
      extern void executeEsdosCommand(String cmd);
      executeEsdosCommand("cd ..");
      fileManagerNeedsRedraw = true;
    } else if (showEsdos) {
      // EsDOS tam ekran modunda bir üst dizine çık (cd ..)
      extern void executeEsdosCommand(String cmd);
      executeEsdosCommand("cd ..");
      esdosNeedsRedraw = true;
    } else if (focusedWindowIndex != -1) {
      // Eğer bir pencere odaklıysa ama özel bir app değilse kapat
      closeWindow(focusedWindowIndex);
    }
    desktopNeedsRedraw = true;
  }

  if (rightButton && !prevRightButton) {
    handleRightClick();
    desktopNeedsRedraw = true;
  }

  // === KLAVYE TAMPON İŞLEME ===
  if (usb_keyboard_data_available) {
    String buf = "";
    if (systemMutex && xSemaphoreTakeRecursive(systemMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
      buf = usb_keyboard_buffer;
      usb_keyboard_buffer = "";
      usb_keyboard_data_available = false;
      xSemaphoreGiveRecursive(systemMutex);
    }
    if (buf.length() > 0) {
      int start = 0, end = buf.indexOf('\n');
      while (end != -1) {
        String k = buf.substring(start, end); k.trim();
        if (k.length() > 0) processKeyboardInput(k);
        start = end + 1;
        end = buf.indexOf('\n', start);
      }
      String last = buf.substring(start); last.trim();
      if (last.length() > 0) processKeyboardInput(last);
    }
  }
}

// ── Event Handlers ──
void onMouseEvent(uint8_t buttons, int8_t x, int8_t y, int8_t wheel, int8_t horiz) {
    usb_mouse_dx += x;
    usb_mouse_dy += y;
    usb_left_button = (buttons & 0x01);
    usb_right_button = (buttons & 0x02);
    usb_middle_button = (buttons & 0x04);
    mouse_connected = true;
    lastMouseActivity = millis();
}

void onKeyboardEvent(uint8_t modifiers, uint8_t key_code) {
    String kStr = "";
    
    // 1. Harfler (a-z / A-Z)
    if (key_code >= 0x04 && key_code <= 0x1D) {
        char c = 'a' + (key_code - 0x04);
        if (modifiers & 0x22) c = toupper(c); // Shift
        kStr = String(c);
    } 
    // 2. Rakamlar (1-9, 0)
    else if (key_code >= 0x1E && key_code <= 0x27) {
        if (key_code == 0x27) kStr = "0";
        else kStr = String((char)('1' + (key_code - 0x1E)));
    }
    // 3. Özel Tuşlar (EyuScript Eşleşmesi İçin Temiz İsimler)
    else if (key_code == 0x28) kStr = "enter";     // ENTER
    else if (key_code == 0x2A) kStr = "backspace"; // BACKSPACE
    else if (key_code == 0x29) kStr = "escape";    // ESC
    else if (key_code == 0x2C) kStr = " ";        // SPACE
    // 4. F Tuşları (F1-F12)
    else if (key_code >= 0x3A && key_code <= 0x45) {
        kStr = String("f") + String(key_code - 0x39);
    }
    
    if (kStr.length() > 0) {
        // EyuScript VM için tamponu DOĞRUDAN doldur ve bayrağı kaldır!
        scriptKeyBuffer = kStr;
        scriptKeyAvailable = true;
        lastKeyDownHID = key_code;

        // Sistemin genel buffer'ı için
        String finalBuffer = "MOD:0x" + String(modifiers, HEX) + ":" + kStr;
        if (systemMutex && xSemaphoreTakeRecursive(systemMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            usb_keyboard_buffer += finalBuffer + "\n";
            usb_keyboard_data_available = true;
            lastKeyboardActivity = millis();
            keyboard_connected = true;
            xSemaphoreGiveRecursive(systemMutex);
        }
    }
}

// ============================================================
// handleMouseClick — Sol tık dispatcher
// ============================================================
// Menü geometrisi SystemConfig.h içinden alınır

void handleMouseClick() {
  lastMouseActivity = millis();

  // --- Tam ekran modlar önce kontrol et ---
  if (runningScript) return;        // Script aktif — tıklamayı yutma
  if (showEyuditor)  return;        // Editör tam ekran — mouse yok say
  if (showEsdos)     return;        // Terminal tam ekran — mouse yok say
  if (showFileManager) {
    // ── Sağ Tık Menüsü Kontrolü ──
    if (showFileMenu) {
      const int MW = 120, MH = 68;
      int mx = fmMenuX, my = fmMenuY;
      if (mx + MW > SCREEN_WIDTH) mx = SCREEN_WIDTH - MW;
      if (my + MH > SCREEN_HEIGHT) my = SCREEN_HEIGHT - MH;

      if (mouseX >= mx && mouseX <= mx + MW && mouseY >= my && mouseY <= my + MH) {
        int sel = (mouseY - my - 2) / 11;
        showFileMenu = false; // Önce menüyü kapat
        fileManagerNeedsRedraw = true;
        if (sel >= 0 && sel < 6) {
          fmMenuSelection = sel;
          switch(sel) {
            case 0: { // Aç / Gir
              ParsedKey pk; pk.isEnter = true; pk.normalizedKey = "\n";
              pk.printableChar = 0; pk.isEscape = pk.isBackspace = pk.isArrow = false;
              handleFileManagerInputKey(pk);
              break;
            }
            case 1: { // Düzenle
              if (fileCount > 0 && !fileList[selectedFile].startsWith("["))
                editFile(fileList[selectedFile]);
              break;
            }
            case 2: { // Yeniden Adlandır
              if (fileCount > 0 && !fileList[selectedFile].startsWith("[")) {
                fileSourcePath = fileList[selectedFile];
                renamingInput = fileList[selectedFile];
                renamingActive = true;
                fileMenuAction = "rename";
                desktopMessage = "Yeni ad: (Enter=onayla, ESC=iptal)";
              }
              break;
            }
            case 3: { // Kopyala
              if (fileCount > 0 && !fileList[selectedFile].startsWith("[")) {
                fileSourcePath = currentFilePath + fileList[selectedFile];
                renamingInput  = fileSourcePath;
                fileMenuAction = "copy_dest";
                desktopMessage = "Kopyala hedef: (Enter=kopyala, ESC=iptal)";
              }
              break;
            }
            case 4: { // Bilgi
              showFileMetadata(fileList[selectedFile].startsWith("[")
                ? fileList[selectedFile].substring(1, fileList[selectedFile].length()-1)
                : fileList[selectedFile]);
              break;
            }
            case 5: { // Sil
              if (fileCount > 0 && !fileList[selectedFile].startsWith("["))
                deleteFile(fileList[selectedFile]);
              break;
            }
          }
        }
        return;
      } else {
        showFileMenu = false; // Dışarı tıklandı
        fileManagerNeedsRedraw = true;
        return;
      }
    }

    // FM'de liste tıklaması
    const int LIST_Y0 = 28, LINE_H = 10;
    if (mouseY >= LIST_Y0 && mouseY < SCREEN_HEIGHT - 22) {
      int clicked = (mouseY - LIST_Y0) / LINE_H;
      int maxVis = (SCREEN_HEIGHT - LIST_Y0 - 22) / LINE_H;
      int startIdx = max(0, selectedFile - maxVis + 1);
      if (selectedFile < startIdx) startIdx = selectedFile;
      int realIdx = startIdx + clicked;
      if (realIdx >= 0 && realIdx < fileCount) {
        if (realIdx == selectedFile) {
          // Çift tık simülasyonu (veya tekrar tık) -> Aç
          ParsedKey pk; pk.isEnter = true; pk.normalizedKey = "\n";
          handleFileManagerInputKey(pk);
        } else {
          selectedFile = realIdx;
        }
        fileManagerNeedsRedraw = true;
      }
    }
    return;
  }

  // --- Menü bağlamı ---
  if (inMenu) {
    for (int i = 0; i < menuItemCount; i++) {
      int iy = MENU_ITEM_Y0 + i * MENU_ITEM_H;
      if (mouseX >= MENU_X + 4 && mouseX <= MENU_X + MENU_W - 4 &&
          mouseY >= iy - 4        && mouseY <= iy + 14) {
        menuSelection = i;
        handleMenuSelection();
        menuNeedsRedraw = true;
        return;
      }
    }
    return; // menü dışı tıklama → yok say
  }

  // --- Virtual keyboard bağlamı ---
  if (showVirtualKeyboard && !vkbActive) {
    handleVirtualKeyboardClick();
    return;
  }

  // --- Pencere yönetimi (z-sırası: üstteki önce) ---
  for (int i = windowCount - 1; i >= 0; i--) {
    Window &w = windows[i];

    // Başlık çubuğu kapatma butonu (yuvarlak kırmızı daire)
    int cx = w.x + w.w - 14, cy = w.y + 11;
    if (mouseX >= cx - 8 && mouseX <= cx + 8 &&
        mouseY >= cy - 8 && mouseY <= cy + 8) {
      closeWindow(i);
      desktopNeedsRedraw = true;
      wm_click_used = true;
      return;
    }

    // Maximize Button (only for Doom)
    if (strcmp(w.appName, "Doom") == 0) {
      int mx = cx - 18, my = cy;
      if (mouseX >= mx - 8 && mouseX <= mx + 8 &&
          mouseY >= my - 8 && mouseY <= my + 8) {
        // Fullscreen toggle disabled for PrBoom port
        desktopNeedsRedraw = true;
        wm_click_used = true;
        staticBackgroundDrawn = false;
        return;
      }
    }

    // Başlık çubuğuna tıklama → drag başlat
    if (mouseX >= w.x && mouseX <= w.x + w.w &&
        mouseY >= w.y && mouseY <= w.y + 22) {
      focusWindow(i);
      wm_drag_idx = i;
      wm_drag_ox  = mouseX - w.x;
      wm_drag_oy  = mouseY - w.y;
      wm_click_used = true;
      desktopNeedsRedraw = true;
      return;
    }

    // Pencere gövdesine tıklama → odakla
    if (mouseX >= w.x && mouseX <= w.x + w.w &&
        mouseY >= w.y && mouseY <= w.y + w.h) {
      focusWindow(i);
      
      // ── Uygulama Özel Tıklamaları ──
      // Tasks: Tab bar tıklama
      if (strcmp(w.appName, "Tasks") == 0) {
        int ox = w.x + 2, oy = w.y + 24;
        int tw = w.w - 4;
        // Tab bar is 16px high at top of content area
        if (mouseY >= oy && mouseY <= oy + 16) {
          if (mouseX < ox + tw / 2) taskManagerTabGlobal = 0;  // FreeRTOS tab
          else                       taskManagerTabGlobal = 1;  // BG Scripts tab
          desktopNeedsRedraw = true;
          wm_click_used = true;
          return;
        }
      }
      if (strcmp(w.appName, "Doom") == 0) {
          event_m_t ev;
          ev.type = ev_keydown_m;
          ev.data1 = 0x80+0x1d; // Ctrl (Shoot)
          ev.data2 = 0;
          ev.data3 = 0;
          D_PostEvent(&ev);
          
          ev.type = ev_keyup_m;
          D_PostEvent(&ev);
          
          wm_click_used = true;
          desktopNeedsRedraw = true;
          return;
      }
      if (strcmp(w.appName, "Display") == 0) {
        int ox = w.x + 2, oy = w.y + 24, winW = w.w - 4;
        int cols = (winW > 300) ? 2 : 1;
        int itemsPerCol = (8 + cols - 1) / cols;
        for (int i = 0; i < 8; i++) {
          int col = i / itemsPerCol;
          int row = i % itemsPerCol;
          int colWidth = (winW - 30) / cols;
          int rx = ox + 15 + col * colWidth, ry = oy + 35 + row * 20;
          if (mouseX >= rx && mouseX <= rx + colWidth - 10 && mouseY >= ry && mouseY <= ry + 18) {
            changeResolution(i, useDoubleBuffering);
            desktopNeedsRedraw = true; return;
          }
        }
        int tx = ox + 15, ty = oy + w.h - 26 - 35;
        if (mouseX >= tx && mouseX <= tx + winW - 30 && mouseY >= ty && mouseY <= ty + 20) {
          useDoubleBuffering = !useDoubleBuffering;
          changeResolution(currentResolutionIndex, useDoubleBuffering);
          desktopNeedsRedraw = true; return;
        }
      } else if (strcmp(w.appName, "Services") == 0) {
        int ox = w.x + 2, oy = w.y + 24;
        if (mouseX >= ox + 20 && mouseX <= ox + 80 && mouseY >= oy + 40 && mouseY <= oy + 65) {
          toggleWifiService(!wifiServiceActive);
          desktopNeedsRedraw = true; return;
        }
        if (mouseX >= ox + 20 && mouseX <= ox + 80 && mouseY >= oy + 80 && mouseY <= oy + 105) {
          toggleBluetoothService(!btServiceActive);
          desktopNeedsRedraw = true; return;
        }
      } else if (strcmp(w.appName, "Market") == 0) {
        handleMarketClick(mouseX, mouseY);
        desktopNeedsRedraw = true; return;
      }

      desktopNeedsRedraw = true;
      wm_click_used = true;
      return;
    }
  }

  // --- Masaüstü ikonu tıklama ---
  if (!wm_click_used) {
    handleDesktopInput(ParsedKey{});
  }
}

// ============================================================
// parseKey — Ham girişi ParsedKey'e dönüştür
// ============================================================
ParsedKey parseKey(String rawKey) {
  ParsedKey pk;
  pk.normalizedKey = rawKey;
  pk.isCmd = false; pk.isCtrl = false; pk.isAlt = false; pk.isShift = false;

  String actualKey = rawKey;
  if (rawKey.startsWith("MOD:")) {
    int fc = rawKey.indexOf(':');
    int sc = rawKey.indexOf(':', fc + 1);
    if (fc != -1 && sc != -1) {
      String modStr = rawKey.substring(fc + 1, sc);
      uint8_t mods = (uint8_t)strtol(modStr.c_str(), NULL, 16);
      pk.isCtrl  = (mods & 0x11);
      pk.isShift = (mods & 0x22);
      pk.isAlt   = (mods & 0x44);
      pk.isCmd   = (mods & 0x88);
      actualKey  = rawKey.substring(sc + 1);
      pk.normalizedKey = actualKey;
    }
  }

  pk.isEnter     = (actualKey == "\n" || actualKey == "\r" || actualKey == "ENTER" || actualKey == "enter" || actualKey == "0x28");
  pk.isEscape    = (actualKey == "\e" || actualKey == "\033"|| actualKey == "ESCAPE" || actualKey == "escape" || actualKey == "0x29");
  pk.isBackspace = (actualKey == "\b" || actualKey == "BACKSPACE" || actualKey == "backspace" || actualKey == "0x2A" || actualKey == "0x2a");
  pk.isArrow     = false;
  pk.arrowDirection = -1;
  pk.printableChar  = 0;

  String checkKey = actualKey;
  checkKey.toLowerCase();

  // Özel Tuş Eşleşmeleri (HID Scan Codes + İsimler)
  if      (actualKey == "0x2B" || checkKey == "tab") { pk.normalizedKey = "\t"; }
  else if (checkKey == "up"   || actualKey == "0x52") { pk.isArrow = true; pk.arrowDirection = 0; pk.normalizedKey = "UP"; }
  else if (checkKey == "down" || actualKey == "0x51") { pk.isArrow = true; pk.arrowDirection = 1; pk.normalizedKey = "DOWN"; }
  else if (checkKey == "left" || actualKey == "0x50") { pk.isArrow = true; pk.arrowDirection = 2; pk.normalizedKey = "LEFT"; }
  else if (checkKey == "right"|| actualKey == "0x4F") { pk.isArrow = true; pk.arrowDirection = 3; pk.normalizedKey = "RIGHT"; }
  else if (actualKey == "0x3A") { pk.normalizedKey = "F1"; }
  else if (actualKey == "0x3B") { pk.normalizedKey = "F2"; }
  else if (actualKey == "0x3C") { pk.normalizedKey = "F3"; }
  else if (actualKey == "0x3D") { pk.normalizedKey = "F4"; }
  else if (actualKey == "0x3E") { pk.normalizedKey = "F5"; }
  else if (actualKey == "0x3F") { pk.normalizedKey = "F6"; }
  else if (actualKey == "0x40") { pk.normalizedKey = "F7"; }
  else if (actualKey == "0x41") { pk.normalizedKey = "F8"; }
  else if (actualKey == "0x42") { pk.normalizedKey = "F9"; }
  else if (actualKey == "0x43") { pk.normalizedKey = "F10"; }
  else if (actualKey == "0x44") { pk.normalizedKey = "F11"; }
  else if (actualKey == "0x45") { pk.normalizedKey = "F12"; }
  else if (actualKey == "0x2C" || checkKey == "space" || actualKey == " ") { pk.normalizedKey = " "; pk.printableChar = ' '; }
  else if (actualKey == "\b")   { pk.normalizedKey = "BACKSPACE"; }
  else if (checkKey.startsWith("f") && checkKey.length() > 1 && isdigit(checkKey[1])) { pk.normalizedKey = actualKey; pk.normalizedKey.toUpperCase(); }
  else if (actualKey.length() == 1 && (uint8_t)actualKey[0] >= 32 && (uint8_t)actualKey[0] <= 126) {
    pk.printableChar = actualKey[0];
  }

  return pk;
}

// ============================================================
// processKeyboardInput — Aktif context'e klavye girdisi gönder
// ============================================================
void processKeyboardInput(String line) {
  ParsedKey pk = parseKey(line);
  lastKeyboardActivity = millis();

  // Abort waiting for driver flash
  if (inputDriverWaitingForFlash) {
    if (pk.isEscape) {
      inputDriverWaitingForFlash = false;
      inputDriverVerified = true; // Stop waiting
      desktopNeedsRedraw = true;
      return;
    }
  }

  // Shift + Backspace emergency exit for Doom
  if (showDoom) {
    bool isShiftPressed = (heldKeys[0xE1] || heldKeys[0xE5] || pk.isShift);
    bool isBackspacePressed = (heldKeys[0x2A] || pk.isBackspace);
    if (isShiftPressed && isBackspacePressed) {
      extern void closeDoom();
      closeDoom();
      return;
    }
    // Discard all OS-level shortcuts and window inputs when Doom is running.
    // Doom reads heldKeys directly on Core 0.
    return;
  }

  // GSOD (Global Green Screen of Death) kontrolü
  if (systemInErrorState) {
    if (pk.normalizedKey == "r" || pk.normalizedKey == "R" || pk.normalizedKey == "0x15") {
      Serial.println("[SYS] Restarting via R key...");
      Serial.flush();
      delay(50);
      ESP.restart();
    }
    if (pk.isEscape) {
      showEsdos = true;
      currentApp = "EsDOS";
      extern bool esdosNeedsRedraw, desktopNeedsRedraw;
      esdosNeedsRedraw = true;
      desktopNeedsRedraw = true;
      systemInErrorState = false;
      return;
    }
  }

  if (pk.normalizedKey == "F12") { toggleSystemLogs(); return; }

  // Pencere yöneticisi kısayolları (Cmd+...)
  if (pk.isCmd) {
    if (pk.isArrow && focusedWindowIndex != -1) {
      Window &w = windows[focusedWindowIndex];
      if      (pk.arrowDirection == 2) { // Sol → sol yarıya yapıştır
        w.x = 0; w.y = STATUS_BAR_HEIGHT;
        w.w = SCREEN_WIDTH / 2; w.h = DESKTOP_HEIGHT;
      } else if (pk.arrowDirection == 3) { // Sağ → sağ yarıya yapıştır
        w.x = SCREEN_WIDTH / 2; w.y = STATUS_BAR_HEIGHT;
        w.w = SCREEN_WIDTH / 2; w.h = DESKTOP_HEIGHT;
      } else if (pk.arrowDirection == 0) { // Yukarı → tam ekran
        w.x = 0; w.y = STATUS_BAR_HEIGHT;
        w.w = SCREEN_WIDTH; w.h = DESKTOP_HEIGHT;
      } else { // Aşağı → ortala
        w.w = 280; w.h = 180;
        w.x = (SCREEN_WIDTH  - w.w) / 2;
        w.y = (SCREEN_HEIGHT - w.h) / 2;
      }
      w.needsRedraw = true;
      desktopNeedsRedraw = true;
      return;
    }
    if (pk.normalizedKey == "\t" || pk.normalizedKey == "0x2B") { cycleFocus(); return; }
    if (pk.normalizedKey == "w"  || pk.normalizedKey == "W") {
      if (focusedWindowIndex != -1) closeWindow(focusedWindowIndex);
      return;
    }
  }

  // EyuScript tam ekran modu çıkış kontrolü
  if (runningScript) {
    if (pk.isEscape) {
      runningScript = false;
      addEsdosOutput("--- Script durduruldu (ESC) ---");
      esdosNeedsRedraw = true;
      desktopNeedsRedraw = true;
    } else {
      scriptKeyBuffer = pk.normalizedKey;
      scriptKeyAvailable = true;
    }
    return;
  }

  // Bağlama göre dispatch
  if (showVirtualKeyboard) {
    handleVirtualKeyboardInput(pk);
  } else if (showEyuditor) {
    if (pk.isCtrl && (pk.normalizedKey == "s" || pk.normalizedKey == "S")) saveEyuditor();
    else handleEyuditorInputKey(pk);
  } else if (showFileManager) {
    handleFileManagerInputKey(pk);
  } else if (showEsdos) {
    handleEsdosInputKey(pk);
  } else if (focusedWindowIndex != -1) {
    String app = windows[focusedWindowIndex].appName;
    if (pk.isEscape) {
      if (app == "Eyudio" && eyudioFullscreenQR) {
        eyudioFullscreenQR = false;
        desktopNeedsRedraw = true;
        return;
      }
      closeWindow(focusedWindowIndex);
      return;
    }
    if      (app == "Eyudio")       handleEyudioInputKey(pk);
    else if (app == "Eyuditor")     handleEyuditorInputKey(pk);

    else if (app == "esdos")        handleEsdosInputKey(pk);

    else if (app == "File Manager") handleFileManagerInputKey(pk);
    else if (app == "System Logs")  handleSystemLogsInput(pk);
    else if (app == "Display")      handleDisplayInputKey(pk);
    else if (app == "Tasks") {
      if (pk.normalizedKey == "\t" || pk.normalizedKey == "TAB") {
        taskManagerTabGlobal = (taskManagerTabGlobal + 1) % 2;
        desktopNeedsRedraw = true;
      }
    }
    else if (app == "Doom")         handleDoomInput(pk);
    else if (app == "Market")       handleMarketInput(pk);
    else if (app == "Browser") {
      if (pk.normalizedKey == "UP")   { if (webBrowserScrollY > 100) webBrowserScrollY -= 100; else webBrowserScrollY = 0; desktopNeedsRedraw = true; }
      if (pk.normalizedKey == "DOWN") { webBrowserScrollY += 100; desktopNeedsRedraw = true; }
    }
  } else if (inMenu) {
    handleMenuInput(pk, line);
  } else if (showBootMenu) {
    handleBootMenuInput(pk);
  } else {
    handleDesktopInput(pk);
  }
}

// ============================================================
// processSerialInput — UART'tan gelen Arduino Driver v4.7 protokolünü işle
//
//  Yeni Protokol (arduino_uart_keyboard.ino v4.7):
//    M,dx,dy          — Fare hareketi (birikmiş, 20ms'de bir gönderilir)
//    MB,N,S           — Mouse button  N=1(sol) 2(sağ) 4(orta), S=1(basıldı) 0(bırakıldı)
//    KD,mod,key,ascii — Tuş basıldı   (mod & key HEX, ascii decimal)
//    KU,mod,key,ascii — Tuş bırakıldı (şimdi sadece loglama; ileride key-up olayı için)
//    ST,CON,1/0       — Cihaz bağlandı/ayrıldı
//    SYS,BOOT_OK      — Arduino hazır
//    SYS,USB_ERR      — USB init hatası
// ============================================================
void processSerialInput(String line) {
  if (line.length() == 0) return;

  // ── Fare hareketi: M,dx,dy ─────────────────────────────────
  if (line.startsWith("M,")) {
    int c1 = line.indexOf(',');
    int c2 = line.indexOf(',', c1 + 1);
    if (c1 != -1 && c2 != -1) {
      int dx = line.substring(c1 + 1, c2).toInt();
      int dy = line.substring(c2 + 1).toInt();
      usb_mouse_dx += dx;
      usb_mouse_dy += dy;
      lastMouseActivity = millis();
      mouse_connected = true;
    }
    return;
  }

  // ── Fare butonu: MB,N,S  (N=1|2|4, S=0|1) ─────────────────
  // Yeni driver sayısal buton kodu kullanıyor (L/R/M yerine)
  if (line.startsWith("MB,")) {
    int c1 = line.indexOf(',');
    int c2 = line.indexOf(',', c1 + 1);
    if (c1 != -1 && c2 != -1) {
      int  btn   = line.substring(c1 + 1, c2).toInt(); // 1=sol 2=sağ 4=orta
      bool state = (line.substring(c2 + 1).toInt() == 1);

      if      (btn == 1) usb_left_button   = state;
      else if (btn == 2) usb_right_button  = state;
      else if (btn == 4) usb_middle_button = state;

      mouse_connected = true;
    }
    return;
  }

  // ── Tuş basıldı: KD,mod_hex,key_hex,ascii ─────────────────
  if (line.startsWith("KD,")) {
    // Format: KD,<mod_hex>,<key_hex>,<ascii_dec>
    // Örn:   KD,0,28,13   (mod=0, key=Enter 0x28, ascii=13)
    //        KD,22,4,65   (Shift+A: mod=0x22, key=0x04, ascii=65='A')
    int c1 = line.indexOf(',');         // KD|
    int c2 = line.indexOf(',', c1+1);   // mod|
    int c3 = line.indexOf(',', c2+1);   // key|
    if (c1 == -1 || c2 == -1 || c3 == -1) return;

    uint8_t mod   = (uint8_t)strtol(line.substring(c1+1, c2).c_str(), NULL, 16);
    uint8_t hid   = (uint8_t)strtol(line.substring(c2+1, c3).c_str(), NULL, 16);
    uint8_t ascii = (uint8_t)line.substring(c3+1).toInt();

    keyboard_connected = true;
    lastKeyboardActivity = millis();

    // Held-Keys Takibi
    heldKeys[hid] = true;
    lastKeyDownHID = hid;
    keyEventAvailable = true;

    // ASCII'yi ya da HID kodunu ParsedKey imzasına çevir
    // Yeni driver bize hem HID hem ASCII veriyor — ikisini kullan.
    String keyStr = "";

    // Öncelik 1: Özel tuşlar (HID kodu ile tespit)
    switch (hid) {
      case 0x28: keyStr = "enter"; break; // Enter
      case 0x29: keyStr = "escape"; break; // Escape
      case 0x2A: keyStr = "backspace"; break; // Backspace
      case 0x2B: keyStr = "tab"; break; // Tab
      case 0x4F: keyStr = "right"; break; // Sağ ok
      case 0x50: keyStr = "left"; break; // Sol ok
      case 0x51: keyStr = "down"; break; // Aşağı ok
      case 0x52: keyStr = "up"; break; // Yukarı ok
      case 0x3A: keyStr = "f1"; break; // F1
      case 0x3B: keyStr = "f2"; break; // F2
      case 0x3C: keyStr = "f3"; break; // F3
      case 0x3D: keyStr = "f4"; break; // F4
      case 0x3E: keyStr = "f5"; break; // F5
      case 0x3F: keyStr = "f6"; break; // F6
      case 0x40: keyStr = "f7"; break; // F7
      case 0x41: keyStr = "f8"; break; // F8
      case 0x42: keyStr = "f9"; break; // F9
      case 0x43: keyStr = "f10"; break; // F10
      case 0x44: keyStr = "f11"; break; // F11
      case 0x45: keyStr = "f12"; break; // F12
      case 0x49: keyStr = "insert"; break; // Insert
      case 0x4A: keyStr = "home"; break; // Home
      case 0x4B: keyStr = "pageup"; break; // Page Up
      case 0x4C: keyStr = "delete"; break; // Delete
      case 0x4D: keyStr = "end"; break; // End
      case 0x4E: keyStr = "pagedown"; break; // Page Down
      case 0x2C: keyStr = "space"; break; // Space
      default: break;
    }

    // Öncelik 2: ASCII yazdırılabilir karakter (32–126)
    if (keyStr.isEmpty() && ascii >= 32 && ascii <= 126) {
      keyStr = String((char)ascii);
    }

    if (keyStr.isEmpty()) return; // Tanınmayan tuş — yok say

    // Modifier öneki ekle (mevcut parseKey sistemi ile uyumlu)
    String finalLine = "";
    if (mod != 0) {
      finalLine = "MOD:0x";
      if (mod < 16) finalLine += "0";
      finalLine += String(mod, HEX) + ":" + keyStr;
    } else {
      finalLine = keyStr;
    }

    // Klavye tamponuna gönder (thread-safe)
    if (systemMutex && xSemaphoreTakeRecursive(systemMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
      usb_keyboard_buffer += finalLine + "\n";
      usb_keyboard_data_available = true;
      xSemaphoreGiveRecursive(systemMutex);
    }
    return;
  }

  // ── Tuş bırakıldı: KU,mod,key,ascii ───────────────────────
  if (line.startsWith("KU,")) {
    int c1 = line.indexOf(',');
    int c2 = line.indexOf(',', c1+1);
    int c3 = line.indexOf(',', c2+1);
    if (c1 != -1 && c2 != -1 && c3 != -1) {
      uint8_t hid = (uint8_t)strtol(line.substring(c2+1, c3).c_str(), NULL, 16);
      heldKeys[hid] = false;
      lastKeyUpHID = hid;
      keyEventAvailable = true;
    }
    return;
  }

  // ── Gamepad butonu: GP,player,buttonName,state (state=0|1) ─────────────────
  if (line.startsWith("GP,")) {
    int c1 = line.indexOf(',');
    int c2 = line.indexOf(',', c1 + 1);
    int c3 = line.indexOf(',', c2 + 1);
    if (c1 != -1 && c2 != -1 && c3 != -1) {
      int player = line.substring(c1 + 1, c2).toInt();
      String btn = line.substring(c2 + 1, c3);
      bool state = (line.substring(c3 + 1).toInt() == 1);
      
      int playerIdx = player - 1;
      if (playerIdx >= 0 && playerIdx < 2) {
        int btnIdx = -1;
        if      (btn == "UP")       btnIdx = 0;
        else if (btn == "DOWN")     btnIdx = 1;
        else if (btn == "LEFT")     btnIdx = 2;
        else if (btn == "RIGHT")    btnIdx = 3;
        else if (btn == "X")        btnIdx = 4;
        else if (btn == "TRIANGLE") btnIdx = 5;
        else if (btn == "CIRCLE")   btnIdx = 6;
        else if (btn == "SQUARE")   btnIdx = 7;
        else if (btn == "L1")       btnIdx = 8;
        else if (btn == "R1")       btnIdx = 9;
        else if (btn == "L2")       btnIdx = 10;
        else if (btn == "R2")       btnIdx = 11;
        else if (btn == "SELECT")   btnIdx = 12;
        else if (btn == "START")    btnIdx = 13;
        
        if (btnIdx >= 0 && btnIdx < 14) {
          gamepadButtons[playerIdx][btnIdx] = state;
        }
      }
      
      if (player == 1) {
        uint8_t hid = 0;
        String keyStr = "";
        
        if (btn == "UP")         { hid = 0x52; keyStr = "UP"; }
        else if (btn == "DOWN")  { hid = 0x51; keyStr = "DOWN"; }
        else if (btn == "LEFT")  { hid = 0x50; keyStr = "LEFT"; }
        else if (btn == "RIGHT") { hid = 0x4F; keyStr = "RIGHT"; }
        else if (btn == "X")     { hid = 0x28; keyStr = "0x28"; } // Enter
        else if (btn == "TRIANGLE"){ hid = 0x29; keyStr = "0x29"; } // Escape
        else if (btn == "CIRCLE") { hid = 0x2A; keyStr = "0x2A"; } // Backspace
        else if (btn == "SQUARE") { hid = 0x2C; keyStr = "0x2C"; } // Space
        else if (btn == "SELECT") { hid = 0x2B; keyStr = "0x2B"; } // Tab
        else if (btn == "START")  { hid = 0x28; keyStr = "0x28"; } // Enter
        else if (btn == "R1" || btn == "R2") { hid = 0xE0; keyStr = ""; } // Left Ctrl (Shoot in Doom)
        else if (btn == "L1" || btn == "L2") { hid = 0x2C; keyStr = " "; } // Space (Open doors / Use)
        
        if (hid != 0) {
          heldKeys[hid] = state;
          if (state) {
            lastKeyDownHID = hid;
          } else {
            lastKeyUpHID = hid;
          }
          keyEventAvailable = true;
          
          if (state && keyStr.length() > 0) {
            processKeyboardInput(keyStr);
          }
        }
      }
    }
    return;
  }

  // ── Gamepad analog çubukları: GPA,player,LX,LY,RX,RY ─────────────────
  if (line.startsWith("GPA,")) {
    int c1 = line.indexOf(',');
    int c2 = line.indexOf(',', c1 + 1);
    int c3 = line.indexOf(',', c2 + 1);
    int c4 = line.indexOf(',', c3 + 1);
    int c5 = line.indexOf(',', c4 + 1);
    if (c1 != -1 && c2 != -1 && c3 != -1 && c4 != -1 && c5 != -1) {
      int player = line.substring(c1 + 1, c2).toInt();
      int lx = line.substring(c2 + 1, c3).toInt();
      int ly = line.substring(c3 + 1, c4).toInt();
      int rx = line.substring(c4 + 1, c5).toInt();
      int ry = line.substring(c5 + 1).toInt();
      
      int playerIdx = player - 1;
      if (playerIdx >= 0 && playerIdx < 2) {
        gamepadAnalog[playerIdx][0] = lx;
        gamepadAnalog[playerIdx][1] = ly;
        gamepadAnalog[playerIdx][2] = rx;
        gamepadAnalog[playerIdx][3] = ry;
      }
      
      if (player == 1) {
        int activeLX = 0;
        int activeLY = 0;
        
        if (analogStickPreference == 0) { // Auto/Left with Right Backup
          activeLX = lx;
          activeLY = ly;
          int deadzone = 25;
          if (abs(rx) > deadzone || abs(ry) > deadzone) {
            activeLX = rx;
            activeLY = ry;
          }
        } 
        else if (analogStickPreference == 1) { // Force Right Stick
          activeLX = rx;
          activeLY = ry;
        } 
        // if analogStickPreference == 2 (Disabled), activeLX and activeLY remain 0

        if (showDoom) {
          int moveY = activeLY;
          int moveX = activeLX;

          // Walk forward/backward
          if (moveY < -40) {
            if (!heldKeys[0x52]) { heldKeys[0x52] = true; keyEventAvailable = true; }
            heldKeys[0x51] = false;
          } else if (moveY > 40) {
            if (!heldKeys[0x51]) { heldKeys[0x51] = true; keyEventAvailable = true; }
            heldKeys[0x52] = false;
          } else {
            if (heldKeys[0x52]) { heldKeys[0x52] = false; keyEventAvailable = true; }
            if (heldKeys[0x51]) { heldKeys[0x51] = false; keyEventAvailable = true; }
          }
          
          // Turn left/right
          if (moveX < -40) {
            if (!heldKeys[0x50]) { heldKeys[0x50] = true; keyEventAvailable = true; }
            heldKeys[0x4F] = false;
          } else if (moveX > 40) {
            if (!heldKeys[0x4F]) { heldKeys[0x4F] = true; keyEventAvailable = true; }
            heldKeys[0x50] = false;
          } else {
            if (heldKeys[0x50]) { heldKeys[0x50] = false; keyEventAvailable = true; }
            if (heldKeys[0x4F]) { heldKeys[0x4F] = false; keyEventAvailable = true; }
          }
        } else {
          // Desktop/Menu navigation
          int deadzone = 25;
          if (abs(activeLX) > deadzone || abs(activeLY) > deadzone) {
            int dx = (activeLX > 0) ? (activeLX - deadzone) : (activeLX + deadzone);
            int dy = (activeLY > 0) ? (activeLY - deadzone) : (activeLY + deadzone);
            usb_mouse_dx += dx / 16;
            usb_mouse_dy += dy / 16;
            lastMouseActivity = millis();
            mouse_connected = true;
          }
        }
      }
    }
    return;
  }

  // ── Cihaz bağlantı durumu: ST,CON,0/1 ────────────────────
  if (line.startsWith("ST,CON,")) {
    bool connected = (line.endsWith("1"));
    mouse_connected    = connected;
    keyboard_connected = connected;
    logToFile(connected ? "[USB] HID Cihaz Baglandi" : "[USB] HID Cihaz Ayrildi");
    return;
  }

  // ── Sistem mesajları: SYS,* ───────────────────────────────
  if (line.startsWith("SYS,")) {
    String msg = line.substring(4);
    if (msg.startsWith("DRV:")) {
      String drv = msg.substring(4);
      if (drv == "NORMAL") currentInputDriver = 0;
      else if (drv == "CONTROLLER") currentInputDriver = 1;
      else if (drv == "BTDONGLE") currentInputDriver = 2;
      
      Serial1.println("SYS,ACK");
      if (!inputDriverVerified) {
        inputDriverVerified = true;
        inputDriverWaitingForFlash = false;
        logToFile("[USB] Handshake OK: " + drv);
      }
    }

    else if (msg == "BOOT_OK") {
      logToFile("[USB] Arduino Driver hazir (v4.7)");
      mouse_connected    = false; // Henüz cihaz yok, ST,CON bekle
      keyboard_connected = false;
    } else if (msg == "USB_ERR") {
      logToFile("[USB] Arduino USB Init Hatasi!");
    } else {
      logToFile("[USB] SYS: " + msg);
    }
    return;
  }

  // ── Bilinmeyen paket — debug loguna yaz ──────────────────
  // logToFile("[UART?] " + line);
}


// handleRightClick — Sağ tık dispatcher
void handleRightClick() {
  lastMouseActivity = millis();
  
  if (showFileManager) {
    const int LIST_Y0 = 28, LINE_H = 10;
    if (mouseY >= LIST_Y0 && mouseY < SCREEN_HEIGHT - 22) {
      int clicked = (mouseY - LIST_Y0) / LINE_H;
      int maxVis = (SCREEN_HEIGHT - LIST_Y0 - 22) / LINE_H;
      int startIdx = max(0, selectedFile - maxVis + 1);
      if (selectedFile < startIdx) startIdx = selectedFile;
      int realIdx = startIdx + clicked;
      
      if (realIdx >= 0 && realIdx < fileCount) {
        selectedFile = realIdx; // Sağ tıkladığında seç
        fmHoverIndex = realIdx;
        fmMenuX = mouseX;
        fmMenuY = mouseY;
        fmMenuSelection = 0; 
        showFileMenu = true;
        fileManagerNeedsRedraw = true;
      }
    } else {
      // Liste dışına sağ tık: menüyü kapat
      if (showFileMenu) { showFileMenu = false; fileManagerNeedsRedraw = true; }
    }
  }
  
  // Desktop sağ tık menüsü
  else if (!inMenu) {
    enterMenu("Main");
    desktopNeedsRedraw = true;
  }
}

// updateCursorArea — Ghost temizlemek için eski fare konumunu saklar
void updateCursorArea() {
  prevMouseX = mouseX;
  prevMouseY = mouseY;
}
