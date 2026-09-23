#include "Globals.h"
#include "EyudioApps.h"
#include <esp_task_wdt.h>


void drawSplashScreen() {
  videodisplay.clear(COLOR_BACKGROUND);
  videodisplay.setCursor(SCREEN_WIDTH / 2 - 40, SCREEN_HEIGHT / 2 - 20);
  videodisplay.setTextColor(COLOR_TITLE); videodisplay.print("EyudiOS S3");
  videodisplay.setCursor(SCREEN_WIDTH / 2 - 60, SCREEN_HEIGHT / 2 + 10);
  videodisplay.setTextColor(COLOR_TEXT); videodisplay.print(splashMessage.c_str());
  videodisplay.show();
}

void showSystemError(String title, String message) {
  // Serial Log
  Serial.println("\n\n!!! EYUDIOS SISTEM HATASI (GSOD) !!!");
  Serial.println("Hata Basligi: " + title);
  Serial.println("Hata Mesaji:  " + message);
  Serial.println("-------------------------------------\n");

  // State Management
  systemInErrorState = true;
  runningScript = false;
  bmpPreviewActive = false;
  showEyuditor = false;
  showFileManager = false;

  // Deadlock Recovery: Force release mutex several times if we were holding it
  if (systemMutex) {
    for (int i = 0; i < 5; i++) xSemaphoreGiveRecursive(systemMutex);
    
    // Now take it properly for drawing
    if (xSemaphoreTakeRecursive(systemMutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
      uint16_t green = videodisplay.RGB(0, 100, 0);
      uint16_t darkgreen = videodisplay.RGB(0, 60, 0);
      uint16_t white = videodisplay.RGB(255, 255, 255);
      uint16_t cyan = videodisplay.RGB(0, 255, 255);
      
      videodisplay.fillScreen(green);
      videodisplay.fillRect(0, 0, SCREEN_WIDTH, 30, darkgreen);
      
      videodisplay.setTextSize(2);
      videodisplay.setTextColor(white);
      videodisplay.setCursor(10, 8);
      videodisplay.print(title.c_str());
      
      videodisplay.setTextSize(1);
      videodisplay.setCursor(10, 50);
      videodisplay.println("EyudiOS bir sorunla karsilasti ve durduruldu.");
      videodisplay.println("Eger bu hata ekranini ilk kez goruyorsaniz,");
      videodisplay.println("cihazinizi yeniden baslatin.");
      videodisplay.println("");
      videodisplay.println("Teknik Bilgi:");
      videodisplay.setTextColor(cyan);
      videodisplay.println(message.c_str());
      videodisplay.println("");
      videodisplay.setTextColor(white);
      videodisplay.println("-----------------------------------------------------");
      videodisplay.println("Reboot icin [R] tusuna basin");
      videodisplay.println("EsDOS'a donmek icin [ESC] tusuna basin");
      
      videodisplay.show();
      xSemaphoreGiveRecursive(systemMutex);
    }
  }

  // Wait with WDT reset loop - AGGRESSIVE KEYBOARD LISTEN
  while(systemInErrorState) {
    esp_task_wdt_reset();
    
    // USB Host Klavyeyi Dinle
    processUSBInput();
    
    // Seri Port (UART) Klavyeyi Dinle - TAMPONSUZ ACİL DURUM KONTROLÜ
    if (Serial.available()) {
      int c = Serial.read();
      if (c == 'r' || c == 'R') {
        Serial.println("[SYS] Emergency Restarting...");
        delay(100);
        ESP.restart();
      }
      if (c == 27) { // ESC - EsDOS'a Acil Dönüş
        systemInErrorState = false;
        showEsdos = true;
        currentApp = "EsDOS";
        break;
      }
      // Diğer karakterler için normal süreci işlet (Buffer üzerinden)
      if (c != -1) {
          String sLine = String((char)c);
          if (Serial.available()) sLine += Serial.readStringUntil('\n');
          processSerialInput(sLine);
      }
    }
    
    vTaskDelay(pdMS_TO_TICKS(10)); // Daha hızlı tepki için 10ms
  }
  systemInErrorState = false;
}

void drawBootMenu() {
  videodisplay.clear(COLOR_BACKGROUND);
  videodisplay.setCursor(40, 40); videodisplay.setTextColor(COLOR_TITLE); videodisplay.print("Boot Menu");
  for (int i = 0; i < bootMenuCount; i++) {
    int y = 70 + i * 20; if (i == bootMenuSelection) { videodisplay.fillRect(35, y - 2, 250, 18, COLOR_ICON_BG); drawRect(35, y - 2, 250, 18, COLOR_ICON_SELECTED_BORDER); }
    videodisplay.setCursor(40, y); videodisplay.setTextColor(COLOR_TEXT); videodisplay.print(bootMenuItems[i].c_str());
  }
}

// Menu geometry taken from SystemConfig.h macros

void drawMenu() {
  // Background panel
  videodisplay.fillRoundRect(MENU_X, MENU_Y, MENU_W, MENU_H, 8, videodisplay.RGB(12,15,28));
  videodisplay.drawRoundRect(MENU_X, MENU_Y, MENU_W, MENU_H, 8, videodisplay.RGB(0,150,220));
  // Header bar
  videodisplay.fillRoundRect(MENU_X+1, MENU_Y+1, MENU_W-2, 26, 8, videodisplay.RGB(15,22,50));
  videodisplay.line(MENU_X, MENU_Y+27, MENU_X+MENU_W-1, MENU_Y+27, videodisplay.RGB(0,120,180));
  // Title
  videodisplay.setFont(NULL);
  videodisplay.setTextSize(1);
  videodisplay.setTextColor(videodisplay.RGB(0,200,255));
  videodisplay.setCursor(MENU_X + 12, MENU_Y + 9);
  videodisplay.print(sysCurrentMenu.c_str());
  // Close hint (top-right)
  videodisplay.setTextColor(videodisplay.RGB(100,110,140));
  videodisplay.setCursor(MENU_X + MENU_W - 38, MENU_Y + 9);
  videodisplay.print("[Mid=Back]");

  // Items
  for (int i = 0; i < menuItemCount; i++) {
    int iy = MENU_ITEM_Y0 + i * MENU_ITEM_H;
    bool sel = (i == menuSelection);
    bool hov = (mouseX >= MENU_X+4 && mouseX <= MENU_X+MENU_W-4 &&
                mouseY >= iy-4 && mouseY <= iy+14);
    if (sel) {
      videodisplay.fillRoundRect(MENU_X+4, iy-4, MENU_W-8, 20, 4, videodisplay.RGB(0,100,180));
      videodisplay.setTextColor(videodisplay.RGB(255,255,255));
    } else if (hov) {
      videodisplay.fillRoundRect(MENU_X+4, iy-4, MENU_W-8, 20, 4, videodisplay.RGB(20,30,55));
      videodisplay.setTextColor(videodisplay.RGB(0,200,255));
    } else {
      videodisplay.fillRoundRect(MENU_X+4, iy-4, MENU_W-8, 20, 4, videodisplay.RGB(8,10,18));
      videodisplay.setTextColor(videodisplay.RGB(180,190,220));
    }
    // Arrow indicator for selected
    if (sel) {
      videodisplay.setCursor(MENU_X+8, iy);
      videodisplay.print(">");
    }
    videodisplay.setCursor(MENU_X + 22, iy);
    videodisplay.print(menuItems[i].c_str());
  }
}


void drawVirtualKeyboard() {
  videodisplay.fillRect(10, 100, 300, 130, COLOR_STATUS_BAR); drawRect(10, 100, 300, 130, COLOR_ICON_BORDER);
  videodisplay.setCursor(15, 105); videodisplay.setTextColor(COLOR_TITLE); videodisplay.print(vkbTitle.c_str());
  videodisplay.fillRect(15, 120, 290, 20, COLOR_BACKGROUND); videodisplay.setCursor(20, 125); videodisplay.setTextColor(COLOR_TEXT); videodisplay.print(keyboardInput.c_str());
  for (int r = 0; r < 5; r++) {
    for (int c = 0; c < keyboardRowLengths[r]; c++) {
      int x = 15 + c * 25, y = 145 + r * 15;
      if (r == keyboardSelectedRow && c == keyboardSelectedCol) videodisplay.setTextColor(COLOR_ICON_HOVER); else videodisplay.setTextColor(COLOR_TEXT);
      if (r < 4) { char ch = keyboardKeyRows[r][c]; if (keyboardShift) ch = toupper(ch); videodisplay.setCursor(x, y); char s[2] = {ch, 0}; videodisplay.print(s); }
      else {
        videodisplay.setCursor(x, y);
        if (c == 0) videodisplay.print("SH"); else if (c == 1) videodisplay.print("BS"); else if (c == 2) videodisplay.print("SP"); else if (c == 3) videodisplay.print("OK"); else if (c == 4) videodisplay.print("EX");
      }
    }
  }
}

void drawFileManager() {
  extern void processAppRepeatingKeys();
  processAppRepeatingKeys();

  uint16_t acc = videodisplay.RGB(0, 180, 255);

  // Arkaplan
  videodisplay.fillRect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, videodisplay.RGB(3, 6, 10));

  // Baslik bari
  videodisplay.fillRect(0, 0, SCREEN_WIDTH, 22, videodisplay.RGB(6, 14, 28));
  videodisplay.line(0, 22, SCREEN_WIDTH - 1, 22, acc);
  videodisplay.setTextColor(acc);
  videodisplay.setCursor(4, 5);
  videodisplay.print("[FM]");
  videodisplay.setTextColor(videodisplay.RGB(180, 210, 240));
  videodisplay.setCursor(36, 5);
  String pathDisp = currentFilePath;
  if ((int)pathDisp.length() > 36) pathDisp = ".." + pathDisp.substring(pathDisp.length() - 34);
  videodisplay.print(pathDisp.c_str());

  // Kisayol ipuclari
  videodisplay.setTextColor(videodisplay.RGB(40, 90, 130));
  videodisplay.setCursor(SCREEN_WIDTH - 148, 4);
  videodisplay.print("ENT=Ac  N=Yeni  E=Edit");
  videodisplay.setCursor(SCREEN_WIDTH - 148, 14);
  videodisplay.print("D=Sil R=Ren C=Kop M=Tas");

  // Dosya listesi
  const int LIST_Y0 = 28, LINE_H = 10;
  const int MAX_VIS = (SCREEN_HEIGHT - LIST_Y0 - 22) / LINE_H;
  int startIdx = max(0, selectedFile - MAX_VIS + 1);
  if (selectedFile < startIdx) startIdx = selectedFile;

  for (int i = 0; i < MAX_VIS && (startIdx + i) < fileCount; i++) {
    int li = startIdx + i;
    int yPos = LIST_Y0 + i * LINE_H;
    bool isSel = (li == selectedFile);
    bool isHov = (li == fmHoverIndex && !isSel);
    bool isDir = fileList[li].startsWith("[");

    if (isSel) {
      videodisplay.fillRect(0, yPos - 1, SCREEN_WIDTH, LINE_H + 1, videodisplay.RGB(0, 55, 115));
      videodisplay.setTextColor(videodisplay.RGB(255, 255, 255));
    } else if (isHov) {
      // Hover highlight for ALL items (files and folders)
      videodisplay.fillRect(0, yPos - 1, SCREEN_WIDTH, LINE_H + 1, videodisplay.RGB(0, 30, 65));
      videodisplay.setTextColor(isDir ? videodisplay.RGB(120, 195, 255) : videodisplay.RGB(200, 230, 200));
    } else {
      videodisplay.setTextColor(isDir ? videodisplay.RGB(80, 160, 255) : videodisplay.RGB(150, 195, 170));
    }
    videodisplay.setCursor(4, yPos);
    videodisplay.print(isDir ? "/" : " ");
    String nm = fileList[li];
    if (isDir) nm = nm.substring(1, nm.length() - 1);
    if ((int)nm.length() > 42) nm = nm.substring(0, 39) + "..";
    videodisplay.setCursor(14, yPos);
    videodisplay.print(nm.c_str());
    if (isSel && !isDir) {
      videodisplay.setTextColor(videodisplay.RGB(30, 70, 110));
      videodisplay.setCursor(SCREEN_WIDTH - 30, yPos);
      videodisplay.print("I=?");
    }
  }


  // Alt bilgi bari
  int sbY = SCREEN_HEIGHT - 12;
  videodisplay.fillRect(0, sbY, SCREEN_WIDTH, 12, videodisplay.RGB(5, 12, 22));
  videodisplay.line(0, sbY, SCREEN_WIDTH - 1, sbY, videodisplay.RGB(0, 80, 130));
  videodisplay.setTextColor(videodisplay.RGB(60, 120, 160));
  videodisplay.setCursor(4, sbY + 2);
  if (desktopMessage.length() > 0) {
    String msg = desktopMessage;
    if ((int)msg.length() > 54) msg = msg.substring(0, 51) + "..";
    videodisplay.print(msg.c_str());
  } else {
    char sbuf[56];
    snprintf(sbuf, sizeof(sbuf), "Dosyalar:%d  Sec:%d  ESC=Ust/Cik  I=Bilgi", fileCount, selectedFile + 1);
    videodisplay.print(sbuf);
  }

  // Giris diyalogu (rename / yeni dosya / copy-hedef / move-hedef)
  bool dlgActive = renamingActive || (fileMenuAction == "copy_dest") || (fileMenuAction == "move_dest");
  if (dlgActive) {
    videodisplay.fillRect(20, 108, 280, 48, videodisplay.RGB(6, 16, 36));
    drawRect(20, 108, 280, 48, acc);
    videodisplay.setTextColor(acc);
    videodisplay.setCursor(26, 114);
    if (fileMenuAction == "rename")     videodisplay.print("Yeni ad giriniz:");
    else if (fileMenuAction == "copy_dest") videodisplay.print("Kopyala hedef yol:");
    else if (fileMenuAction == "move_dest") videodisplay.print("Tasi hedef yol:");
    else                                    videodisplay.print("Yeni dosya adi:");
    videodisplay.setTextColor(videodisplay.RGB(220, 240, 255));
    videodisplay.setCursor(26, 128);
    String disp = renamingInput;
    if ((int)disp.length() > 36) disp = ".." + disp.substring(disp.length() - 34);
    disp += ((millis() / 400) % 2 == 0) ? "_" : " ";
    videodisplay.print(disp.c_str());
    videodisplay.setTextColor(videodisplay.RGB(40, 80, 120));
    videodisplay.setCursor(26, 142);
    videodisplay.print("Enter=Onayla  ESC=Iptal");
  }

  // ── Sağ Tık Menüsü (Bağlam Menüsü) ──
  if (showFileMenu) {
    uint16_t acc = videodisplay.RGB(0, 180, 255);
    const int MW = 120, MH = 68;
    int mx = fmMenuX, my = fmMenuY;
    if (mx + MW > SCREEN_WIDTH) mx = SCREEN_WIDTH - MW;
    if (my + MH > SCREEN_HEIGHT) my = SCREEN_HEIGHT - MH;
    
    videodisplay.fillRect(mx, my, MW, MH, videodisplay.RGB(10, 20, 35));
    drawRect(mx, my, MW, MH, acc);
    
    const char* items[] = {"Ac (Enter)", "Duzenle (E)", "Adlandir (R)", "Kopyala (C)", "Ozellikler (I)", "Sil (D)"};
    for (int i = 0; i < 6; i++) {
      int y = my + 2 + i * 11;
      if (i == fmMenuSelection) {
        videodisplay.fillRect(mx + 2, y, MW - 4, 11, videodisplay.RGB(0, 100, 200));
        videodisplay.setTextColor(videodisplay.RGB(255, 255, 255));
      } else {
        videodisplay.setTextColor(videodisplay.RGB(150, 200, 240));
      }
      videodisplay.setCursor(mx + 6, y);
      videodisplay.print(items[i]);
    }
  }
}

