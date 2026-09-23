#include "Globals.h"
#include <esp_task_wdt.h>
#include "WadManager.h"
#include "DoomEngine.h"
#include "driver/temp_sensor.h"

// ── Doom Engine App ───────────────────────────────────────
void openDoom(String wadPath) {
  Serial.println("[DOOM-DEBUG] openDoom started");
  if (wadPath == "") wadPath = selectedWadPath;
  if (wadPath == "") wadPath = "/sd/doom1.wad"; // Default

  Serial.println("[DOOM-DEBUG] Closing WAD...");
  // Release WadManager file handles so Doom can safely open the file
  wadMgr.closeWad();

  Serial.println("[DOOM-DEBUG] Initializing eyeDoom...");
  if (!eyeDoom.init(wadPath)) {
      Serial.println("[DOOM-DEBUG] eyeDoom init failed!");
      addSystemLog("[DOOM] Failed to initialize PrBoom");
      desktopMessage = "HATA: Doom baslatilamadi!";
      desktopNeedsRedraw = true;
      return;
  }
  Serial.println("[DOOM-DEBUG] eyeDoom init succeeded. Setting flags...");
  showDoom = true;
  doomWadLoaded = true;
  currentApp = "Doom";
  desktopNeedsRedraw = true;
  Serial.println("[DOOM-DEBUG] openDoom finished");
}

void closeDoom() {
  showDoom = false;
  doomWadLoaded = false;
  currentApp = "";
  eyeDoom.stop();
  desktopNeedsRedraw = true;
}

// --- Key Repeat Logic for Apps (Continuous polling for held keys) ---
static uint32_t lastAppRepeatTick = 0;
static uint8_t repeatingAppHID = 0xFF;

void processAppRepeatingKeys() {
  if (!heldKeys) return; 

  uint32_t now = millis();
  uint8_t currentHID = 0xFF;

  // Priority: Backspace, Arrows
  if      (heldKeys[0x2A]) currentHID = 0x2A; // Backspace
  else if (heldKeys[0x52]) currentHID = 0x52; // Up
  else if (heldKeys[0x51]) currentHID = 0x51; // Down
  else if (heldKeys[0x50]) currentHID = 0x50; // Left
  else if (heldKeys[0x4F]) currentHID = 0x4F; // Right

  if (currentHID == 0xFF) {
    repeatingAppHID = 0xFF;
    return;
  }

  // If this is the start of a press or a key switch
  if (currentHID != repeatingAppHID) {
    repeatingAppHID = currentHID;
    lastAppRepeatTick = now + 400; // Initial Delay Period (400ms)
    return;
  }

  // If initial delay has passed, tick at high frequency
  if (now >= lastAppRepeatTick) {
    lastAppRepeatTick = now + 50; // Repeat Interval (50ms = 20Hz)
    
    ParsedKey pk;
    pk.printableChar=0; pk.isShift=false; pk.isCtrl=false; pk.isAlt=false; pk.isCmd=false;
    pk.normalizedKey = ""; 
    pk.isBackspace = (currentHID == 0x2A);
    pk.isArrow = (currentHID >= 0x4F && currentHID <= 0x52);
    pk.isEnter = false; pk.isEscape = false;
    if (pk.isArrow) pk.arrowDirection = (currentHID == 0x52 ? 0 : currentHID == 0x51 ? 1 : currentHID == 0x50 ? 2 : 3);
    
    // Explicitly handle repeat-friendly actions
    if (showEyuditor)      handleEyuditorInputKey(pk);
    else if (showEsdos)    handleEsdosInputKey(pk);
    else if (showFileManager) handleFileManagerInputKey(pk);
  }
}

void renderDoomFrame() {
  if (!showDoom) return;

  static uint32_t lastDoomUpdate = 0;
  uint32_t now = millis();
  float dt = (now - lastDoomUpdate) / 1000.0f;
  if (dt > 0.1f) dt = 0.1f;
  lastDoomUpdate = now;

  eyeDoom.update(dt);
}

void handleDoomInput(ParsedKey key) {
  // Escape key is passed to PrBoom's event loop to open Doom's menu.
  // F10 key acts as the emergency exit to return to EsDOS/Desktop.
  if (key.normalizedKey == "F10" || key.normalizedKey == "f10") {
    closeDoom();
  }
  // F11 toggles fullscreen / small window
  if (key.normalizedKey == "F11" || key.normalizedKey == "f11") {
    eyeDoom.toggleFullscreen();
  }
}

float getChipTemperature() {
    temp_sensor_config_t temp_sensor = TSENS_CONFIG_DEFAULT();
    temp_sensor.dac_offset = TSENS_DAC_L2;
    temp_sensor_set_config(temp_sensor);
    temp_sensor_start();
    
    uint32_t tsens_out;
    temp_sensor_read_raw(&tsens_out);
    temp_sensor_stop();
    
    // Basit bir donusturme (yaklasik)
    return (float)tsens_out * 0.1f + 10.0f; 
}

// ============================================================
// === FILE MANAGER ==========================================
// ============================================================

void openFileManager() {
  if (!sdCardPresent && !usbPresent) { desktopMessage = "Hafiza bulunamadi!"; return; }
  showFileManager      = true;
  fileManagerNeedsRedraw = true;
  staticBackgroundDrawn  = false;
  selectedFile           = 0;
  fileManagerFirstVisible = 0;
  currentFilePath        = "/";
  renamingActive         = false;
  renamingInput          = "";
  showFileMenu           = false;
  showFileHelp           = false;
  fileMenuAction         = "";
  fileSourcePath         = "";
  scanFiles();
}

void scanFiles() {
  fileCount = 0;
  if (!sdCardPresent && !usbPresent) return;
  File root = SD.open(currentFilePath);
  if (!root) return;
  while (fileCount < 20) {
    File entry = root.openNextFile();
    if (!entry) break;
    String name = String(entry.name());
    int lastSlash = name.lastIndexOf('/');
    if (lastSlash >= 0) name = name.substring(lastSlash + 1);
    fileList[fileCount++] = entry.isDirectory() ? "[" + name + "]" : name;
    entry.close();
  }
  root.close();
}

// Dosyayı Eyuditor'da aç
void editFile(String fileName) {
  openEyuditor(currentFilePath + fileName);
  showFileManager = false;
}

// Eyuditor içeriğini kaydet (filePath ile)
void saveFile(String filePath) {
  if (!sdCardPresent && !usbPresent) return;
  File file = SD.open(filePath, FILE_WRITE);
  if (file) {
    for (int i = 0; i < eyuditorLineCount; i++) file.println(eyuLines[i]);
    file.close();
    desktopMessage = "Kaydedildi: " + filePath;
    scanFiles();
    fileManagerNeedsRedraw = true;
  }
}

// Dosya bilgisini statusbar'a yaz
void showFileMetadata(String fileName) {
  String fullPath = currentFilePath + fileName;
  if (!sdCardPresent && !usbPresent) return;
  File file = SD.open(fullPath);
  if (!file) { desktopMessage = "Dosya bulunamadi!"; return; }
  size_t sz = file.size();
  bool isDir = file.isDirectory();
  file.close();
  char buf[80];
  if (isDir) snprintf(buf, sizeof(buf), "[DIZ] %s", fileName.c_str());
  else       snprintf(buf, sizeof(buf), "%s | %u B", fileName.c_str(), (unsigned)sz);
  desktopMessage = String(buf);
  fileMetadataVisible    = true;
  fileManagerNeedsRedraw = true;
}

void deleteFile(String fileName) {
  String path = currentFilePath + fileName;
  if (sdCardPresent || usbPresent) {
    bool ok = SD.remove(path);
    desktopMessage = ok ? "Silindi: " + fileName : "Silme hatasi!";
    if (ok) {
      scanFiles();
      if (selectedFile >= fileCount && selectedFile > 0) selectedFile--;
    }
    fileManagerNeedsRedraw = true;
  }
}

void renameFile(String oldN, String newN) {
  if (!sdCardPresent && !usbPresent) return;
  String src = currentFilePath + oldN;
  String dst = currentFilePath + newN;
  bool ok = SD.rename(src, dst);
  desktopMessage = ok ? "Yeniden adlandirildi." : "Rename hatasi!";
  scanFiles();
  fileManagerNeedsRedraw = true;
}

void copyFile(String src, String dst) {
  if (!sdCardPresent && !usbPresent) return;
  File s = SD.open(src);
  File d = SD.open(dst, FILE_WRITE);
  if (s && d) {
    while (s.available()) d.write(s.read());
    desktopMessage = "Kopyalandi.";
  } else {
    desktopMessage = "Kopyalama hatasi!";
  }
  if (s) s.close();
  if (d) d.close();
  scanFiles();
  fileManagerNeedsRedraw = true;
}