void drawFileMenu() {
  // FM ana çiziminde hallediliyor
}





// ──────────────────────────────────────────────────
// ESDOS — Pencere stili terminal + Script Modu
// ──────────────────────────────────────────────────
void drawEsdos() {
  extern void processAppRepeatingKeys();
  processAppRepeatingKeys();

  if (runningScript) {
    // ── Script Modu: temiz tam ekran ──
    // (ScriptEngine kendi çizimini yapıyor, burası sadece overlay)
    // Üst bilgi şerit göster:
    videodisplay.fillRect(0, 0, SCREEN_WIDTH, 12, videodisplay.RGB(8, 5, 2));
    videodisplay.setTextColor(videodisplay.RGB(255, 180, 40));
    videodisplay.setCursor(4, 2);
    videodisplay.print("EyuScript >> ESC=Stop");
    // Cursor blink
    if ((millis() / 300) % 2 == 0) {
      videodisplay.fillRect(SCREEN_WIDTH - 10, 2, 6, 8, videodisplay.RGB(255,150,0));
    }
    return;
  }

  const int TITLE_H  = 24;
  const int CMD_H    = 20;
  const int LINE_H   = 10;
  const int OUT_LINES = (SCREEN_HEIGHT - TITLE_H - CMD_H) / LINE_H;

  // ── Arka plan ──
  videodisplay.fillRect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, videodisplay.RGB(3, 5, 3));

  // ── Başlık barı ──
  uint16_t acc = videodisplay.RGB(0, 200, 70);
  videodisplay.fillRect(0, 0, SCREEN_WIDTH, TITLE_H, videodisplay.RGB(6, 20, 8));
  videodisplay.line(0, TITLE_H - 1, SCREEN_WIDTH - 1, TITLE_H - 1, acc);

  videodisplay.setTextColor(acc);
  videodisplay.setCursor(4, 4);
  videodisplay.print("[S] EsDOS Shell");

  videodisplay.setTextColor(videodisplay.RGB(50, 100, 60));
  videodisplay.setCursor(SCREEN_WIDTH - 100, 4);
  videodisplay.print("ESC=Exit");

  videodisplay.setTextColor(videodisplay.RGB(40, 90, 50));
  videodisplay.setCursor(4, 15);
  videodisplay.print(esdosCurrentPath.c_str());

  // ── Çıktı satırları ──
  // En son çıktılar altta görünsün
  int startOutput = max(0, esdosOutputCount - OUT_LINES);
  for (int i = 0; i < OUT_LINES && (startOutput + i) < esdosOutputCount; i++) {
    int lineIdx = startOutput + i;
    int yPos = TITLE_H + i * LINE_H + 1;
    String line = esdosOutput[lineIdx];

    // Renk: > ile başlayan prompt=yeşil, [ERROR]=kırmızı, [DIR]=cyan, normal=gri
    String lineLower = line;
    lineLower.toLowerCase();
    uint16_t lc;
    if (line.indexOf("> ") >= 0 && line.indexOf("> ") < 5)
      lc = videodisplay.RGB(0, 220, 80);          // komut satırı
    else if (line.startsWith("[DIR]"))
      lc = videodisplay.RGB(80, 160, 255);         // dizin
    else if (line.startsWith("[FILE]"))
      lc = videodisplay.RGB(180, 220, 185);        // dosya
    else if (lineLower.indexOf("error") >= 0 || lineLower.indexOf("not found") >= 0)
      lc = videodisplay.RGB(255, 80, 80);          // hata
    else if (line.startsWith("---") || line.startsWith("==="))
      lc = videodisplay.RGB(0, 140, 50);           // ayraç
    else
      lc = videodisplay.RGB(140, 180, 145);        // normal


    videodisplay.setTextColor(lc);
    String disp = line;
    if ((int)disp.length() > SCREEN_WIDTH / 6) disp = disp.substring(0, SCREEN_WIDTH / 6 - 1) + ">";
    videodisplay.setCursor(4, yPos);
    videodisplay.print(disp.c_str());
  }

  // ── Komut giriş satırı ──
  int cmdY = SCREEN_HEIGHT - CMD_H;
  videodisplay.fillRect(0, cmdY, SCREEN_WIDTH, CMD_H, videodisplay.RGB(5, 18, 7));
  videodisplay.line(0, cmdY, SCREEN_WIDTH - 1, cmdY, acc);

  String prompt = esdosCurrentPath + "> ";
  videodisplay.setTextColor(acc);
  videodisplay.setCursor(4, cmdY + 5);
  videodisplay.print(prompt.c_str());

  String cmd = esdosCommand;
  int maxLen = (SCREEN_WIDTH - 8 - (int)prompt.length() * 6) / 6;
  int startIdx = 0;
  if ((int)cmd.length() > maxLen) {
    // Scroll to keep cursor visible
    if (esdosCursorPos < maxLen) startIdx = 0;
    else if (esdosCursorPos >= (int)cmd.length()) startIdx = cmd.length() - maxLen;
    else startIdx = esdosCursorPos - maxLen + 5; // keep some context
    
    if (startIdx < 0) startIdx = 0;
    if (startIdx > (int)cmd.length() - maxLen) startIdx = cmd.length() - maxLen;
    cmd = cmd.substring(startIdx, startIdx + maxLen);
  }

  videodisplay.setTextColor(videodisplay.RGB(220, 240, 220));
  videodisplay.setCursor(4 + prompt.length() * 6, cmdY + 5);
  videodisplay.print(cmd.c_str());

  // Komut imleci blink
  if ((millis() / 400) % 2 == 0) {
    int curRelPos = esdosCursorPos - startIdx;
    if (curRelPos >= 0 && curRelPos <= maxLen) {
      int curX = 4 + (prompt.length() + curRelPos) * 6;
      if (curX < SCREEN_WIDTH - 8)
        videodisplay.fillRect(curX, cmdY + 4, 5, 9, videodisplay.RGB(0, 220, 70));
    }
  }

  // Alt ipucu
  videodisplay.setTextColor(videodisplay.RGB(35, 70, 40));
  videodisplay.setCursor(SCREEN_WIDTH - 100, cmdY + 12);
  videodisplay.print("help=komutlar");
}