void moveFile(String src, String dst) {
  if (!sdCardPresent && !usbPresent) return;
  // Kopyala + sil = taşı
  copyFile(src, dst);
  SD.remove(src);
  desktopMessage = "Tasindi.";
  scanFiles();
  fileManagerNeedsRedraw = true;
}

// ── FM Tuş İşleyicisi ────────────────────────────────────────
// FM'nin modları:
//   renamingActive    → yeni dosya adı / rename giriş modu
//   fileMenuAction == "copy_dest" → kopya hedef yolu giriş modu
//   fileMenuAction == "move_dest" → taşıma hedef yolu giriş modu
// ─────────────────────────────────────────────────────────────

void handleFileManagerInputKey(ParsedKey key) {

  // ── Giriş modu: yeni dosya / rename / kopyala-hedef / taşı-hedef ──
  if (renamingActive) {
    if (key.isEnter && renamingInput.length() > 0) {
      if (fileMenuAction == "rename") {
        // Yeniden adlandır
        String oldName = fileSourcePath;  // fileSourcePath'e eski adı koyduk
        renameFile(oldName, renamingInput);
      } else {
        // Yeni dosya oluştur
        String fullPath = currentFilePath + renamingInput;
        if (fullPath.indexOf('.') == -1) {
          fullPath    += ".txt";
          renamingInput += ".txt";
        }
        File f = SD.open(fullPath, FILE_WRITE); if (f) f.close();
        desktopMessage = "Olusturuldu: " + renamingInput;
        scanFiles();
        // Seç ve Eyuditor'da aç
        for (int i = 0; i < fileCount; i++) {
          if (fileList[i] == renamingInput) { selectedFile = i; break; }
        }
      }
      renamingActive = false; renamingInput = ""; fileMenuAction = "";
      fileManagerNeedsRedraw = true;
    } else if (key.isEscape) {
      renamingActive = false; renamingInput = ""; fileMenuAction = "";
      fileManagerNeedsRedraw = true;
    } else if (key.isBackspace) {
      if (renamingInput.length() > 0) renamingInput.remove(renamingInput.length() - 1);
      fileManagerNeedsRedraw = true;
    } else if (key.printableChar != 0) {
      renamingInput += key.printableChar;
      fileManagerNeedsRedraw = true;
    }
    return;
  }

  // ── Hedef yol giriş modu (copy/move) ──
  if (fileMenuAction == "copy_dest" || fileMenuAction == "move_dest") {
    if (key.isEnter && renamingInput.length() > 0) {
      if (fileMenuAction == "copy_dest")
        copyFile(fileSourcePath, renamingInput);
      else
        moveFile(fileSourcePath, renamingInput);
      renamingInput = ""; fileMenuAction = ""; fileSourcePath = "";
      fileManagerNeedsRedraw = true;
    } else if (key.isEscape) {
      renamingInput = ""; fileMenuAction = ""; fileSourcePath = "";
      fileManagerNeedsRedraw = true;
    } else if (key.isBackspace) {
      if (renamingInput.length() > 0) renamingInput.remove(renamingInput.length() - 1);
      fileManagerNeedsRedraw = true;
    } else if (key.printableChar != 0) {
      renamingInput += key.printableChar;
      fileManagerNeedsRedraw = true;
    }
    return;
  }

  // ── Normal Gezinti Modları ──
  if (key.isArrow && key.arrowDirection == 0) {
    if (selectedFile > 0) selectedFile--;
    fmHoverIndex = selectedFile; // Hover senkronize et
    fileManagerNeedsRedraw = true;
  } else if (key.isArrow && key.arrowDirection == 1) {
    if (selectedFile < fileCount - 1) selectedFile++;
    fmHoverIndex = selectedFile; // Hover senkronize et
    fileManagerNeedsRedraw = true;
  } else if (key.normalizedKey == "w") {
    if (selectedFile > 0) selectedFile--;
    fmHoverIndex = selectedFile;
    fileManagerNeedsRedraw = true;
  } else if (key.normalizedKey == "s") {
    if (selectedFile < fileCount - 1) selectedFile++;
    fmHoverIndex = selectedFile;
    fileManagerNeedsRedraw = true;

  // Enter: dizine gir veya dosyayı aç
  } else if (key.isEnter) {
    if (fileCount == 0) return;
    String n = fileList[selectedFile];
    if (n.startsWith("[")) {
      currentFilePath += n.substring(1, n.length() - 1) + "/";
      selectedFile = 0; scanFiles();
    } else {
      editFile(n);
    }
    fileManagerNeedsRedraw = true;

  // E: Eyuditor'da aç (her zaman)
  } else if (key.normalizedKey == "e" || key.normalizedKey == "E") {
    if (fileCount > 0 && !fileList[selectedFile].startsWith("["))
      editFile(fileList[selectedFile]);

  // N: Yeni dosya oluştur
  } else if (key.normalizedKey == "n" || key.normalizedKey == "N") {
    renamingActive = true; renamingInput = ""; fileMenuAction = "new";
    desktopMessage = "Yeni dosya adi: (Enter=olustur, ESC=iptal)";
    fileManagerNeedsRedraw = true;

  // R: Yeniden adlandır
  } else if (key.normalizedKey == "r" || key.normalizedKey == "R") {
    if (fileCount > 0 && !fileList[selectedFile].startsWith("[")) {
      fileSourcePath = fileList[selectedFile];
      renamingInput = fileList[selectedFile];  // Mevcut adla başla
      renamingActive = true; fileMenuAction = "rename";
      desktopMessage = "Yeni ad: (Enter=onayla, ESC=iptal)";
      fileManagerNeedsRedraw = true;
    }

  // C: Dosyayı kopyala (hedef yol sor)
  } else if (key.normalizedKey == "c" || key.normalizedKey == "C") {
    if (fileCount > 0 && !fileList[selectedFile].startsWith("[")) {
      fileSourcePath = currentFilePath + fileList[selectedFile];
      renamingInput  = fileSourcePath;   // hedef yol girişi
      fileMenuAction = "copy_dest";
      desktopMessage = "Kopyala hedef: (Enter=kopyala, ESC=iptal)";
      fileManagerNeedsRedraw = true;
    }

  // M: Dosyayı taşı (hedef yol sor)
  } else if (key.normalizedKey == "m" || key.normalizedKey == "M") {
    if (fileCount > 0 && !fileList[selectedFile].startsWith("[")) {
      fileSourcePath = currentFilePath + fileList[selectedFile];
      renamingInput  = fileSourcePath;
      fileMenuAction = "move_dest";
      desktopMessage = "Tasi hedef: (Enter=tasi, ESC=iptal)";
      fileManagerNeedsRedraw = true;
    }

  // I: Dosya bilgisi (metadata)
  } else if (key.normalizedKey == "i" || key.normalizedKey == "I") {
    if (fileCount > 0)
      showFileMetadata(fileList[selectedFile].startsWith("[")
        ? fileList[selectedFile].substring(1, fileList[selectedFile].length()-1)
        : fileList[selectedFile]);

  // D: Sil
  } else if (key.normalizedKey == "d" || key.normalizedKey == "D") {
    if (fileCount > 0 && !fileList[selectedFile].startsWith("[")) {
      deleteFile(fileList[selectedFile]);
    }

  // ESC: Üst dizin / çıkış
  } else if (key.isEscape) {
    if (currentFilePath != "/") {
      int ls = currentFilePath.lastIndexOf('/', currentFilePath.length() - 2);
      currentFilePath = (ls >= 0) ? currentFilePath.substring(0, ls+1) : "/";
      selectedFile = 0; scanFiles();
      fileManagerNeedsRedraw = true;
    } else {
      showFileManager = false; currentApp = "";
      desktopNeedsRedraw = true; staticBackgroundDrawn = false;
    }
  }
}

// ============================================================
// === ESDOS SHELL ===========================================
// ============================================================

void addEsdosOutput(String line) {
  if (esdosOutputCount < 15) esdosOutput[esdosOutputCount++] = line;
  else {
    for (int i = 0; i < 14; i++) esdosOutput[i] = esdosOutput[i+1];
    esdosOutput[14] = line;
  }
  esdosNeedsRedraw = true;
}

// Yardımcı: tam yol
static String esdosFullPath(String p) {
  if (p.length() == 0) return esdosCurrentPath;
  if (p.startsWith("/") || p.startsWith("http://") || p.startsWith("https://")) return p;
  return esdosCurrentPath + p;
}