// ──────────────────────────────────────────────────
// EYUDITOR — Tam Ekran Pencere Editörü
// ──────────────────────────────────────────────────
void drawEyuditor() {
  extern void processAppRepeatingKeys();
  processAppRepeatingKeys();

  const int TITLE_H   = 24;  // başlık barı yüksekliği
  const int STATUS_H  = 12;  // alt durum çubuğu
  const int LNUM_W    = 28;  // satır numarası genişliği
  const int LINE_H    = 10;  // satır yüksekliği (px)
  const int CHAR_W    =  6;  // yaklaşık karakter genişliği
  const int visLines  = (SCREEN_HEIGHT - TITLE_H - STATUS_H) / LINE_H;

  // Scroll: imleç görünür alanda mı?
  if (eyuditorCursorRow < eyuditorFirstVisibleRow)
    eyuditorFirstVisibleRow = eyuditorCursorRow;
  if (eyuditorCursorRow >= eyuditorFirstVisibleRow + visLines)
    eyuditorFirstVisibleRow = eyuditorCursorRow - visLines + 1;

  // ── Arka plan ──
  videodisplay.fillRect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, videodisplay.RGB(4, 8, 5));

  // ── Başlık barı ──
  uint16_t hdrBg  = videodisplay.RGB(8, 28, 12);
  uint16_t hdrAcc = videodisplay.RGB(0, 200, 70);
  videodisplay.fillRect(0, 0, SCREEN_WIDTH, TITLE_H, hdrBg);
  videodisplay.line(0, TITLE_H - 1, SCREEN_WIDTH - 1, TITLE_H - 1, hdrAcc);

  // İkon (editör sembolü)
  videodisplay.setTextColor(hdrAcc);
  videodisplay.setCursor(4, 7);
  videodisplay.print("[E]");

  // Dosya adı (kıs)
  String fname = eyuditorPath;
  int slash = fname.lastIndexOf('/');
  if (slash >= 0) fname = fname.substring(slash + 1);
  if (fname.length() > 22) fname = fname.substring(0, 19) + "..";
  videodisplay.setTextColor(videodisplay.RGB(210, 240, 215));
  videodisplay.setCursor(30, 7);
  videodisplay.print(fname.c_str());

  // Satır:sütun
  char pos[16];
  snprintf(pos, sizeof(pos), "L%d C%d", eyuditorCursorRow + 1, eyuditorCursorCol + 1);
  videodisplay.setTextColor(videodisplay.RGB(100, 170, 110));
  videodisplay.setCursor(SCREEN_WIDTH - 70, 7);
  videodisplay.print(pos);

  // Kısayol ipuçları
  videodisplay.setTextColor(videodisplay.RGB(60, 110, 70));
  videodisplay.setCursor(SCREEN_WIDTH - 110, 16);
  videodisplay.print("^S=Save ESC=Exit");

  // ── Satır numarası kolonu ──
  videodisplay.fillRect(0, TITLE_H, LNUM_W - 2, SCREEN_HEIGHT - TITLE_H - STATUS_H,
                        videodisplay.RGB(6, 16, 8));
  videodisplay.line(LNUM_W - 2, TITLE_H, LNUM_W - 2,
                   SCREEN_HEIGHT - STATUS_H - 1, videodisplay.RGB(20, 60, 25));

  // ── Satırları çiz ──
  for (int i = 0; i < visLines; i++) {
    int lineIdx = eyuditorFirstVisibleRow + i;
    if (lineIdx >= eyuditorLineCount) break;

    int yPos = TITLE_H + i * LINE_H + 1;
    bool isCurrentLine = (lineIdx == eyuditorCursorRow);

    // Aktif satır arka planı
    if (isCurrentLine)
      videodisplay.fillRect(LNUM_W, yPos - 1, SCREEN_WIDTH - LNUM_W, LINE_H + 1,
                            videodisplay.RGB(10, 28, 14));

    // Satır numarası
    char lnum[6];
    snprintf(lnum, sizeof(lnum), "%4d", lineIdx + 1);
    videodisplay.setCursor(1, yPos);
    videodisplay.setTextColor(isCurrentLine ? videodisplay.RGB(0, 200, 70)
                                            : videodisplay.RGB(40, 80, 45));
    videodisplay.print(lnum);

    // Satır içeriği
    String lineStr = String(eyuLines[lineIdx]);
    // Syntax renklendirme
    uint16_t lineColor;
    if (lineStr.startsWith("#") || lineStr.startsWith("//"))
      lineColor = videodisplay.RGB(70, 100, 75);   // yorum → koyu gri-yeşil
    else if (lineStr.startsWith(":"))
      lineColor = videodisplay.RGB(80, 180, 255);  // label → mavi
    else if (lineStr.startsWith("set") || lineStr.startsWith("SET"))
      lineColor = videodisplay.RGB(120, 230, 130); // set komutu → açık yeşil
    else if (lineStr.startsWith("if") || lineStr.startsWith("IF") ||
             lineStr.startsWith("goto") || lineStr.startsWith("for") ||
             lineStr.startsWith("while") || lineStr.startsWith("next") ||
             lineStr.startsWith("wend"))
      lineColor = videodisplay.RGB(255, 190, 80);  // kontrol akışı → turuncu
    else if (lineStr.startsWith("print") || lineStr.startsWith("println"))
      lineColor = videodisplay.RGB(200, 210, 255); // print → açık mavi
    else
      lineColor = videodisplay.RGB(180, 220, 185); // normal metin

    videodisplay.setTextColor(lineColor);
    String displayLine = lineStr;
    int maxChars = (SCREEN_WIDTH - LNUM_W) / CHAR_W;
    if ((int)displayLine.length() > maxChars)
      displayLine = displayLine.substring(0, maxChars - 1) + ">";
    videodisplay.setCursor(LNUM_W + 1, yPos);
    videodisplay.print(displayLine.c_str());
  }

  // ── İmleç çiz (blink) ──
  bool cursorVisible = ((millis() / 400) % 2 == 0);
  if (cursorVisible) {
    int ci = eyuditorCursorRow - eyuditorFirstVisibleRow;
    if (ci >= 0 && ci < visLines) {
      int cx = LNUM_W + 1 + eyuditorCursorCol * CHAR_W;
      int cy = TITLE_H + ci * LINE_H + 1;
      if (cx < SCREEN_WIDTH)
        videodisplay.fillRect(cx, cy, 2, LINE_H - 1, videodisplay.RGB(0, 230, 80));
    }
  }

  // ── Alt durum çubuğu ──
  int sbY = SCREEN_HEIGHT - STATUS_H;
  videodisplay.fillRect(0, sbY, SCREEN_WIDTH, STATUS_H, videodisplay.RGB(6, 20, 8));
  videodisplay.line(0, sbY, SCREEN_WIDTH - 1, sbY, videodisplay.RGB(0, 120, 40));
  videodisplay.setTextColor(videodisplay.RGB(80, 150, 90));
  videodisplay.setCursor(4, sbY + 2);
  char sbuf[48];
  snprintf(sbuf, sizeof(sbuf), "Lines:%d  Ctrl+S: Save  ESC: Exit",
           eyuditorLineCount);
  videodisplay.print(sbuf);

  // ── Scroll bar (sağ kenar) ──
  if (eyuditorLineCount > visLines) {
    int sbH   = SCREEN_HEIGHT - TITLE_H - STATUS_H;
    int thH   = max(4, sbH * visLines / eyuditorLineCount);
    int thY   = TITLE_H + (sbH - thH) * eyuditorFirstVisibleRow / max(1, eyuditorLineCount - visLines);
    videodisplay.fillRect(SCREEN_WIDTH - 3, TITLE_H, 3, sbH, videodisplay.RGB(10, 25, 12));
    videodisplay.fillRect(SCREEN_WIDTH - 3, thY, 3, thH, videodisplay.RGB(0, 180, 60));
  }
}

void drawSystemMonitor() {
  int w = 340, h = 220;
  int x = (SCREEN_WIDTH - w)/2, y = (SCREEN_HEIGHT - h)/2;
  videodisplay.fillRect(x, y, w, h, COLOR_ICON_BG);
  drawRect(x, y, w, h, COLOR_ICON_BORDER);

  videodisplay.setCursor(x+10, y+10); videodisplay.setTextColor(COLOR_TITLE);
  videodisplay.print("System Monitor");

  videodisplay.setCursor(x+10, y+30); videodisplay.setTextColor(COLOR_TEXT);
  videodisplay.print("Free Heap:  "); videodisplay.print(ESP.getFreeHeap() / 1024); videodisplay.print(" KB");
  videodisplay.setCursor(x+10, y+42); videodisplay.print("Min Heap:   "); videodisplay.print(ESP.getMinFreeHeap() / 1024); videodisplay.print(" KB");
  videodisplay.setCursor(x+10, y+54); videodisplay.print("Max Alloc:  "); videodisplay.print(ESP.getMaxAllocHeap() / 1024); videodisplay.print(" KB");
  videodisplay.setCursor(x+10, y+66); videodisplay.print("Free PSRAM: "); videodisplay.print(ESP.getFreePsram() / 1024); videodisplay.print(" KB / "); videodisplay.print(ESP.getPsramSize()/1024); videodisplay.print(" KB");
  
  videodisplay.setCursor(x+10, y+85); videodisplay.print("Uptime: "); videodisplay.print(millis() / 1000); videodisplay.print(" s");
  videodisplay.setCursor(x+10, y+97); videodisplay.print("CPU Freq: "); videodisplay.print(ESP.getCpuFreqMHz()); videodisplay.print(" MHz");
  videodisplay.setCursor(x+10, y+109); videodisplay.print("WiFi: "); videodisplay.print(wifiConnected ? "CONNECTED" : "OFF");

  int used = ((totalHeap - freeHeap) * (w - 40)) / totalHeap;
  videodisplay.fillRect(x+20, y+140, w - 40, 12, COLOR_ICON_BG);
  drawRect(x+20, y+140, w - 40, 12, COLOR_ICON_BORDER);
  videodisplay.fillRect(x+20, y+140, used, 12, COLOR_ICON_HOVER);
  videodisplay.setCursor(x+20, y+155); videodisplay.print("RAM Usage Graph");

  videodisplay.setCursor(x+20, y+190); videodisplay.setTextColor(videodisplay.RGB(255, 100, 100));
  videodisplay.print("Press ESC to close");
}

// ── Task Manager tab state ─────────────────────────────────
static int taskManagerTab = 0; // 0=FreeRTOS Tasks, 1=BG Scripts
int taskManagerTabGlobal = 0;  // accessible externally for input

void drawTaskManager() {
  // Get window bounds
  int ox = 4, oy = 4, w = 392, h = 292;
  if (focusedWindowIndex != -1 && strcmp(windows[focusedWindowIndex].appName, "Tasks") == 0) {
    ox = windows[focusedWindowIndex].x + 2;
    oy = windows[focusedWindowIndex].y + 24;
    w  = windows[focusedWindowIndex].w - 4;
    h  = windows[focusedWindowIndex].h - 26;
  }

  taskManagerTab = taskManagerTabGlobal;

  // ── Tab Bar ──────────────────────────────────────────────
  const int TAB_H = 16;
  const int TAB_W = w / 2;

  // Tab 0: FreeRTOS Tasks
  uint16_t t0bg = (taskManagerTab == 0) ? videodisplay.RGB(0, 80, 160) : videodisplay.RGB(10, 18, 30);
  uint16_t t1bg = (taskManagerTab == 1) ? videodisplay.RGB(0, 100, 60) : videodisplay.RGB(10, 18, 30);
  videodisplay.fillRect(ox,         oy, TAB_W,     TAB_H, t0bg);
  videodisplay.fillRect(ox+TAB_W,   oy, w - TAB_W, TAB_H, t1bg);
  videodisplay.drawLine(ox,        oy,       ox + w - 1, oy,       videodisplay.RGB(0, 120, 220));
  videodisplay.drawLine(ox+TAB_W-1, oy,      ox+TAB_W-1, oy+TAB_H, videodisplay.RGB(0, 120, 220));
  videodisplay.drawLine(ox,         oy+TAB_H, ox+w-1,    oy+TAB_H, videodisplay.RGB(0, 100, 140));

  videodisplay.setFont(NULL); videodisplay.setTextSize(1);
  videodisplay.setTextColor(taskManagerTab == 0 ? videodisplay.RGB(255,255,255) : videodisplay.RGB(100,160,220));
  videodisplay.setCursor(ox + (TAB_W - 11*6)/2, oy + 4);
  videodisplay.print("FreeRTOS Tasks");

  videodisplay.setTextColor(taskManagerTab == 1 ? videodisplay.RGB(255,255,255) : videodisplay.RGB(80,180,120));
  videodisplay.setCursor(ox + TAB_W + (TAB_W - 8*6)/2, oy + 4);
  videodisplay.print("BG Scripts");

  int cy = oy + TAB_H + 2;
  int innerH = h - TAB_H - 18;

  if (taskManagerTab == 0) {
    // ── System & OS Info Tab ──────────────────────────────
    videodisplay.setTextColor(videodisplay.RGB(0, 180, 255));
    videodisplay.setCursor(ox+10, cy+5); videodisplay.print("Hardware & OS Overview");
    videodisplay.line(ox+5, cy+15, ox+w-5, cy+15, videodisplay.RGB(0, 60, 100));
    cy += 20;

    auto printStat = [&](const char* label, String val, uint16_t color = COLOR_TEXT) {
      videodisplay.setTextColor(videodisplay.RGB(150, 150, 150));
      videodisplay.setCursor(ox+15, cy); videodisplay.print(label);
      videodisplay.setTextColor(color);
      videodisplay.setCursor(ox+120, cy); videodisplay.print(val.c_str());
      cy += 14;
    };

    printStat("CPU Model:", "ESP32-S3 (Dual Core)");
    printStat("CPU Freq:", String(getCpuFrequencyMhz()) + " MHz", videodisplay.RGB(255, 200, 0));
    printStat("Total Tasks:", String(uxTaskGetNumberOfTasks()), videodisplay.RGB(0, 255, 100));
    
    cy += 10;
    videodisplay.setTextColor(videodisplay.RGB(0, 180, 255));
    videodisplay.setCursor(ox+10, cy); videodisplay.print("Memory Maps (KB)");
    videodisplay.line(ox+5, cy+10, ox+w-5, cy+10, videodisplay.RGB(0, 60, 100));
    cy += 15;

    uint32_t sTotal = heap_caps_get_total_size(MALLOC_CAP_INTERNAL) / 1024;
    uint32_t sFree  = heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024;
    uint32_t sMin   = heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL) / 1024;
    printStat("Internal SRAM:", String(sFree) + " / " + String(sTotal) + " Free");
    printStat("SRAM Min Free:", String(sMin) + " KB", sMin < 32 ? videodisplay.RGB(255, 80, 80) : videodisplay.RGB(0, 200, 100));

    uint32_t pTotal = heap_caps_get_total_size(MALLOC_CAP_SPIRAM) / 1024;
    uint32_t pFree  = heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024;
    printStat("External PSRAM:", String(pFree) + " / " + String(pTotal) + " Free", videodisplay.RGB(180, 120, 255));

    cy += 5;
    videodisplay.setTextColor(videodisplay.RGB(255, 100, 0));
    videodisplay.setCursor(ox+10, cy); videodisplay.print("System Hardware Map");
    videodisplay.line(ox+5, cy+10, ox+w-5, cy+10, videodisplay.RGB(100, 60, 0));
    cy += 15;

    printStat("MCU Model:", "ESP32-S3-DevKitC-1");
    printStat("CPU Cores:", "Dual Core (Xtensa LX7)");
    printStat("Total Tasks:", String(uxTaskGetNumberOfTasks()), videodisplay.RGB(0, 255, 100));
    printStat("Uptime:", String(millis() / 1000) + " s", videodisplay.RGB(0, 255, 255));
    
    cy += 10;
    videodisplay.setTextColor(videodisplay.RGB(0, 180, 255));
    videodisplay.setCursor(ox+10, cy); videodisplay.print("Memory Allocation (KB)");
    videodisplay.line(ox+5, cy+10, ox+w-5, cy+10, videodisplay.RGB(0, 60, 100));
    cy += 15;

    sTotal = heap_caps_get_total_size(MALLOC_CAP_INTERNAL) / 1024;
    sFree  = heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024;
    sMin   = heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL) / 1024;
    printStat("Internal SRAM:", String(sFree) + " / " + String(sTotal) + " Free");
    printStat("SRAM Min Free:", String(sMin) + " KB", sMin < 32 ? videodisplay.RGB(255, 80, 80) : videodisplay.RGB(0, 200, 100));

    pTotal = heap_caps_get_total_size(MALLOC_CAP_SPIRAM) / 1024;
    pFree  = heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024;
    printStat("External PSRAM:", String(pFree) + " / " + String(pTotal) + " Free", videodisplay.RGB(180, 120, 255));

    cy += 15;
    videodisplay.setTextColor(videodisplay.RGB(100, 100, 120));
    videodisplay.setCursor(ox+15, cy);
    videodisplay.print("[Note] RTOS Task List disabled in this build.");
    cy += 10;
    videodisplay.setCursor(ox+15, cy);
    videodisplay.print("Use 'kill <slot>' for BG scripts.");

    // Footer: summary
    int footerY = oy + h - 13;
    videodisplay.line(ox, footerY, ox+w-1, footerY, videodisplay.RGB(40,60,80));
    videodisplay.setTextColor(videodisplay.RGB(0, 140, 200));
    videodisplay.setCursor(ox+4, footerY+2);
    char fsum[48];
    snprintf(fsum, sizeof(fsum), "Uptime: %lu s  |  TAB: Sekme Degistir", millis()/1000);
    videodisplay.print(fsum);

  } else {
    // ── BG Scripts Tab ────────────────────────────────────
    // Column headers
    videodisplay.fillRect(ox, cy, w, 12, videodisplay.RGB(5, 18, 14));
    videodisplay.setTextColor(videodisplay.RGB(0, 220, 120));
    videodisplay.setCursor(ox+2,  cy+2); videodisplay.print("#");
    videodisplay.setCursor(ox+16, cy+2); videodisplay.print("Dosya Yolu");
    videodisplay.setCursor(ox+200,cy+2); videodisplay.print("Handle");
    videodisplay.setCursor(ox+248,cy+2); videodisplay.print("IPC");
    videodisplay.setCursor(ox+284,cy+2); videodisplay.print("Durum");
    videodisplay.line(ox, cy+12, ox+w-1, cy+12, videodisplay.RGB(0, 80, 50));
    cy += 14;

    bool anyBg = false;
    for (int i = 0; i < MAX_BG_TASKS; i++) {
      videodisplay.fillRect(ox, cy + i * 22, w, 21, i%2 ? videodisplay.RGB(3,10,7) : videodisplay.RGB(5,14,10));

      if (bgTasks[i].active) {
        anyBg = true;
        // Slot #
        videodisplay.setTextColor(videodisplay.RGB(0, 220, 120));
        videodisplay.setCursor(ox+2, cy+i*22+3);
        videodisplay.print(i);

        // File path (2 lines if too long)
        videodisplay.setTextColor(videodisplay.RGB(180, 240, 200));
        String fp = bgTasks[i].filePath;
        int maxC = (200 - 16) / 6;
        if ((int)fp.length() > maxC * 2) fp = fp.substring(fp.length() - maxC*2 + 3);
        String line1 = fp.substring(0, min((int)fp.length(), maxC));
        String line2 = (int)fp.length() > maxC ? fp.substring(maxC) : "";
        videodisplay.setCursor(ox+16, cy+i*22+2);  videodisplay.print(line1.c_str());
        if (line2.length() > 0) {
          videodisplay.setTextColor(videodisplay.RGB(120, 180, 140));
          videodisplay.setCursor(ox+16, cy+i*22+13); videodisplay.print(line2.c_str());
        }

        // Handle pointer (hex)
        char hbuf[12];
        if (bgTasks[i].handle) {
          snprintf(hbuf, sizeof(hbuf), "0x%06X", (unsigned)(uintptr_t)bgTasks[i].handle & 0xFFFFFF);
          videodisplay.setTextColor(videodisplay.RGB(100, 160, 255));
        } else {
          strcpy(hbuf, "NULL");
          videodisplay.setTextColor(videodisplay.RGB(130, 60, 60));
        }
        videodisplay.setCursor(ox+200, cy+i*22+3); videodisplay.print(hbuf);

        // IPC pending flag
        videodisplay.setTextColor(bgTasks[i].msgPending ? videodisplay.RGB(255,200,0) : videodisplay.RGB(60,80,60));
        videodisplay.setCursor(ox+248, cy+i*22+3);
        videodisplay.print(bgTasks[i].msgPending ? "PEND" : "---");
        if (bgTasks[i].msgPending) {
          videodisplay.setTextColor(videodisplay.RGB(200, 200, 80));
          videodisplay.setCursor(ox+248, cy+i*22+13);
          char kv[16]; snprintf(kv, 16, "%s=%s", bgTasks[i].msgKey, bgTasks[i].msgVal);
          videodisplay.print(kv);
        }

        // Status badge
        bool alive = (bgTasks[i].handle != NULL);
        videodisplay.fillRoundRect(ox+282, cy+i*22+3, 50, 12, 3,
          alive ? videodisplay.RGB(0,60,20) : videodisplay.RGB(40,10,10));
        videodisplay.drawRoundRect(ox+282, cy+i*22+3, 50, 12, 3,
          alive ? videodisplay.RGB(0,200,80) : videodisplay.RGB(200,60,60));
        videodisplay.setTextColor(alive ? videodisplay.RGB(0,220,100) : videodisplay.RGB(220,80,80));
        videodisplay.setCursor(ox+288, cy+i*22+5);
        videodisplay.print(alive ? "AKTIF" : "BITTI");
      } else {
        // Empty slot
        videodisplay.setTextColor(videodisplay.RGB(40,60,50));
        videodisplay.setCursor(ox+2, cy+i*22+3); videodisplay.print(i);
        videodisplay.setCursor(ox+16, cy+i*22+3); videodisplay.print("-- Bos Slot --");
      }
    }

    // Footer
    int footerY = oy + h - 13;
    videodisplay.line(ox, footerY, ox+w-1, footerY, videodisplay.RGB(40,60,80));
    videodisplay.setTextColor(videodisplay.RGB(0, 140, 80));
    videodisplay.setCursor(ox+4, footerY+2);
    int activeCnt = 0;
    for (int i = 0; i < MAX_BG_TASKS; i++) if (bgTasks[i].active) activeCnt++;
    char fbuf[48]; snprintf(fbuf, sizeof(fbuf), "Aktif BG:%d/%d  kill <slot> ile durdur  TAB=Sekme", activeCnt, MAX_BG_TASKS);
    videodisplay.print(fbuf);
  }
}