void executeEsdosCommand(String cmd) {
  cmd.trim();
  if (cmd.length() == 0) { addEsdosOutput(""); return; }

  // Geçmiş
  if (esdosHistoryCount < 10) esdosHistory[esdosHistoryCount++] = cmd;
  else { for (int i = 0; i < 9; i++) esdosHistory[i] = esdosHistory[i+1]; esdosHistory[9] = cmd; }
  esdosHistoryIndex = esdosHistoryCount;

  addEsdosOutput(esdosCurrentPath + "> " + cmd);

  // Komut + parametreyi ayır
  int sp = cmd.indexOf(' ');
  String c = (sp == -1) ? cmd : cmd.substring(0, sp); c.toLowerCase();
  String p = (sp == -1) ? "" : cmd.substring(sp + 1); p.trim();
  
  // Tırnak işareti soyma: "dosya adi" -> dosya adi
  auto stripQuotes = [](String s) -> String {
    s.trim();
    if (s.startsWith("\"") && s.endsWith("\"") && s.length() >= 2)
      return s.substring(1, s.length() - 1);
    return s;
  };
  p = stripQuotes(p);

  // ── Dosya sistemi ──────────────────────────────────────────
  if (c == "ls" || c == "dir") {
    String path = p.length() > 0 ? esdosFullPath(p) : esdosCurrentPath;
    // Bazi SD lib versiyonlari trailing slash ile dizin acamiyor
    String openPath = path;
    if (openPath.length() > 1 && openPath.endsWith("/")) openPath.remove(openPath.length()-1);
    File r = SD.open(openPath);
    if (!r || !r.isDirectory()) { addEsdosOutput("Hata: klasor acilamadi: " + openPath); return; }
    int count = 0;
    while (count < 30) {
      File e = r.openNextFile();
      if (!e) break;
      String nm = String(e.name());
      int sl = nm.lastIndexOf('/');
      if (sl >= 0) nm = nm.substring(sl + 1);
      addEsdosOutput( (e.isDirectory() ? "[DIR]  " : "[FILE] ") + nm
                     + (e.isDirectory() ? "" : "  (" + String(e.size()) + " B)") );
      e.close(); count++;
    }
    r.close();
    addEsdosOutput("--- " + String(count) + " eleman ---");

  } else if (c == "cd") {
      if (p == ".." || p.length() == 0 || p == "/") {
        if (p == "/") esdosCurrentPath = "/";
        else {
          int ls = esdosCurrentPath.lastIndexOf('/', esdosCurrentPath.length() - 2);
          esdosCurrentPath = (ls >= 0) ? esdosCurrentPath.substring(0, ls+1) : "/";
        }
        addEsdosOutput(esdosCurrentPath);
      } else {
        String np = esdosFullPath(p);
        if (!np.endsWith("/")) np += "/";
        File d = SD.open(np);
        if (d && d.isDirectory()) { 
          esdosCurrentPath = np; 
          addEsdosOutput(esdosCurrentPath); 
        } else {
          // Sondaki / olmadan dene (bazı SD lib farkları için)
          if (np.endsWith("/")) np.remove(np.length()-1);
          d = SD.open(np);
          if (d && d.isDirectory()) {
            esdosCurrentPath = np + "/";
            addEsdosOutput(esdosCurrentPath);
          } else addEsdosOutput("Hata: Dizin bulunamadi: " + p);
        }
        if (d) d.close();
      }

  } else if (c == "pwd") {
    addEsdosOutput(esdosCurrentPath);

  } else if (c == "cat" || c == "type") {
    // Dosyayı ekrana yaz
    String path = esdosFullPath(p);
    File f = SD.open(path);
    if (!f || f.isDirectory()) { addEsdosOutput("Dosya bulunamadi: " + p); return; }
    int lines = 0;
    String line;
    while (f.available() && lines < 20) {
      char ch = f.read();
      if (ch == '\n' || ch == '\r') {
        if (line.length() > 0 || ch == '\n') { addEsdosOutput(line); line = ""; lines++; }
      } else line += ch;
    }
    if (line.length() > 0) addEsdosOutput(line);
    if (f.available()) addEsdosOutput("... (truncated)");
    f.close();

  } else if (c == "head") {
    // İlk N satır
    int n = 5;
    String path = esdosCurrentPath + p;
    if (p.indexOf(' ') != -1) {
      n = p.substring(0, p.indexOf(' ')).toInt();
      path = esdosFullPath(p.substring(p.indexOf(' ') + 1));
    } else path = esdosFullPath(p);
    File f = SD.open(path);
    if (!f) { addEsdosOutput("Bulunamadi: " + p); return; }
    int cnt = 0; String ln;
    while (f.available() && cnt < n) {
      char ch = f.read();
      if (ch == '\n') { addEsdosOutput(ln); ln = ""; cnt++; }
      else if (ch != '\r') ln += ch;
    }
    f.close();

  } else if (c == "touch" || c == "mk") {
    // Boş dosya oluştur
    String path = esdosFullPath(p);
    File f = SD.open(path, FILE_WRITE);
    if (f) { f.close(); addEsdosOutput("Olusturuldu: " + p); }
    else addEsdosOutput("Olusturulamadi!");

  } else if (c == "mkdir") {
    String path = esdosFullPath(p);
    bool ok = SD.mkdir(path);
    addEsdosOutput(ok ? "Klasor olusturuldu." : "Hata (zaten var?)");

  } else if (c == "rm" || c == "del") {
    String path = esdosFullPath(p);
    bool ok = SD.remove(path);
    addEsdosOutput(ok ? "Silindi: " + p : "Silinemedi!");

  } else if (c == "rmdir") {
    String path = esdosFullPath(p);
    bool ok = SD.rmdir(path);
    addEsdosOutput(ok ? "Klasor silindi." : "Silinemedi (bos olmayabilir).");

  } else if (c == "cp" || c == "copy") {
    int sep = p.indexOf(' ');
    if (sep == -1) { addEsdosOutput("Kullanim: cp <kaynak> <hedef>"); return; }
    String src = esdosFullPath(p.substring(0, sep));
    String dst = esdosFullPath(p.substring(sep+1));
    File s = SD.open(src), d = SD.open(dst, FILE_WRITE);
    if (s && d) { while(s.available()) d.write(s.read()); addEsdosOutput("Kopyalandi."); }
    else addEsdosOutput("Kopyalama hatasi!");
    if (s) s.close(); if (d) d.close();

  } else if (c == "mv" || c == "move" || c == "ren") {
    int sep = p.indexOf(' ');
    if (sep == -1) { addEsdosOutput("Kullanim: mv <kaynak> <hedef>"); return; }
    String src = esdosFullPath(p.substring(0, sep));
    String dst = esdosFullPath(p.substring(sep+1));
    bool ok = SD.rename(src, dst);
    addEsdosOutput(ok ? "Tasindi / yeniden adlandirildi." : "Hata!");

  } else if (c == "size" || c == "fsize") {
    String path = esdosFullPath(p);
    File f = SD.open(path);
    if (f) { addEsdosOutput(p + ": " + String(f.size()) + " B"); f.close(); }
    else addEsdosOutput("Bulunamadi.");

  } else if (c == "df") {
    // SD kart boşluk bilgisi (yaklaşık)
    if (sdCardPresent) {
      uint64_t total = SD.totalBytes(), used = SD.usedBytes();
      char buf[60];
      snprintf(buf, sizeof(buf), "Toplam: %llu KB  Kullanilan: %llu KB",
               total/1024, used/1024);
      addEsdosOutput(String(buf));
    } else if (usbPresent) {
      uint64_t total = CH375FS.totalBytes(), used = CH375FS.usedBytes();
      char buf[80];
      snprintf(buf, sizeof(buf), "USB Toplam: %llu MB  Kullanilan: %llu MB",
               total/(1024*1024), used/(1024*1024));
      addEsdosOutput(String(buf));
    } else addEsdosOutput("Hafiza birimi algilanamadi.");

  } else if (c == "usbinfo") {
    if (usbPresent) {
      addEsdosOutput("CH375 USB Host: Bagli ve Monte Edilmis");
      uint64_t total = CH375FS.totalBytes(), used = CH375FS.usedBytes();
      char buf[80];
      snprintf(buf, sizeof(buf), "Kapasite: %llu MB | Dolu: %llu MB", total / (1024*1024), used / (1024*1024));
      addEsdosOutput(String(buf));
    } else {
      addEsdosOutput("CH375 USB Host: Bagli degil.");
    }

  } else if (c == "usbls") {
    if (!usbPresent) { addEsdosOutput("USB bellek takili degil."); return; }
    CH375::CH375FileImpl dir = CH375FS.open("/", CH375::READ_ONLY);
    if (dir && dir.isDirectory()) {
      CH375::FileInfo info;
      while (dir.openNextFile(info)) {
        char buf[280];
        if (info.isDir) {
          snprintf(buf, sizeof(buf), "[DIR] %s", info.name);
        } else {
          snprintf(buf, sizeof(buf), "%s (%u Bytes)", info.name, (unsigned)info.size);
        }
        addEsdosOutput(String(buf));
      }
      dir.close();
    } else {
      addEsdosOutput("Dosya bulunamadi veya listelenemedi.");
    }

  } else if (c == "usbcat") {
    if (!usbPresent) { addEsdosOutput("USB bellek takili degil."); return; }
    if (p.length() == 0) { addEsdosOutput("Kullanim: usbcat <dosya>"); return; }
    String path = p;
    if (!path.startsWith("/")) path = "/" + path;
    CH375::CH375FileImpl file = CH375FS.open(path, CH375::READ_ONLY);
    if (file) {
      uint8_t buffer[64];
      while (file.available()) {
        int readBytes = file.read(buffer, sizeof(buffer));
        if (readBytes <= 0) break;
        String line = "";
        for (int i = 0; i < readBytes; i++) {
          line += (char)buffer[i];
        }
        addEsdosOutput(line);
      }
      file.close();
    } else {
      addEsdosOutput("Dosya acilamadi.");
    }

  } else if (c == "usbrm") {
    if (!usbPresent) { addEsdosOutput("USB bellek takili degil."); return; }
    if (p.length() == 0) { addEsdosOutput("Kullanim: usbrm <dosya>"); return; }
    String path = p;
    if (!path.startsWith("/")) path = "/" + path;
    if (CH375FS.remove(path)) {
      addEsdosOutput("Silindi: " + path);
    } else {
      addEsdosOutput("Silme hatasi!");
    }


  // ── Script çalıştır ───────────────────────────────────────
  } else if (c == "run") {
    if (p.length() == 0) { addEsdosOutput("Kullanim: run <dosya.eyu> [-fps] [-debug]"); return; }
    
    bool showFPS = p.indexOf("-fps") != -1;
    bool showDebug = p.indexOf("-debug") != -1;
    String file = p; 
    file.replace("-fps", ""); 
    file.replace("-debug", ""); 
    file.trim();
    if (!file.endsWith(".eyu")) file += ".eyu";
    
    String fullPath = esdosFullPath(file);
    if (!SD.exists(fullPath)) {
      if (SD.exists("/scripts/" + file)) fullPath = "/scripts/" + file;
    }
    
    if (SD.exists(fullPath)) {
      String msg = "Baslatiliyor: " + file;
      if (showFPS) msg += " (FPS)";
      if (showDebug) msg += " (Debug)";
      addEsdosOutput(msg);
      
      vmShowFps = showFPS;
      vmDebugActive = showDebug;
      runEyuScript(fullPath);
    } else {
      addEsdosOutput("Hata: dosya bulunamadi: " + file);
    }

  } else if (c == "sdreset" || c == "sdretry" || c == "sd") {
    addEsdosOutput("SD kart yeniden deneniyor...");
    retrySDCard();
    addEsdosOutput(sdCardPresent ? "SD Kart Baglandi OK!" : "SD Kart Baglanti Hatasi!");

  } else if (c == "edit" || c == "open") {
    String path = esdosFullPath(p);
    openEyuditor(path);
    showEsdos = false;

  } else if (c == "fm" || c == "files") {
    showEsdos = false;
    openFileManager();

  // ── Sistem bilgisi ────────────────────────────────────────
  } else if (c == "mem" || c == "free") {
    // SRAM (Internal RAM)
    uint32_t sramTotal = heap_caps_get_total_size(MALLOC_CAP_INTERNAL);
    uint32_t sramFree = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    uint32_t sramMin = heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL);
    uint32_t sramMax = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
    
    // PSRAM (External RAM)
    uint32_t psramTotal = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
    uint32_t psramFree = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    uint32_t psramMin = heap_caps_get_minimum_free_size(MALLOC_CAP_SPIRAM);
    uint32_t psramMax = heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM);

    addEsdosOutput("--- INTERNAL SRAM ---");
    addEsdosOutput("Total: " + String(sramTotal/1024) + " KB | Free: " + String(sramFree/1024) + " KB");
    addEsdosOutput("MinFree: " + String(sramMin/1024) + " KB | MaxBlk: " + String(sramMax/1024) + " KB");
    
    if (psramTotal > 0) {
      addEsdosOutput("--- EXTERNAL PSRAM ---");
      addEsdosOutput("Total: " + String(psramTotal/1024) + " KB | Free: " + String(psramFree/1024) + " KB");
      addEsdosOutput("MinFree: " + String(psramMin/1024) + " KB | MaxBlk: " + String(psramMax/1024) + " KB");
    } else {
      addEsdosOutput("--- NO PSRAM DETECTED ---");
    }

  } else if (c == "uptime") {
    uint32_t s = millis() / 1000;
    char buf[30]; snprintf(buf, sizeof(buf), "%02d:%02d:%02d", s/3600, (s%3600)/60, s%60);
    addEsdosOutput("Uptime: " + String(buf));

  } else if (c == "temp" || c == "temperature") {
    float t = getChipTemperature();
    addEsdosOutput("Cip Sicakligi: " + String(t, 1) + " C");

  } else if (c == "ver" || c == "version") {
    addEsdosOutput("EyudiOS S3 Edition - EsDOS v2.1");
    addEsdosOutput("ESP32-S3 @ " + String(ESP.getCpuFreqMHz()) + " MHz");

  } else if (c == "reset" || c == "reboot") {
    addEsdosOutput("Yeniden baslatiliyor...");
    delay(500); ESP.restart();

  // ── WiFi ──────────────────────────────────────────────────
  } else if (c == "wifi") {
    if (p == "status") {
      bool connected = (WiFi.status() == WL_CONNECTED);
      wifiConnected = connected;
      addEsdosOutput(connected
        ? "Bagli: " + WiFi.SSID() + " | IP: " + WiFi.localIP().toString()
        : "Bagli degil.");
    } else if (p == "off" || p == "disconnect") {
      WiFi.disconnect(true);
      vTaskDelay(pdMS_TO_TICKS(200));
      wifiConnected = false;
      addEsdosOutput("WiFi kesildi.");
    } else if (p == "scan") {
      addEsdosOutput("Taranıyor...");
      WiFi.mode(WIFI_STA);
      int n = WiFi.scanNetworks();
      if (n <= 0) { addEsdosOutput("Ag bulunamadi."); }
      else {
        for (int i = 0; i < n && i < 8; i++)
          addEsdosOutput(String(i) + ": " + WiFi.SSID(i) + " (" + WiFi.RSSI(i) + " dBm)");
      }
    } else if (p.indexOf(' ') != -1) {
      // wifi SSID pass — non-blocking spinner ile baglan
      int sp2 = p.indexOf(' ');
      String ssid = p.substring(0, sp2); ssid.replace("\"",""); ssid.trim();
      String pass = p.substring(sp2+1); pass.replace("\"",""); pass.trim();

      WiFi.mode(WIFI_STA);
      WiFi.disconnect(false);
      vTaskDelay(pdMS_TO_TICKS(100));
      WiFi.begin(ssid.c_str(), pass.c_str());
      addEsdosOutput("Baglaniliyor: " + ssid);
      esdosNeedsRedraw = true;

      // Spinner ile max 15 saniye bekle
      const char* spin = "|/-\\";
      int tick = 0;
      unsigned long t0 = millis();
      while (WiFi.status() != WL_CONNECTED && millis() - t0 < 15000) {
        desktopMessage = String("WiFi... ") + spin[tick % 4];
        tick++; esdosNeedsRedraw = true;
        if (systemMutex && xSemaphoreTakeRecursive(systemMutex, pdMS_TO_TICKS(20)) == pdTRUE) {
          drawEsdos(); videodisplay.show();
          xSemaphoreGiveRecursive(systemMutex);
        }
        vTaskDelay(pdMS_TO_TICKS(300));
      }
      desktopMessage = "";
      if (WiFi.status() == WL_CONNECTED) {
        wifiConnected = true;
        currentSSID = ssid;
        wifiSSID = ssid; wifiPassword = pass;
        saveWiFiCredentials();
        addEsdosOutput("Baglandi! IP: " + WiFi.localIP().toString());
      } else {
        wifiConnected = false;
        WiFi.disconnect(true);
        addEsdosOutput("HATA: Baglanamadi. SSID/sifre kontrol edin.");
      }
    } else {
      addEsdosOutput("wifi status|disconnect|scan|<SSID> <sifre>");
    }

  } else if (c == "ip") {
    addEsdosOutput(WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : "Bagli degil.");

  } else if (c == "ping") {
    if (p.length() == 0) { addEsdosOutput("Kullanim: ping <host>"); return; }
    addEsdosOutput("Adrese paket gonderiliyor...");
    pingHost(p);
    addEsdosOutput(lastPingResult);

  } else if (c == "services") {
    currentApp = "Services";
    desktopNeedsRedraw = true;

  // ── HTTP ──────────────────────────────────────────────────
  } else if (c == "httpget") {
    if (!p.startsWith("http")) { addEsdosOutput("Kullanim: httpget <url>"); return; }
    // Gerçek WiFi durumu kontrolü
    if (WiFi.status() != WL_CONNECTED) {
      wifiConnected = false;
      addEsdosOutput("HATA: WiFi bagli degil! Once: wifi <ssid> <sifre>");
      return;
    }
    wifiConnected = true;
    // Non-blocking HTTP GET with spinner animation
    addEsdosOutput("Baglaniliyor: " + p.substring(0, 40));
    esdosNeedsRedraw = true;
    
    // Use a local struct to pass URL + result back
    struct HttpGetResult { String url; String result; bool done; };
    static HttpGetResult hgr;
    hgr.url = p; hgr.result = ""; hgr.done = false;
    
    xTaskCreatePinnedToCore([](void* pv) {
      HttpGetResult* r = (HttpGetResult*)pv;
      HTTPClient http; http.setTimeout(10000);
      http.begin(r->url);
      http.addHeader("User-Agent", "EyudiOS/1.0");
      int code = http.GET();
      if (code == 200) {
        String body = http.getString();
        r->result = "HTTP 200 OK (" + String(body.length()) + " B)\n";
        // First 5 lines
        int idx = 0, nl = 0;
        while (idx < (int)body.length() && nl < 5) {
          int end = body.indexOf('\n', idx);
          if (end == -1) { r->result += body.substring(idx); break; }
          r->result += body.substring(idx, end) + "\n";
          idx = end + 1; nl++;
        }
      } else {
        const char* desc[] = {"","","","",
          "Gecersiz Istek","Yetkisiz","","Yasak","Bulunamadi","","","","","","",
          "Sunucu Hatasi","Kotu Gecit","Kullanılamiyor"};
        if (code == 404) r->result = "404 Sayfa Bulunamadi";
        else if (code == 403) r->result = "403 Erisim Engellendi";
        else if (code == -1) r->result = "Baglanti Kurulamadi";
        else if (code == -11) r->result = "Zaman Asimi";
        else r->result = "HTTP Hata: " + String(code);
      }
      http.end();
      r->done = true;
      vTaskDelete(NULL);
    }, "esdos_http", 8192, &hgr, 1, nullptr, 0);
    
    // Spinner loop (~10 saniye max, 500ms adim)
    const char* spin = "|/-\\";
    int tick = 0;
    unsigned long start = millis();
    while (!hgr.done && millis() - start < 12000) {
      desktopMessage = String("httpget... ") + spin[tick % 4];
      tick++; esdosNeedsRedraw = true;
      // Ekranı güncelle
      if (systemMutex && xSemaphoreTakeRecursive(systemMutex, pdMS_TO_TICKS(30)) == pdTRUE) {
        drawEsdos(); videodisplay.show();
        xSemaphoreGiveRecursive(systemMutex);
      }
      vTaskDelay(pdMS_TO_TICKS(300));
    }
    desktopMessage = "";
    // Sonuçları yaz
    if (hgr.done) {
      int idx = 0;
      while (idx < (int)hgr.result.length()) {
        int end = hgr.result.indexOf('\n', idx);
        if (end == -1) { addEsdosOutput(hgr.result.substring(idx)); break; }
        String ln = hgr.result.substring(idx, end);
        if (ln.length() > 0) addEsdosOutput(ln);
        idx = end + 1;
      }
    } else {
      addEsdosOutput("Zaman asimi (12s)!");
    }

  } else if (c == "httpget-save" || c == "wget") {
    // wget <url> <dosya>
    int sep = p.indexOf(' ');
    if (sep == -1) { addEsdosOutput("Kullanim: wget <url> <dosya>"); return; }
    String url = p.substring(0, sep), fp = esdosFullPath(p.substring(sep+1));
    addEsdosOutput("Indiriliyor: " + url.substring(0, 36));
    
    // Background download with spinner
    struct WgetResult { String url; String fp; String result; bool done; };
    static WgetResult wgr;
    wgr.url = url; wgr.fp = fp; wgr.result = ""; wgr.done = false;
    
    xTaskCreatePinnedToCore([](void* pv) {
      WgetResult* r = (WgetResult*)pv;
      HTTPClient http; http.setTimeout(15000);
      http.begin(r->url);
      int code = http.GET();
      if (code == 200) {
        if (sdCardPresent) {
          File f = SD.open(r->fp, FILE_WRITE);
          if (f) {
            WiFiClient* stream = http.getStreamPtr();
            uint8_t buf[256]; int total = 0;
            while (http.connected()) {
              int avail = stream->available();
              if (avail) {
                int rd = stream->readBytes(buf, min(avail, 256));
                f.write(buf, rd); total += rd;
              } else { vTaskDelay(1); }
              if (total > 500000) break; // 500KB limit
            }
            f.close();
            r->result = "Kaydedildi: " + r->fp + " (" + String(total) + " B)";
          } else r->result = "HATA: Dosya acilamadi: " + r->fp;
        } else r->result = "HATA: SD kart yok";
      } else {
        if (code == -1) r->result = "HATA: Baglanti kurulamadi";
        else r->result = "HATA: HTTP " + String(code);
      }
      http.end();
      r->done = true;
      vTaskDelete(NULL);
    }, "esdos_wget", 8192, &wgr, 1, nullptr, 0);
    
    const char* spin2 = "|/-\\";
    int tick2 = 0;
    unsigned long st2 = millis();
    while (!wgr.done && millis() - st2 < 30000) {
      desktopMessage = String("wget... ") + spin2[tick2 % 4];
      tick2++; esdosNeedsRedraw = true;
      if (systemMutex && xSemaphoreTakeRecursive(systemMutex, pdMS_TO_TICKS(30)) == pdTRUE) {
        drawEsdos(); videodisplay.show();
        xSemaphoreGiveRecursive(systemMutex);
      }
      vTaskDelay(pdMS_TO_TICKS(300));
    }
    desktopMessage = "";
    addEsdosOutput(wgr.done ? wgr.result : "Zaman asimi (30s)!");



  } else if (c == "wad") {
    int sp = p.indexOf(' ');
    String sub = (sp == -1) ? p : p.substring(0, sp);
    String param = (sp == -1) ? "" : p.substring(sp + 1);

    if (sub == "load") {
      if (param.length() == 0) { addEsdosOutput("Kullanim: wad load <path>"); return; }
      String full = esdosFullPath(param);
      if (wadMgr.openWad(full)) addEsdosOutput("WAD Yuklendi: " + full);
      else addEsdosOutput("WAD Yukleme Hatasi! (Magic error?)");
    } else if (sub == "info") {
      addEsdosOutput(wadMgr.getWadInfo());
    } else if (sub == "find") {
      int idx = wadMgr.findLump(param.c_str());
      if (idx != -1) {
        uint32_t s = wadMgr.getLumpSize(idx);
        addEsdosOutput("Bulundu: " + param + " (Index: " + String(idx) + ", Size: " + String(s) + " B)");
        void* data = wadMgr.getLumpData(idx);
        if (data) addEsdosOutput("-> PSRAM'e cekildi OK.");
      } else addEsdosOutput("Lump bulunamadi: " + param);
    } else if (sub == "close") {
      wadMgr.closeWad();
      addEsdosOutput("WAD Kapatildi.");
    } else {
      addEsdosOutput("wad load|info|find|close");
    }

  // ── Yardım ────────────────────────────────────────────────
  } else if (c == "runbg") {
    // Arkaplanda script calistir
    if (p.length() == 0) { addEsdosOutput("Kullanim: runbg <dosya.eyu>"); return; }
    String path = esdosFullPath(p);
    int slot = runBgScript(path);
    if (slot >= 0) {
      addEsdosOutput("BG[" + String(slot) + "] baslatildi: " + path);
    } else {
      addEsdosOutput("HATA: Tum BG slotlar dolu! (max " + String(MAX_BG_TASKS) + ")");
    }

  } else if (c == "bglist" || c == "bgs") {
    // Aktif bg task'lari listele
    bool any = false;
    for (int i = 0; i < MAX_BG_TASKS; i++) {
      if (bgTasks[i].active) {
        addEsdosOutput("BG[" + String(i) + "] " + bgTasks[i].filePath);
        any = true;
      }
    }
    if (!any) addEsdosOutput("Aktif BG task yok.");

  } else if (c == "killbg" || c == "bgkill") {
    // Bir BG task'ini durdur
    if (p.length() == 0) { addEsdosOutput("Kullanim: killbg <slot_no>"); return; }
    int slot = p.toInt();
    if (slot >= 0 && slot < MAX_BG_TASKS && bgTasks[slot].active) {
      stopBgScript(slot);
      addEsdosOutput("BG[" + String(slot) + "] durduruldu.");
    } else {
      addEsdosOutput("Gecersiz slot ya da slot aktif degil.");
    }

  } else if (c == "help") {
    int page = p.toInt();
    if (page <= 1) {
      addEsdosOutput("=== EsDOS Yardim [1/3] ===");
      addEsdosOutput("ls/dir [yol]     - Dizin listele");
      addEsdosOutput("cd <yol|..>      - Dizin degistir");
      addEsdosOutput("pwd              - Mevcut yol");
      addEsdosOutput("cat/type <dosya> - Dosyayi goster");
      addEsdosOutput("mkdir/rmdir <ad> - Klasor islemleri");
      addEsdosOutput("rm/del/cp/mv     - Dosya islemleri");
      addEsdosOutput("> help 2 (Script & Sistem)");
    } else if (page == 2) {
      addEsdosOutput("=== EsDOS Yardim [2/3] ===");
      addEsdosOutput("run <dosya.eyu>  - Script calistir");
      addEsdosOutput("runbg/bglist/bgkill - Arkaplan");
      addEsdosOutput("edit/open <dosya>- Editor / fm (dosya yon)");
      addEsdosOutput("mem/free/uptime  - Sistem bilgisi");
      addEsdosOutput("ver/reset/temp   - Cihaz bilgisi");
      addEsdosOutput("wifi status/scan - Ag islemleri");
      addEsdosOutput("> help 3 (HW & Multimedya)");
    } else {
      addEsdosOutput("=== EsDOS Yardim [3/3] ===");
      addEsdosOutput("bmp <f> [x] [y]  - Resim goster");
      addEsdosOutput("play <f>         - Muzik oynat");
      addEsdosOutput("audio stop|pause - Ses kontrol");
      addEsdosOutput("tone <hz> <ms>   - Bip sesi");
      addEsdosOutput("pinmode <p> <m>  - GPIO Mode");
      addEsdosOutput("dw/dr/ar <p> [v] - Digital/Analog IO");
      addEsdosOutput("ftp/browse/cls   - Diger komutlar");
    }


  } else if (c == "ftp") {
    if (p.startsWith("start")) {
      if (!wifiConnected) { addEsdosOutput("Hata: Once WiFi bagla!"); }
      else { 
        ftpDebugMode = (p.indexOf("-debug") != -1);
        startFtpServer(); 
        String msg = "FTP baslatildi: " + WiFi.localIP().toString() + ":21";
        if (ftpDebugMode) msg += " [-DEBUG ACTIVE]";
        addEsdosOutput(msg); 
      }
    } else if (p == "stop") {
      stopFtpServer(); ftpDebugMode = false; addEsdosOutput("FTP durduruldu.");
    } else if (p == "status" || p == "") {
      addEsdosOutput(ftpRunning ? ("FTP AKTIF | IP: " + WiFi.localIP().toString() + ":21") : "FTP KAPALI");
    } else {
      addEsdosOutput("ftp start [-debug] | stop | status");
    }

  } else if (c == "browse" || c == "browser") {
    showEsdos = false; currentApp = "";
    webBrowserContent = "";
    webBrowserScrollY = 0;
    webBrowserURL = p.length() > 0 ? p : "http://";
    createWindow("Browser", "Web Browser", 10, 24, 380, 260);
    // URL'yi arka planda yükle
    if (p.length() > 4 && wifiConnected) {
      webBrowserLoading = true;
      webBrowserStatus = "Baglaniliyor...";
      desktopNeedsRedraw = true;
      String* urlPtr = new String(webBrowserURL);
      xTaskCreatePinnedToCore([](void* pv) {
        String* urlStr = (String*)pv;
        webBrowserLoad(*urlStr);
        delete urlStr;
        desktopNeedsRedraw = true; // Force final redraw
        vTaskDelete(NULL);
      }, "wb_load", 16384, urlPtr, 1, nullptr, 0);
    } else if (!wifiConnected) {
      webBrowserContent = "*** WiFi bagli degil! ***\nOnce WiFi kur: wifi <ssid> <sifre>";
    }


  } else if (c == "audio" || c == "play" || c == "tone") {
    if (c == "tone") {
      int hz=440,ms=100; sscanf(p.c_str(),"%d %d",&hz,&ms);
      audioBeep(hz, ms); addEsdosOutput("Bip: " + String(hz) + " Hz");
    } else if (p == "init") {
      initAudio(); addEsdosOutput("Ses motoru baslatildi.");
    } else if (p.startsWith("play ")) {
      String path = esdosFullPath(p.substring(5)); path.trim();
      bool ok = audioPlay(path);
      addEsdosOutput(ok ? ("Oynatiliyor: " + path) : "Hata: " + path);
    } else if (p.startsWith("radio") || p == "radio") {
      String path = "/radio.txt";
      if (p.startsWith("radio ")) {
        path = p.substring(6); path.trim();
      }
      path = esdosFullPath(path);
      addEsdosOutput("Radyo baglaniyor (" + path + ")...");
      bool ok = playRadioFromConfig(path);
      addEsdosOutput(ok ? "Radyo oynatiliyor." : "Radyo baglanti hatasi!");
    } else if (p == "stop") {
      audioStop(); addEsdosOutput("Ses durduruldu.");
    } else if (p == "pause") {
      audioPause(); addEsdosOutput("Duraklandi/Devam edildi.");
    } else if (p.startsWith("volume ")) {
      audioSetVolume(p.substring(7).toInt());
      addEsdosOutput("Ses: " + String(audioVolume) + "%");
    } else if (p == "status") {
      addEsdosOutput(audioStatus());
    } else if (p.length() > 2) {
      String path = esdosFullPath(p);
      bool ok;
      if (path.endsWith(".txt")) {
        addEsdosOutput("Radyo baglaniyor (" + path + ")...");
        ok = playRadioFromConfig(path);
      } else {
        ok = audioPlay(path);
      }
      addEsdosOutput(ok ? ("Oynatiliyor: " + path) : "Hata: " + path);
    } else {
      addEsdosOutput("audio play|radio|stop|pause|volume|status | tone <hz> <ms>");
    }

  } else if (c == "bmp" || c == "view") {
    if (p.length() == 0) { addEsdosOutput("Kullanim: bmp [preview] <dosya> [x] [y]"); }
    else {
      String pathPart = p;
      bool previewMode = false;
      if (p.startsWith("preview ")) {
        previewMode = true;
        pathPart = p.substring(8);
      }
      
      char path[64]; int x=0, y=0;
      if (sscanf(pathPart.c_str(),"%63s %d %d", path, &x, &y) >= 1) {
        String full = esdosFullPath(path);
        // Temizlik: [ ] karakterlerini temizle (folder tagları karışmış olabilir)
        full.replace("[", ""); full.replace("]", "");
        
        if (previewMode) {
          if (!SD.exists(full)) {
            addEsdosOutput("HATA: Dosya bulunamadi: " + full);
            return;
          }
          addEsdosOutput("Onizleme aciliyor: " + full);
          // Preview modal logic
          bool oldShow = showEsdos;
          bmpPreviewActive = true; 
          videodisplay.fillScreen(COLOR_BACKGROUND);
          videodisplay.show();
          renderBMP(full.c_str(), 0, 0); 
          videodisplay.show(); // Ekrana yansıt
          addEsdosOutput("Geri donmek için bir tusa basin...");
          videodisplay.show(); // Metni göster
          
          unsigned long start = millis();
          keyEventAvailable = false; // Reset events
          usb_left_button = false;   // Reset mouse
          vTaskDelay(pdMS_TO_TICKS(300)); // Enter yankısını bekle
          
          while(millis() - start < 30000) {
            processUSBInput();
            if (keyEventAvailable || usb_left_button) break;
            esp_task_wdt_reset();
            vTaskDelay(pdMS_TO_TICKS(20));
          }
          bmpPreviewActive = false;
          showEsdos = oldShow;
          esdosNeedsRedraw = true;
        } else {
          addEsdosOutput("Resim yansitiliyor: " + full);
          renderBMP(full.c_str(), x, y);
        }
      }
    }

  } else if (c == "pinmode") {
    int pin; char mode[16];
    if (sscanf(p.c_str(),"%d %15s", &pin, mode) >= 2) {
      int m = INPUT;
      if (strcmp(mode,"OUTPUT")==0 || strcmp(mode,"OUT")==0) m=OUTPUT;
      else if (strcmp(mode,"INPUT_PULLUP")==0 || strcmp(mode,"PULLUP")==0) m=INPUT_PULLUP;
      pinMode(pin, m); addEsdosOutput("Pin " + String(pin) + " mod: " + mode);
    } else addEsdosOutput("Kullanim: pinmode <pin> <INPUT/OUTPUT/PULLUP>");

  } else if (c == "dw") {
    int pin; char val[8];
    if (sscanf(p.c_str(),"%d %7s", &pin, val) >= 2) {
      int v = (strcmp(val,"HIGH")==0 || strcmp(val,"1")==0) ? HIGH : LOW;
      digitalWrite(pin, v); addEsdosOutput("Pin " + String(pin) + " -> " + val);
    } else addEsdosOutput("Kullanim: dw <pin> <1/0/HIGH/LOW>");

  } else if (c == "dr") {
    int pin = p.toInt();
    int v = digitalRead(pin);
    addEsdosOutput("Pin " + String(pin) + " Digital: " + String(v));

  } else if (c == "ar") {
    int pin = p.toInt();
    int v = analogRead(pin);
    addEsdosOutput("Pin " + String(pin) + " Analog: " + String(v));


  } else if (c == "unzip" || c == "extract") {
    int sp2 = p.indexOf(' ');
    if (sp2 == -1) { addEsdosOutput("Kullanim: unzip <dosya.zip> <hedef_klasor>"); }
    else {
      String zf = esdosFullPath(p.substring(0, sp2));
      String zd = esdosFullPath(p.substring(sp2+1));
      addEsdosOutput("Cikartiliyor: " + zf + " -> " + zd);
      String zErr;
      if (unzipFile(zf, zd, zErr)) addEsdosOutput("Basarili: " + zd);
      else addEsdosOutput("Hata: " + zErr);
    }

  } else if (c == "theme") {
    if (p == "list") {
      addEsdosOutput("0=Green  1=Blue  2=Amber  3=Cyan");
    } else if (p.length() > 0 && isDigit(p[0])) {
      int t = p.toInt();
      applyTheme(t); saveThemeSettings();
      addEsdosOutput("Tema degistirildi: " + p);
    } else {
      addEsdosOutput("Kullanim: theme <0-3> | theme list");
    }

  } else if (c == "wallpaper" || c == "wp") {
    if (p == "list") {
      addEsdosOutput("0=Static 1=Matrix 2=Starfield 3=Plasma 4=Rain 5=BMP");
    } else if (p.startsWith("load ")) {
      String file = esdosFullPath(p.substring(5));
      extern bool loadWallpaperBMP(const char* path);
      addEsdosOutput("BMP yukleniyor: " + file);
      if (loadWallpaperBMP(file.c_str())) addEsdosOutput("Wallpaper basariyla kuruldu.");
      else addEsdosOutput("HATA: BMP yuklenemedi!");
    } else if (p.length() > 0 && isDigit(p[0])) {
      wallpaperMode = constrain(p.toInt(), 0, 5);
      saveThemeSettings(); staticBackgroundDrawn = false; desktopNeedsRedraw = true;
      addEsdosOutput("Wallpaper modu: " + p);
    } else {
      addEsdosOutput("Kullanim: wp <0-5> | wp load <dosya.bmp> | wp list");
    }

  } else if (c == "audio") {
    int sp2 = p.indexOf(' ');
    String sub = (sp2 == -1) ? p : p.substring(0, sp2); sub.toLowerCase();
    String val = (sp2 == -1) ? "" : p.substring(sp2 + 1); val.trim();

    if (sub == "play") {
      if (val.length() == 0) { addEsdosOutput("Kullanim: audio play <dosya>"); return; }
      String path = esdosFullPath(val);
      audioPlay(path);
      addEsdosOutput("Caliniyor: " + path);
    } else if (sub == "stop") {
      audioStop();
      addEsdosOutput("Audio durduruldu.");
    } else if (sub == "pause") {
      audioPause();
      addEsdosOutput("Audio duraklatildi.");
    } else if (sub == "vol" || sub == "volume") {
      int v = val.toInt();
      audioSetVolume(v);
      addEsdosOutput("Ses seviyesi: " + String(v));
    } else if (sub == "status") {
      addEsdosOutput("Audio Status: " + audioStatus());
    } else {
      addEsdosOutput("Alt komutlar: play, stop, pause, vol, status");
    }

  } else if (c == "bt") {
    if (p == "scan" || p == "pair") {
      addEsdosOutput("Bluetooth taramasi baslatildi (biraz surebilir)...");
      pairNewBluetoothDevice();
      addEsdosOutput("Tarama bitti.");
    } else if (p == "reset") {
      clearBluetoothDevices();
      addEsdosOutput("BLE cihaz listesi temizlendi.");
    } else if (p == "off") {
      toggleBluetoothService(false);
      addEsdosOutput("Bluetooth servis kapatildi.");
    } else if (p == "on") {
      toggleBluetoothService(true);
      addEsdosOutput("Bluetooth servis acildi.");
    } else {
      addEsdosOutput("Alt komutlar: scan, reset, on, off");
    }

  } else if (c == "grid" || c == "layout") {
    if (p == "list") {
      addEsdosOutput("0=Normal  1=Buyuk Grid  2=Liste");
    } else if (p.length() > 0 && isDigit(p[0])) {
      desktopGridMode = constrain(p.toInt(), 0, 2);
      saveThemeSettings(); staticBackgroundDrawn = false; desktopNeedsRedraw = true;
      const char* names[] = {"Normal", "Buyuk Grid (4 sutun)", "Minimal Liste"};
      addEsdosOutput(String("Layout: ") + names[desktopGridMode]);
    } else {
      addEsdosOutput("Kullanim: grid <0-2> | grid list");
    }


  } else if (c == "wad") {
    if (p.startsWith("load ")) {
      String path = esdosFullPath(p.substring(5));
      if (wadMgr.openWad(path)) {
        selectedWadPath = path;
        addEsdosOutput("WAD yuklendi: " + path);
      } else {
        addEsdosOutput("HATA: WAD acilamadi!");
      }
    } else if (p == "info" || p == "status") {
      addEsdosOutput(wadMgr.getWadInfo());
    } else if (p.startsWith("find ")) {
      String name = p.substring(5); name.trim();
      int idx = wadMgr.findLump(name.c_str());
      if (idx != -1) addEsdosOutput("Lump [" + String(idx) + "]: " + String(wadMgr.getLumpSize(idx)) + " bytes");
      else addEsdosOutput("Lump bulunamadi.");
    } else if (p == "close") {
      wadMgr.closeWad(); addEsdosOutput("WAD kapatildi.");
    } else {
      addEsdosOutput("wad load|status|find|close");
    }

  } else if (c == "clear" || c == "cls") {
    esdosOutputCount = 0; addEsdosOutput("EsDOS v2.1 - help yazin");

  } else if (c == "exit" || c == "quit") {
    showEsdos = false; currentApp = ""; desktopNeedsRedraw = true; staticBackgroundDrawn = false;

  } else {
    addEsdosOutput("Bilinmeyen komut: " + c + "  (help yazin)");
  }
}