void drawDisplaySettings() {
  int ox = 0, oy = 0, w = 280, h = 240;
  if (focusedWindowIndex != -1 && strcmp(windows[focusedWindowIndex].appName, "Display") == 0) {
    ox = windows[focusedWindowIndex].x + 2; oy = windows[focusedWindowIndex].y + 24;
    w = windows[focusedWindowIndex].w - 4; h = windows[focusedWindowIndex].h - 26;
  } else {
    ox = (SCREEN_WIDTH - w)/2; oy = (SCREEN_HEIGHT - h)/2;
    videodisplay.fillRect(ox, oy, w, h, videodisplay.RGB(15, 15, 25));
    drawRect(ox, oy, w, h, COLOR_ACCENT);
  }
  
  videodisplay.setCursor(ox+10, oy+10); videodisplay.setTextColor(COLOR_TITLE);
  videodisplay.print("Display Settings");
  videodisplay.line(ox+5, oy+22, ox+w-5, oy+22, themeColorAccent);

  const char* resolutions[] = { 
    "320x200", "320x240", "400x300", "512x400",
    "640x400", "640x480", "800x600", "1024x768"
  };
  
  int cols = (w > 300) ? 2 : 1;
  int itemsPerCol = (8 + cols - 1) / cols;

  for (int i = 0; i < 8; i++) {
    int col = i / itemsPerCol;
    int row = i % itemsPerCol;
    int colWidth = (w - 30) / cols;
    int rx = ox + 15 + col * colWidth, ry = oy + 35 + row * 20;
    
    bool isCurrent = (i == currentResolutionIndex);
    bool isSelected = (i == displayAppSelection);
    
    // Selection/Hover highlight
    if (isSelected || (mouseX >= rx && mouseX <= rx + colWidth - 10 && mouseY >= ry && mouseY <= ry + 18)) {
      videodisplay.fillRect(rx-2, ry-2, colWidth-5, 18, videodisplay.RGB(40, 40, 60));
    }
    
    videodisplay.setTextColor(isCurrent ? COLOR_ICON_HOVER : (isSelected ? COLOR_TITLE : COLOR_TEXT));
    videodisplay.setCursor(rx + 20, ry + 4);
    videodisplay.print(resolutions[i]);
    
    // Radio button
    videodisplay.drawCircle(rx + 8, ry + 8, 4, isCurrent ? COLOR_ICON_HOVER : COLOR_TEXT_GREY);
    if (isCurrent) videodisplay.fillCircle(rx + 8, ry + 8, 2, COLOR_ICON_HOVER);
  }

  // Double Buffering toggle
  int tx = ox + 15, ty = oy + h - 35;
  bool isSelected = (displayAppSelection == 8);
  if (isSelected) videodisplay.fillRect(tx-4, ty-2, w-30, 20, videodisplay.RGB(40, 40, 60));
  
  videodisplay.setTextColor(isSelected ? COLOR_TITLE : COLOR_TEXT);
  videodisplay.setCursor(tx + 25, ty + 4);
  videodisplay.print("Double Buffering (Smooth)");
  videodisplay.drawRect(tx, ty, 14, 14, isSelected ? COLOR_TITLE : COLOR_TEXT_GREY);
  if (useDoubleBuffering) {
    videodisplay.line(tx+2, ty+7, tx+6, ty+11, COLOR_ICON_HOVER);
    videodisplay.line(tx+6, ty+11, tx+12, ty+3, COLOR_ICON_HOVER);
  }
}

void drawServices() {
  int ox = 0, oy = 0, w = 240, h = 150;
  if (focusedWindowIndex != -1 && strcmp(windows[focusedWindowIndex].appName, "Services") == 0) {
    ox = windows[focusedWindowIndex].x + 2; oy = windows[focusedWindowIndex].y + 24;
    w = windows[focusedWindowIndex].w - 4; h = windows[focusedWindowIndex].h - 26;
  } else {
    ox = (SCREEN_WIDTH - w)/2; oy = (SCREEN_HEIGHT - h)/2;
    videodisplay.fillRect(ox, oy, w, h, videodisplay.RGB(20, 10, 10));
    drawRect(ox, oy, w, h, videodisplay.RGB(255, 150, 0));
  }

  videodisplay.setCursor(ox+10, oy+10); videodisplay.setTextColor(videodisplay.RGB(255, 200, 0));
  videodisplay.print("Services & RAM Saver");

  // WiFi Service
  int wx = ox + 20, wy = oy + 40;
  videodisplay.setTextColor(COLOR_TEXT);
  videodisplay.setCursor(wx + 34, wy + 2);
  videodisplay.print("WiFi Service");
  videodisplay.setTextColor(wifiServiceActive ? COLOR_ICON_HOVER : COLOR_TEXT_GREY);
  videodisplay.setCursor(wx + 34, wy + 13);
  videodisplay.print(wifiServiceActive ? "[RUNNING]" : "[KILLED]");

  videodisplay.drawRoundRect(wx, wy + 4, 28, 14, 7, COLOR_TEXT_GREY);
  if (wifiServiceActive) videodisplay.fillCircle(wx + 21, wy + 11, 5, COLOR_ICON_HOVER);
  else videodisplay.fillCircle(wx + 7, wy + 11, 5, videodisplay.RGB(100, 20, 20));

  // BT Service
  int bx = ox + 20, by = oy + 80;
  videodisplay.setTextColor(COLOR_TEXT);
  videodisplay.setCursor(bx + 34, by + 2);
  videodisplay.print("Bluetooth HID");
  videodisplay.setTextColor(btServiceActive ? COLOR_ICON_HOVER : COLOR_TEXT_GREY);
  videodisplay.setCursor(bx + 34, by + 13);
  videodisplay.print(btServiceActive ? "[RUNNING]" : "[KILLED]");

  videodisplay.drawRoundRect(bx, by + 4, 28, 14, 7, COLOR_TEXT_GREY);
  if (btServiceActive) videodisplay.fillCircle(bx + 21, by + 11, 5, COLOR_ICON_HOVER);
  else videodisplay.fillCircle(bx + 7, by + 11, 5, videodisplay.RGB(100, 20, 20));

  videodisplay.setTextColor(COLOR_TEXT_GREY);
  videodisplay.setCursor(ox+10, oy+h-15);
  videodisplay.print("Free Heap: "); videodisplay.print(ESP.getFreeHeap()/1024); videodisplay.print(" KB");
}

void drawSettings() {
  videodisplay.fillRect(10, 30, 300, 200, COLOR_ICON_BG);
  drawRect(10, 30, 300, 200, COLOR_ICON_BORDER);
  videodisplay.setCursor(20, 40); videodisplay.setTextColor(COLOR_TITLE); videodisplay.print("System Settings");
  videodisplay.setCursor(20, 60); videodisplay.setTextColor(COLOR_TEXT);
  videodisplay.print("Model: ESP32-S3");
  videodisplay.setCursor(20, 75); videodisplay.print("Cores: "); videodisplay.print(ESP.getChipCores());
  videodisplay.setCursor(20, 90); videodisplay.print("Flash: "); videodisplay.print(ESP.getFlashChipSize() / 1024 / 1024); videodisplay.print(" MB");
  videodisplay.setCursor(20, 105); videodisplay.print("Frequency: "); videodisplay.print(ESP.getCpuFreqMHz()); videodisplay.print(" MHz");
  videodisplay.setCursor(20, 180); videodisplay.setTextColor(videodisplay.RGB(255, 0, 0)); videodisplay.print("Press ESC to close");
}

void drawAbout() {
  videodisplay.clear(COLOR_BACKGROUND);
  videodisplay.setCursor(40, 60); videodisplay.setTextColor(COLOR_TITLE); videodisplay.print("EyudiOS S3 Edition");
  videodisplay.setTextColor(COLOR_TEXT);
  videodisplay.setCursor(40, 80); videodisplay.print("Core: ESP32-S3");
  videodisplay.setCursor(40, 95); videodisplay.print("Dual-core 240MHz");
  videodisplay.setCursor(40, 110); videodisplay.print("By EyudiOS Team");
  videodisplay.setCursor(40, 140); videodisplay.setTextColor(COLOR_TEXT_LIGHT); videodisplay.print("Press ESC to exit");
}

void drawNetworkTools() {
  videodisplay.clear(COLOR_BACKGROUND);
  videodisplay.setCursor(20, 40); videodisplay.setTextColor(COLOR_TITLE); videodisplay.print("Network Tools");
  videodisplay.setTextColor(COLOR_TEXT);
  videodisplay.setCursor(20, 60); videodisplay.print(wifiConnected ? "Status: Connected" : "Status: Disconnected");
  videodisplay.setCursor(20, 75); videodisplay.print("IP: " + WiFi.localIP().toString());
  videodisplay.setCursor(20, 130); videodisplay.setTextColor(COLOR_TEXT_LIGHT); videodisplay.print("Press ESC to exit");
}

void drawWindowDecoration(int index) {
  Window &w = windows[index];
  uint16_t accentColor = w.focused ? videodisplay.RGB(0, 210, 80)  : videodisplay.RGB(30, 70, 35);
  uint16_t headerColor = w.focused ? videodisplay.RGB(8,  30, 12)  : videodisplay.RGB(6,  18, 8);
  uint16_t bodyColor   = videodisplay.RGB(5, 14, 6);
  uint16_t shadowColor = videodisplay.RGB(1,  4,  1);

  // Drop shadow
  videodisplay.fillRoundRect(w.x + 4, w.y + 4, w.w, w.h, 6, shadowColor);

  // Body
  videodisplay.fillRoundRect(w.x, w.y, w.w, w.h, 6, bodyColor);
  videodisplay.drawRoundRect(w.x, w.y, w.w, w.h, 6, accentColor);

  // Header bar
  videodisplay.fillRoundRect(w.x + 1, w.y + 1, w.w - 2, 22, 6, headerColor);
  // Header bottom edge
  videodisplay.line(w.x + 1, w.y + 22, w.x + w.w - 2, w.y + 22, accentColor);
  // Top highlight (ince parlak çizgi)
  videodisplay.line(w.x + 2, w.y + 1, w.x + w.w - 4, w.y + 1, videodisplay.RGB(40, 120, 60));

  // Title
  videodisplay.setFont(NULL);
  videodisplay.setTextSize(1);
  videodisplay.setTextColor(videodisplay.RGB(200, 240, 210));
  videodisplay.setCursor(w.x + 10, w.y + 7);
  videodisplay.print(w.title);

  // Window Buttons (Top Right)
  int bx = w.x + w.w - 14, cy = w.y + 11;

  // Close Button (Red Circle)
  videodisplay.fillCircle(bx, cy, 6, videodisplay.RGB(210, 50, 60));
  videodisplay.drawCircle(bx, cy, 6, videodisplay.RGB(255, 80, 80));
  videodisplay.setTextColor(videodisplay.RGB(255, 255, 255));
  videodisplay.setCursor(bx - 3, cy - 4);
  videodisplay.print("x");

  // Maximize Button (Green Square) - only if Doom
  if (strcmp(w.appName, "Doom") == 0) {
    int mx = bx - 18;
    videodisplay.fillRect(mx - 6, cy - 6, 12, 12, videodisplay.RGB(40, 160, 40));
    videodisplay.drawRect(mx - 6, cy - 6, 12, 12, videodisplay.RGB(80, 255, 80));
    videodisplay.setTextColor(videodisplay.RGB(255, 255, 255));
    videodisplay.setCursor(mx - 3, cy - 4);
    videodisplay.print("+");
  }
}