// ── EsDOS Klavye Handler ─────────────────────────────────────
void handleEsdosInputKey(ParsedKey key) {
  // wifi_inline mod
  if (vkbCallbackType == "wifi_inline") {
    if (key.isEnter) {
      String targetSSID = vkbCallbackParam;
      String targetPass = esdosCommand;
      
      desktopMessage = "Baglaniliyor: " + targetSSID;
      
      WiFi.begin(targetSSID.c_str(), targetPass.c_str());
      int connected = 0;
      for (int i = 0; i < 20; i++) {
        if (WiFi.status() == WL_CONNECTED) { connected = 1; break; }
        vTaskDelay(pdMS_TO_TICKS(500));
      }
      
      if (connected) {
        wifiSSID = targetSSID;
        wifiPassword = targetPass;
        wifiConnected = true;
        currentSSID = targetSSID;
        saveWiFiCredentials();
        desktopMessage = "WiFi Baglandi: " + wifiSSID;
      } else {
        wifiConnected = false;
        desktopMessage = "WiFi Hatasi: Baglanamadi!";
      }

      vkbCallbackType = "";
      esdosCommand = ""; esdosCursorPos = 0;
      showEsdos = false; currentApp = ""; desktopNeedsRedraw = true; staticBackgroundDrawn = false;
    } else if (key.isEscape) {
      vkbCallbackType = ""; esdosCommand = ""; esdosCursorPos = 0;
      showEsdos = false; currentApp = ""; desktopNeedsRedraw = true; staticBackgroundDrawn = false;
      desktopMessage = "WiFi iptal edildi.";
    } else if (key.isBackspace) {
      if (esdosCursorPos > 0) {
        esdosCommand.remove(esdosCursorPos - 1, 1);
        esdosCursorPos--;
      }
    } else if (key.isArrow) {
      if (key.arrowDirection == 2 && esdosCursorPos > 0) esdosCursorPos--;
      if (key.arrowDirection == 3 && esdosCursorPos < (int)esdosCommand.length()) esdosCursorPos++;
    } else if (key.printableChar != 0) {
      String left = esdosCommand.substring(0, esdosCursorPos);
      String right = esdosCommand.substring(esdosCursorPos);
      esdosCommand = left + key.printableChar + right;
      esdosCursorPos++;
    }
    esdosNeedsRedraw = true;
    return;
  }

  if (key.isEnter) { 
    executeEsdosCommand(esdosCommand); 
    esdosCommand = ""; esdosCursorPos = 0;
  }
  else if (key.isEscape) { 
    showEsdos = false; currentApp = ""; desktopNeedsRedraw = true; staticBackgroundDrawn = false; 
    esdosCursorPos = 0;
  }
  else if (key.isBackspace) { 
    if (esdosCursorPos > 0) {
      esdosCommand.remove(esdosCursorPos - 1, 1);
      esdosCursorPos--;
    }
  }
  else if (key.isArrow) {
    if (key.arrowDirection == 0 && esdosHistoryIndex > 0) {
      esdosCommand = esdosHistory[--esdosHistoryIndex];
      esdosCursorPos = esdosCommand.length();
    }
    else if (key.arrowDirection == 1 && esdosHistoryIndex < esdosHistoryCount - 1) {
      esdosCommand = esdosHistory[++esdosHistoryIndex];
      esdosCursorPos = esdosCommand.length();
    }
    else if (key.arrowDirection == 2 && esdosCursorPos > 0) {
      esdosCursorPos--;
    }
    else if (key.arrowDirection == 3 && esdosCursorPos < (int)esdosCommand.length()) {
      esdosCursorPos++;
    }
  }
  else if (key.isCtrl && (key.normalizedKey == "c" || key.normalizedKey == "C")) {
    esdosCommand = ""; esdosCursorPos = 0; addEsdosOutput("^C");
  }
  else if (key.printableChar != 0) { 
    String left = esdosCommand.substring(0, esdosCursorPos);
    String right = esdosCommand.substring(esdosCursorPos);
    esdosCommand = left + key.printableChar + right;
    esdosCursorPos++;
  }
  esdosNeedsRedraw = true;
}