void drawAppInWindow(int index) {
  Window &w = windows[index];
  drawWindowDecoration(index);
  
  int ox = w.x + 2, oy = w.y + 17;
  int innerW = w.w - 4, innerH = w.h - 19;

  if (strcmp(w.appName, "Eyudio") == 0) {
    renderEyudioApp(w);
  } else if (strcmp(w.appName, "Eyuditor") == 0) {


    for (int i = 0; i < min(15, eyuditorLineCount); i++) { 
      videodisplay.setCursor(ox + 5, oy + i * 10); 
      videodisplay.setTextColor(COLOR_TEXT); 
      videodisplay.print(eyuLines[i]); 
    }
  } else if (strcmp(w.appName, "File Manager") == 0) {
    for (int i = 0; i < min(18, fileCount); i++) {
      int y = oy + i * 10; 
      // Hover ve Seçim ayrımı
      if (i == selectedFile) {
        videodisplay.fillRect(ox, y - 1, innerW, 10, videodisplay.RGB(0, 80, 30));
        videodisplay.setTextColor(videodisplay.RGB(255, 255, 255));
        videodisplay.setCursor(ox + 2, y); videodisplay.print(">");
      } else if (i == fmHoverIndex) {
        videodisplay.fillRect(ox, y - 1, innerW, 10, videodisplay.RGB(5, 30, 10));
        videodisplay.setTextColor(COLOR_ACCENT);
      } else {
        videodisplay.setTextColor(COLOR_TEXT);
      }
      videodisplay.setCursor(ox + 10, y); 
      videodisplay.print(fileList[i].c_str());
    }
  } else if (strcmp(w.appName, "esdos") == 0) {
    // ... ESDOS same ...
    for (int i = 0; i < min(12, esdosOutputCount); i++) { 
        videodisplay.setCursor(ox + 5, oy + i * 10); 
        videodisplay.setTextColor(COLOR_TEXT_LIGHT); 
        videodisplay.print(esdosOutput[i].c_str()); 
    }
    videodisplay.setCursor(ox + 5, oy + innerH - 10); 
    videodisplay.setTextColor(COLOR_TITLE); 
    videodisplay.print((esdosCurrentPath + "> " + esdosCommand).c_str());
  } else if (strcmp(w.appName, "Info") == 0) {
    // ── EyudiOS System Info (MODERN) ──────────────────
    int lh = 11; int y0 = oy + 4;
    auto txt = [&](int line, const char* label, String val, uint16_t vc = COLOR_TEXT) {
      videodisplay.setCursor(ox + 4, y0 + line * lh);
      videodisplay.setTextColor(videodisplay.RGB(0, 180, 255)); videodisplay.print(label);
      videodisplay.setTextColor(vc); videodisplay.print(val.c_str());
    };
    videodisplay.fillRect(ox, oy, w.w - 2, 14, videodisplay.RGB(0, 40, 60));
    videodisplay.setCursor(ox + 4, oy + 3);
    videodisplay.setTextColor(videodisplay.RGB(0, 255, 255));
    videodisplay.print("EyudiOS S3 Edition v2.5");
    y0 = oy + 18;
    txt(0, "Chip:  ", "ESP32-S3 Dual-Core LX7");
    txt(1, "Freq:  ", "240 MHz (Xtensa 32-bit)");
    txt(2, "PSRAM: ", String(ESP.getPsramSize()/1024/1024) + " MB OPI Speed");
    txt(3, "Flash: ", String(ESP.getFlashChipSize()/1024/1024) + " MB (DIO)");
    txt(4, "WiFi:  ", wifiConnected ? WiFi.localIP().toString() : "Not Connected");
    txt(5, "Heap:  ", String(ESP.getFreeHeap()/1024) + " KB Free");
    txt(6, "VGA:   ", "400x300 @ 60Hz 16-bit");
    txt(7, "OS:    ", "EyudiOS Real-time Kernel");
    txt(9, "FTP:   ", ftpRunning ? "RUNNING (Port 21)" : "IDLE");
  } else if (strcmp(w.appName, "Browser") == 0) {
    // ── Web Browser ─────────────────────────────────
    int urlBarH = 14; int cy = oy + urlBarH + 2;
    // URL bar
    videodisplay.fillRect(ox, oy, w.w - 2, urlBarH, videodisplay.RGB(10, 20, 35));
    videodisplay.setTextColor(videodisplay.RGB(0, 200, 255));
    videodisplay.setCursor(ox+3, oy+3); videodisplay.print("URL: ");
    videodisplay.setTextColor(COLOR_TEXT);
    String urlDisp = webBrowserURL;
    if ((int)urlDisp.length() > 38) urlDisp = urlDisp.substring(0, 35) + "...";
    videodisplay.print(urlDisp.c_str());

    if (webBrowserLoading) {
      // Animated spinner
      static uint8_t spinIdx = 0;
      static unsigned long spinLast = 0;
      unsigned long now = millis();
      if (now - spinLast > 200) { spinIdx = (spinIdx + 1) % 4; spinLast = now; }
      const char spin[] = {'|', '/', '-', '\\'};
      
      // Loading box
      videodisplay.fillRect(ox+4, cy+5, innerW-8, 40, videodisplay.RGB(5, 15, 30));
      videodisplay.drawRoundRect(ox+4, cy+5, innerW-8, 40, 4, videodisplay.RGB(0, 100, 180));
      videodisplay.setTextColor(videodisplay.RGB(0, 200, 255));
      videodisplay.setCursor(ox+14, cy+14);
      videodisplay.print("Yukleniyor ");
      videodisplay.print(spin[spinIdx]);
      if (webBrowserStatus.length() > 0) {
        videodisplay.setTextColor(videodisplay.RGB(80, 160, 200));
        videodisplay.setCursor(ox+14, cy+28);
        videodisplay.print(webBrowserStatus.c_str());
      }
    } else if (webBrowserContent.length() == 0) {
      videodisplay.setTextColor(videodisplay.RGB(60, 100, 80));
      videodisplay.setCursor(ox+10, cy+20);
      videodisplay.print("Bos. EsDOS: browse <url>");
    } else {
      // Render parsed content directly (already cleaned by webBrowserLoad)
      int line = 0;
      int start = webBrowserScrollY;
      while (start < (int)webBrowserContent.length() && line < 16) {
        int end = webBrowserContent.indexOf('\n', start);
        if (end == -1) end = webBrowserContent.length();
        String s = webBrowserContent.substring(start, end);
        s.trim();
        if (s.length() > 0) {
          // Color JSON keys differently
          uint16_t tc = COLOR_TEXT;
          if (s.startsWith("[JSON]")) tc = videodisplay.RGB(0, 255, 150);
          else if (s.startsWith("***")) tc = videodisplay.RGB(255, 80, 80);
          else if (s.startsWith("- ")) tc = videodisplay.RGB(180, 230, 180);
          else if (s.indexOf(':') != -1 && s.indexOf(':') < 20) tc = videodisplay.RGB(200, 220, 255);
          videodisplay.setTextColor(tc);
          videodisplay.setCursor(ox+4, cy + line*11);
          // Truncate to fit window
          int maxChars = (innerW - 8) / 6;
          if ((int)s.length() > maxChars) s = s.substring(0, maxChars - 1) + "~";
          videodisplay.print(s.c_str());
          line++;
        }
        start = end + 1;
      }
    }
    // Hint bar
    videodisplay.setCursor(ox + 3, oy + innerH - 9);
    videodisplay.setTextColor(videodisplay.RGB(40, 80, 40));
    videodisplay.print("browse <url> | PgUp/PgDn=Kaydir");



  } else if (strcmp(w.appName, "FTP") == 0) {
    // ── FTP Server Panel ─────────────────────────────
    // Header
    videodisplay.fillRect(ox, oy, w.w - 2, 16, ftpRunning ? videodisplay.RGB(60, 20, 20) : videodisplay.RGB(20, 40, 30));
    videodisplay.setTextColor(videodisplay.RGB(0, 255, 100));
    videodisplay.setCursor(ox + 4, oy + 4);
    videodisplay.print(ftpRunning ? "FTP: ACTIVE" : "FTP: STOPPED");
    
    videodisplay.setTextColor(COLOR_TEXT);
    videodisplay.setCursor(ox + 10, oy + 30);
    videodisplay.print("User: admin  /  Pass: 1234");
    videodisplay.setCursor(ox + 10, oy + 45);
    videodisplay.print("Port: 21 (Control)");
    
    if (ftpRunning) {
      videodisplay.setTextColor(videodisplay.RGB(255, 200, 0));
      videodisplay.setCursor(ox + 10, oy + 70);
      videodisplay.print("Waiting for connection...");
    }
  } else if (strcmp(w.appName, "Doom") == 0) {
    // ── Doom Engine App Rendering ────────────────────
    renderDoomFrame();
  } else if (strcmp(w.appName, "Tasks") == 0) {
    drawTaskManager();
  } else if (strcmp(w.appName, "Display") == 0) {
    drawDisplaySettings();
  } else if (strcmp(w.appName, "Services") == 0) {
    drawServices();
  } else if (strcmp(w.appName, "Market") == 0) {
    drawMarket();
  }
}

// ============================================================
// === EYUMARKET (SCRIPT LAUNCHER) ===
// ============================================================
void scanMarketScripts() {
  marketScriptCount = 0;
  if (WiFi.status() != WL_CONNECTED) {
    logToFile("[MARKET] WiFi not connected. Scanning local files only.");
    // Fallback: Scan local SD card for existing scripts
    if (sdCardPresent) {
      File root = SD.open("/");
      if (!root) return;
      while (true) {
        File f = root.openNextFile();
        if (!f) break;
        String n = String(f.name());
        if (!f.isDirectory() && (n.endsWith(".eyu") || n.endsWith(".EYU"))) {
          marketScripts[marketScriptCount] = n.substring(0, n.length() - 4); // friendly name
          marketFiles[marketScriptCount] = n;
          marketDescs[marketScriptCount] = "Local script (Offline)";
          marketUrls[marketScriptCount] = "";
          marketScriptCount++;
        }
        f.close();
        if (marketScriptCount >= 30) break;
      }
      root.close();
    }
    return;
  }

  HTTPClient http;
  http.setTimeout(10000);
  http.begin("https://raw.githubusercontent.com/6eyp6/EyuMarket/main/market.json");
  http.addHeader("User-Agent", "EyudiOS/1.0 (ESP32-S3)");
  
  int httpCode = http.GET();
  if (httpCode == 200) {
    String payload = http.getString();
    DynamicJsonDocument doc(8192); // 8KB is enough for 30 apps
    DeserializationError error = deserializeJson(doc, payload);
    if (!error) {
      JsonArray arr = doc.as<JsonArray>();
      for (JsonVariant val : arr) {
        if (marketScriptCount >= 30) break;
        marketScripts[marketScriptCount] = val["name"].as<String>();
        marketFiles[marketScriptCount] = val["filename"].as<String>();
        marketDescs[marketScriptCount] = val["desc"].as<String>();
        marketUrls[marketScriptCount] = val["url"].as<String>();
        marketScriptCount++;
      }
    } else {
      logToFile("[MARKET] JSON Parse Error: " + String(error.c_str()));
    }
  } else {
    logToFile("[MARKET] HTTP Error: " + String(httpCode));
  }
  http.end();
}

void drawMarket() {
  int idx = -1;
  for(int i=0; i<windowCount; i++) if(strcmp(windows[i].appName, "Market")==0) { idx = i; break; }
  if(idx == -1) return;
  Window &w = windows[idx];
  int ox = w.x + 2, oy = w.y + 24;
  int ww = w.w - 4, wh = w.h - 26;

  videodisplay.fillRect(ox, oy, ww, wh, videodisplay.RGB(10, 15, 30));
  videodisplay.fillRect(ox, oy, ww, 22, videodisplay.RGB(255, 0, 255));
  videodisplay.setTextColor(videodisplay.RGB(255, 255, 255));
  videodisplay.setCursor(ox+10, oy+4);
  videodisplay.print("EyuMarket - Online App Store");

  if (marketScriptCount == 0) {
    videodisplay.setCursor(ox+20, oy+40);
    videodisplay.print("No EyuScripts found.");
    return;
  }

  int cols = 1, cw = ww, ch = 38;
  marketHoverIdx = -1;
  for(int i=0; i<marketScriptCount; i++) {
    int rx = ox + 5, ry = oy + 32 + i * ch;
    if (ry + ch > oy + wh) break;
    bool hov = (mouseX >= rx && mouseX <= rx + cw - 10 && mouseY >= ry && mouseY <= ry + ch - 5);
    bool sel = (i == marketSelection);
    if (hov) { marketHoverIdx = i; marketSelection = i; }

    videodisplay.fillRoundRect(rx, ry, cw - 10, ch - 5, 4, sel ? videodisplay.RGB(180, 0, 180) : videodisplay.RGB(25, 30, 60));
    videodisplay.drawRoundRect(rx, ry, cw - 10, ch - 5, 4, sel ? videodisplay.RGB(255, 255, 255) : videodisplay.RGB(70, 80, 120));
    
    // Check if local file exists
    String localFile = "/" + marketFiles[i];
    bool isDownloaded = SD.exists(localFile);
    
    // App Name
    videodisplay.setTextColor(videodisplay.RGB(255, 255, 255));
    videodisplay.setCursor(rx + 10, ry + 4);
    videodisplay.print(marketScripts[i].c_str());
    
    // Description
    videodisplay.setTextColor(videodisplay.RGB(160, 170, 200));
    videodisplay.setCursor(rx + 10, ry + 18);
    String desc = marketDescs[i];
    if (desc.length() > 32) desc = desc.substring(0, 29) + "...";
    videodisplay.print(desc.c_str());
    
    // Action label (RUN or GET)
    videodisplay.setCursor(rx + cw - 65, ry + 10);
    if (isDownloaded) {
        videodisplay.setTextColor(videodisplay.RGB(0, 255, 100));
        videodisplay.print("[RUN]");
    } else {
        videodisplay.setTextColor(videodisplay.RGB(0, 200, 255));
        videodisplay.print("[GET]");
    }
  }
}

void handleMarketClick(int x, int y) {
   if (marketSelection != -1 && marketSelection < marketScriptCount) {
      String localFile = "/" + marketFiles[marketSelection];
      String downloadUrl = marketUrls[marketSelection];
      
      // Download if not present locally
      if (downloadUrl.length() > 0 && !SD.exists(localFile)) {
         Serial.printf("[MARKET] Downloading: %s -> %s\n", downloadUrl.c_str(), localFile.c_str());
         
         // Drawing "Downloading..." overlay
         videodisplay.fillRect(100, 100, 200, 80, videodisplay.RGB(20, 30, 60));
         videodisplay.drawRect(100, 100, 200, 80, videodisplay.RGB(255, 255, 255));
         videodisplay.setTextColor(videodisplay.RGB(255, 255, 255));
         videodisplay.setCursor(115, 120);
         videodisplay.print("Downloading App...");
         videodisplay.setCursor(115, 140);
         videodisplay.print(marketFiles[marketSelection].c_str());
         videodisplay.show();
         
         HTTPClient http;
         http.setTimeout(15000); // 15s
         http.begin(downloadUrl);
         http.addHeader("User-Agent", "EyudiOS/1.0 (ESP32-S3)");
         int code = http.GET();
         if (code == 200) {
            File f = SD.open(localFile, FILE_WRITE);
            if (f) {
               http.writeToStream(&f);
               f.close();
               Serial.println("[MARKET] Download successful!");
            } else {
               Serial.println("[MARKET] Failed to open local file for writing.");
            }
         } else {
            Serial.printf("[MARKET] Download failed, HTTP Code: %d\n", code);
         }
         http.end();
      }
      
      if (SD.exists(localFile)) {
         Serial.printf("[MARKET] Running: %s\n", localFile.c_str());
         extern void runEyuScript(String fullPath);
         runEyuScript(localFile);
      } else {
         Serial.println("[MARKET] Script not found and download failed.");
      }
   }
}

void handleMarketInput(ParsedKey key) {
  if (key.isEscape) {
    for (int i = 0; i < windowCount; i++) {
      if (strcmp(windows[i].appName, "Market") == 0) { closeWindow(i); break; }
    }
    return;
  }
  if (key.normalizedKey == "UP" || (key.isArrow && key.arrowDirection == 0)) {
    if (marketSelection >= 2) marketSelection -= 2;
  }
  else if (key.normalizedKey == "DOWN" || (key.isArrow && key.arrowDirection == 1)) {
    if (marketSelection + 2 < marketScriptCount) marketSelection += 2;
  }
  else if (key.normalizedKey == "LEFT" || (key.isArrow && key.arrowDirection == 2)) {
    if (marketSelection > 0) marketSelection--;
  }
  else if (key.normalizedKey == "RIGHT" || (key.isArrow && key.arrowDirection == 3)) {
    if (marketSelection < marketScriptCount - 1) marketSelection++;
  }
  else if (key.isEnter) {
    handleMarketClick(0, 0); // Reuse logic
  }
}

// This is no longer used in the new WM system, but kept for compatibility
void drawCurrentApplication() {
  if (focusedWindowIndex != -1) drawAppInWindow(focusedWindowIndex);
}

void renderBootMenu() {
  drawBootMenu();
  drawCursor();
  videodisplay.show();
  prevMouseX = mouseX; prevMouseY = mouseY;
}

void renderMenu() {
  // Her kareyi back buffer'da çiz
  drawMenu();
  drawCursor();
  videodisplay.show();
  prevMouseX = mouseX; prevMouseY = mouseY;
}

void renderApplication() {
  // Pencereleri arkadan öne çiz — double buffer olduğu için ghost silmeye gerek yok
  // Sadece needsRedraw olsa bile, her loop'ta tamamı çizilmeli
  for (int i = 0; i < windowCount; i++) {
    drawAppInWindow(i);
  }
  if (showVirtualKeyboard) {
    drawVirtualKeyboard();
  }
}


void handleSystemLogsInput(ParsedKey key) {
  if (key.isEscape) {
    showSystemLogs = false;
    currentApp = "";
    desktopNeedsRedraw = true;
  }
}

void drawInputDriverWaitingScreen() {
  // Premium dark-blue/space background
  videodisplay.fillScreen(videodisplay.RGB(10, 14, 28));
  
  // Outer frame glow
  videodisplay.drawRect(5, 5, SCREEN_WIDTH - 10, SCREEN_HEIGHT - 10, videodisplay.RGB(0, 120, 180));
  videodisplay.drawRect(6, 6, SCREEN_WIDTH - 12, SCREEN_HEIGHT - 12, videodisplay.RGB(0, 80, 120));
  
  // Icon / Graphic (simple beautiful shapes)
  int cx = SCREEN_WIDTH / 2;
  int cy = 70;
  // Draw simulated gamepad
  videodisplay.fillRoundRect(cx - 30, cy - 15, 60, 30, 8, videodisplay.RGB(20, 30, 55));
  videodisplay.drawRoundRect(cx - 30, cy - 15, 60, 30, 8, videodisplay.RGB(0, 200, 255));
  videodisplay.fillCircle(cx - 15, cy, 6, videodisplay.RGB(0, 120, 200)); // Left stick
  videodisplay.fillCircle(cx + 15, cy, 6, videodisplay.RGB(0, 120, 200)); // Right stick
  
  // Title text
  videodisplay.setFont(NULL);
  videodisplay.setTextSize(2);
  videodisplay.setTextColor(videodisplay.RGB(0, 200, 255));
  const char* title = "Waiting for Driver...";
  videodisplay.setCursor(cx - 95, 110);
  videodisplay.print(title);
  
  // Status info
  videodisplay.setTextSize(1);
  videodisplay.setTextColor(videodisplay.RGB(255, 255, 255));
  videodisplay.setCursor(50, 140);
  videodisplay.print("Target Driver: ");
  videodisplay.setTextColor(videodisplay.RGB(0, 255, 128));
  if (currentInputDriver == 0) videodisplay.print("Normal (Kbd + Mouse)");
  else if (currentInputDriver == 1) videodisplay.print("Controllers (USB)");
  else if (currentInputDriver == 2) videodisplay.print("Bluetooth Dongle");
  
  // Animated loading bar
  int barWidth = 240;
  int barHeight = 8;
  int barX = cx - barWidth / 2;
  int barY = 160;
  videodisplay.drawRect(barX, barY, barWidth, barHeight, videodisplay.RGB(100, 100, 100));
  
  // Animate indicator based on millis
  int speedOffset = (millis() / 5) % (barWidth + 40);
  for (int i = 0; i < 4; i++) {
    int pulseX = barX + speedOffset - (i * 12);
    if (pulseX >= barX + 1 && pulseX < barX + barWidth - 1) {
      videodisplay.fillRect(pulseX, barY + 1, 8, barHeight - 2, videodisplay.RGB(0, 200, 255));
    }
  }
  
  // Info text box
  videodisplay.fillRect(cx - 150, 185, 300, 75, videodisplay.RGB(15, 20, 38));
  videodisplay.drawRect(cx - 150, 185, 300, 75, videodisplay.RGB(35, 45, 70));
  
  videodisplay.setTextColor(videodisplay.RGB(200, 200, 200));
  videodisplay.setCursor(cx - 140, 195);
  videodisplay.print("1. Connect Arduino Leonardo/Uno to your PC.");
  videodisplay.setCursor(cx - 140, 210);
  videodisplay.print("2. Upload new driver using Arduino IDE.");
  videodisplay.setCursor(cx - 140, 225);
  videodisplay.print("3. Screen will close automatically.");
  
  videodisplay.setCursor(cx - 140, 245);
  videodisplay.setTextColor(videodisplay.RGB(255, 100, 100));
  videodisplay.print("Press [ESC] to cancel.");
}