// ============================================================
// === EYUDITOR ===============================================
// ============================================================

void openEyuditor(String path) {
  eyuditorPath = path; eyuditorLineCount = 1; eyuditorCursorRow = 0; eyuditorCursorCol = 0;
  eyuditorFirstVisibleRow = 0;
  for (int i = 0; i < EYU_MAX_LINES; i++) eyuLines[i][0] = '\0';
  if (sdCardPresent) {
    File f = SD.open(path);
    if (f) {
      int r = 0, c = 0;
      while (f.available()) {
        char ch = f.read();
        if (ch == '\r') continue;
        if (ch == '\n') { r++; c = 0; eyuditorLineCount++; if (r >= EYU_MAX_LINES) break; }
        else if (c < EYU_MAX_COLS) { eyuLines[r][c++] = ch; eyuLines[r][c] = '\0'; }
      }
      f.close();
    }
  }
  showEyuditor = true; currentApp = "Eyuditor"; eyuditorNeedsRedraw = true; staticBackgroundDrawn = false;
}

void saveEyuditor() {
  if (sdCardPresent) {
    File f = SD.open(eyuditorPath, FILE_WRITE);
    if (f) {
      for (int i = 0; i < eyuditorLineCount; i++) f.println(eyuLines[i]);
      f.close();
      desktopMessage = "Kaydedildi: " + eyuditorPath;
    }
  }
}

void handleEyuditorInputKey(ParsedKey key) {
  if (key.isEscape) {
    showEyuditor = false; currentApp = "";
    staticBackgroundDrawn = false; desktopNeedsRedraw = true;
    return;
  }
  if (key.isArrow) {
    if (key.arrowDirection == 0 && eyuditorCursorRow > 0) {
      eyuditorCursorRow--;
      int len = (int)strlen(eyuLines[eyuditorCursorRow]);
      if (eyuditorCursorCol > len) eyuditorCursorCol = len;
    }
    else if (key.arrowDirection == 1 && eyuditorCursorRow < eyuditorLineCount - 1) {
      eyuditorCursorRow++;
      int len = (int)strlen(eyuLines[eyuditorCursorRow]);
      if (eyuditorCursorCol > len) eyuditorCursorCol = len;
    }
    else if (key.arrowDirection == 2 && eyuditorCursorCol > 0) {
      eyuditorCursorCol--;
    }
    else if (key.arrowDirection == 3 && eyuditorCursorCol < (int)strlen(eyuLines[eyuditorCursorRow])) {
      eyuditorCursorCol++;
    }
  } else if (key.isBackspace) {
    if (eyuditorCursorCol > 0) {
      int len = strlen(eyuLines[eyuditorCursorRow]);
      memmove(&eyuLines[eyuditorCursorRow][eyuditorCursorCol-1],
              &eyuLines[eyuditorCursorRow][eyuditorCursorCol],
              len - eyuditorCursorCol + 1);
      eyuditorCursorCol--;
    } else if (eyuditorCursorRow > 0) {
      int pr = eyuditorCursorRow - 1;
      int pl = strlen(eyuLines[pr]);
      int cl = strlen(eyuLines[eyuditorCursorRow]);
      if (pl + cl <= EYU_MAX_COLS) {
        strcat(eyuLines[pr], eyuLines[eyuditorCursorRow]);
        for (int i = eyuditorCursorRow; i < eyuditorLineCount - 1; i++) strcpy(eyuLines[i], eyuLines[i+1]);
        eyuditorLineCount--; eyuditorCursorRow--; eyuditorCursorCol = pl;
      } else {
        desktopMessage = "Satir cok uzun, birlestirilemez!";
      }
    }
  } else if (key.isEnter) {
    if (eyuditorLineCount < EYU_MAX_LINES) {
      // Step 1: Shift lines down
      for (int i = eyuditorLineCount; i > eyuditorCursorRow + 1; i--) {
        strncpy(eyuLines[i], eyuLines[i-1], EYU_MAX_COLS);
        eyuLines[i][EYU_MAX_COLS] = '\0';
      }
      // Step 2: Split current line
      strncpy(eyuLines[eyuditorCursorRow+1], &eyuLines[eyuditorCursorRow][eyuditorCursorCol], EYU_MAX_COLS);
      eyuLines[eyuditorCursorRow+1][EYU_MAX_COLS] = '\0';
      eyuLines[eyuditorCursorRow][eyuditorCursorCol] = '\0';
      
      eyuditorLineCount++; eyuditorCursorRow++; eyuditorCursorCol = 0;
    } else {
      desktopMessage = "Maksimum satir sayisina ulasildi!";
    }
  } else if (key.printableChar != 0) {
    int len = strlen(eyuLines[eyuditorCursorRow]);
    if (len < EYU_MAX_COLS && eyuditorCursorCol < EYU_MAX_COLS) {
      memmove(&eyuLines[eyuditorCursorRow][eyuditorCursorCol+1],
              &eyuLines[eyuditorCursorRow][eyuditorCursorCol],
              len - eyuditorCursorCol + 1);
      eyuLines[eyuditorCursorRow][eyuditorCursorCol] = key.printableChar;
      eyuditorCursorCol++;
    }
  }
  eyuditorNeedsRedraw = true;
}
