// ============================================================
// FtpServer.cpp — Minimal FTP Server for EyudiOS S3 Edition
// Aktif / Pasif mod destekler, SD kart üzerinde r/w yapar
// EsDOS: "ftp start" ile başlatılır, "ftp stop" ile durdurulur
// ============================================================
#include "Globals.h"
#include <WiFi.h>
#include <esp_wifi.h>

// ── Durum ──────────────────────────────────────────────────
bool ftpRunning = false;
static WiFiServer ftpCtrlServer(21);
static WiFiServer ftpDataServer(50021); // Moved from 20 to 50021 to bypass privileged port checks (ports < 1024)

// ── Yardımcı: tek satır gönder ─────────────────────────────
static void ftpSend(WiFiClient& c, const char* msg) {
  c.print(msg); c.print("\r\n");
}

// ── FTP İstemci Görevi (Core 0) ────────────────────────────
static void ftpClientTask(void* pv) {
  WiFiClient ctrl = *((WiFiClient*)pv);
  delete (WiFiClient*)pv;

  char buf[256];
  String cwd = "/";
  WiFiClient dataConn;
  bool dataActive = false;
  String dataHost;
  uint16_t dataPort = 0;
  bool loggedIn = false;
  String renameFrom;

  ftpSend(ctrl, "220 EyudiOS FTP Server Ready");

  auto readLine = [&](char* dst, int maxLen) -> int {
    int idx = 0;
    unsigned long t0 = millis();
    while (millis() - t0 < 15000) {
      if (!ctrl.connected()) break;
      if (ctrl.available()) {
        char c = ctrl.read();
        if (c == '\n') break;
        if (c != '\r' && idx < maxLen - 1) dst[idx++] = c;
        t0 = millis();
      }
      vTaskDelay(1);
    }
    dst[idx] = '\0';
    return idx;
  };

  auto openDataConn = [&]() -> bool {
    if (dataActive) {
      // PORT modu: biz bağlanıyoruz
      dataConn = WiFiClient();
      IPAddress ip;
      ip.fromString(dataHost);
      if (!dataConn.connect(ip, dataPort)) { ftpSend(ctrl, "425 Can't open data connection"); return false; }
    } else {
      // PASV modu: istemci bize bağlandı
      unsigned long t0 = millis();
      while (!ftpDataServer.hasClient() && millis() - t0 < 5000) vTaskDelay(10);
      if (!ftpDataServer.hasClient()) { ftpSend(ctrl, "425 Can't open data connection"); return false; }
      dataConn = ftpDataServer.accept();
    }
    return true;
  };

  auto fullPath = [&](const String& p) -> String {
    if (p.startsWith("/")) return p;
    if (cwd.endsWith("/")) return cwd + p;
    return cwd + "/" + p;
  };

  while (ctrl.connected()) {
    if (!readLine(buf, sizeof(buf))) break;
    if (strlen(buf) == 0) continue;

    String line(buf);
    String cmd = line.substring(0, min((int)line.length(), 4));
    cmd.trim(); cmd.toUpperCase();
    String arg = (line.length() > cmd.length() + 1) ? line.substring(cmd.length() + 1) : "";
    arg.trim();

    // ── Kimlik doğrulama (herkes izinli, şifresiz) ─────────
    if (cmd == "USER") { ftpSend(ctrl, "331 Password required"); }
    else if (cmd == "PASS") { loggedIn = true; ftpSend(ctrl, "230 Login successful"); }
    else if (cmd == "QUIT") { ftpSend(ctrl, "221 Goodbye"); break; }
    else if (cmd == "SYST") { ftpSend(ctrl, "215 UNIX Type: L8"); }
    else if (cmd == "FEAT") { ctrl.print("211-Features:\r\n PASV\r\n211 End\r\n"); }
    else if (cmd == "TYPE") { ftpSend(ctrl, "200 Type set"); }
    else if (cmd == "NOOP") { ftpSend(ctrl, "200 OK"); }
    else if (!loggedIn)     { ftpSend(ctrl, "530 Not logged in"); }

    // ── Dizin işlemleri ─────────────────────────────────
    else if (cmd == "PWD" || cmd == "XPWD") {
      ctrl.printf("257 \"%s\" is current directory\r\n", cwd.c_str());
    }
    else if (cmd == "CWD" || cmd == "XCWD") {
      String np = fullPath(arg);
      if (np.length() > 1 && np.endsWith("/")) np.remove(np.length()-1);
      if (sdMutex) xSemaphoreTake(sdMutex, portMAX_DELAY);
      File d = SD.open(np);
      if (d && d.isDirectory()) { cwd = np; if (!cwd.endsWith("/")) cwd += "/"; ftpSend(ctrl, "250 CWD successful"); }
      else ftpSend(ctrl, "550 Directory not found");
      if (d) d.close();
      if (sdMutex) xSemaphoreGive(sdMutex);
    }
    else if (cmd == "CDUP") {
      int ls = cwd.lastIndexOf('/', cwd.length()-2);
      cwd = (ls >= 0) ? cwd.substring(0, ls+1) : "/";
      ftpSend(ctrl, "250 CDUP successful");
    }
    else if (cmd == "MKD" || cmd == "XMKD") {
      String np = fullPath(arg);
      bool ok = false;
      if (sdMutex) xSemaphoreTake(sdMutex, portMAX_DELAY);
      ok = SD.mkdir(np);
      if (sdMutex) xSemaphoreGive(sdMutex);
      if (ok) ctrl.printf("257 \"%s\" created\r\n", np.c_str());
      else ftpSend(ctrl, "550 Can't create directory");
    }
    else if (cmd == "RMD" || cmd == "XRMD") {
      bool ok = false;
      if (sdMutex) xSemaphoreTake(sdMutex, portMAX_DELAY);
      ok = SD.rmdir(fullPath(arg));
      if (sdMutex) xSemaphoreGive(sdMutex);
      if (ok) ftpSend(ctrl, "250 RMD successful");
      else ftpSend(ctrl, "550 Can't remove directory");
    }
    else if (cmd == "DELE") {
      bool ok = false;
      if (sdMutex) xSemaphoreTake(sdMutex, portMAX_DELAY);
      ok = SD.remove(fullPath(arg));
      if (sdMutex) xSemaphoreGive(sdMutex);
      if (ok) ftpSend(ctrl, "250 DELE successful");
      else ftpSend(ctrl, "550 Can't delete file");
    }
    else if (cmd == "RNFR") { renameFrom = fullPath(arg); ftpSend(ctrl, "350 File pending rename"); }
    else if (cmd == "RNTO") {
      bool ok = false;
      if (sdMutex) xSemaphoreTake(sdMutex, portMAX_DELAY);
      ok = SD.rename(renameFrom, fullPath(arg));
      if (sdMutex) xSemaphoreGive(sdMutex);
      if (ok) ftpSend(ctrl, "250 RNTO successful");
      else ftpSend(ctrl, "550 Rename failed");
    }

    // ── PORT / PASV ─────────────────────────────────────
    else if (cmd == "PORT") {
      // PORT h1,h2,h3,h4,p1,p2
      int parts[6]; int n = 0;
      char* tok = strtok(arg.begin(), ",");
      while (tok && n < 6) { parts[n++] = atoi(tok); tok = strtok(NULL, ","); }
      if (n == 6) {
        dataHost = String(parts[0])+"."+String(parts[1])+"."+String(parts[2])+"."+String(parts[3]);
        dataPort = (uint16_t)(parts[4]*256 + parts[5]);
        dataActive = true;
        ftpSend(ctrl, "200 PORT command successful");
      } else ftpSend(ctrl, "501 Bad PORT argument");
    }
    else if (cmd == "PASV") {
      IPAddress ip = WiFi.localIP();
      uint16_t port = 50021; // Match new data server port
      char resp[64];
      snprintf(resp, sizeof(resp), "227 Entering Passive Mode (%d,%d,%d,%d,%d,%d)",
        ip[0],ip[1],ip[2],ip[3], port/256, port%256);
      dataActive = false;
      ctrl.print(resp); ctrl.print("\r\n");
    }

    // ── Listeleme ────────────────────────────────────────
    else if (cmd == "LIST" || cmd == "NLST") {
      if (!openDataConn()) continue;
      ftpSend(ctrl, "150 Opening data connection");
      String lPath = cwd;
      if (lPath.length() > 1 && lPath.endsWith("/")) lPath.remove(lPath.length()-1);
      if (sdMutex) xSemaphoreTake(sdMutex, portMAX_DELAY);
      File dir = SD.open(lPath);
      if (dir) {
        File f;
        while ((f = dir.openNextFile())) {
          String nm = String(f.name()); int sl = nm.lastIndexOf('/'); if (sl>=0) nm = nm.substring(sl+1);
          if (cmd == "LIST") {
            char line2[64];
            if (f.isDirectory())
              snprintf(line2, sizeof(line2), "drwxr-xr-x 1 user user  0 Jan 01 00:00 %s", nm.c_str());
            else
              snprintf(line2, sizeof(line2), "-rw-r--r-- 1 user user %6lu Jan 01 00:00 %s", (unsigned long)f.size(), nm.c_str());
            dataConn.print(line2); dataConn.print("\r\n");
          } else {
            dataConn.print(nm); dataConn.print("\r\n");
          }
          f.close();
          vTaskDelay(1); 
        }
        dir.close();
      }
      if (sdMutex) xSemaphoreGive(sdMutex);
      dataConn.stop();
      ftpSend(ctrl, "226 Transfer complete");
    }
    else if (cmd == "SIZE") {
      if (sdMutex) xSemaphoreTake(sdMutex, portMAX_DELAY);
      File f = SD.open(fullPath(arg));
      if (f && !f.isDirectory()) ctrl.printf("213 %lu\r\n", (unsigned long)f.size());
      else ftpSend(ctrl, "550 File not found");
      if (f) f.close();
      if (sdMutex) xSemaphoreGive(sdMutex);
    }
    else if (cmd == "MDTM") { ftpSend(ctrl, "213 20240101000000"); }

    // ── İndirme (RETR) ───────────────────────────────────
    else if (cmd == "RETR") {
      File f;
      if (sdMutex) xSemaphoreTake(sdMutex, portMAX_DELAY);
      f = SD.open(fullPath(arg));
      if (sdMutex) xSemaphoreGive(sdMutex);

      if (!f || f.isDirectory()) { 
        ftpSend(ctrl, "550 File not found"); if(f) f.close(); continue; 
      }
      if (!openDataConn()) { f.close(); continue; }
      
      setStatusLED(EYU_LED_BLUE);
      ctrl.printf("150 Opening data connection for %s (%lu bytes)\r\n", arg.c_str(), (unsigned long)f.size());
      
      const int bSize = 2048; // 2KB buffer
      uint8_t* tbuf = (uint8_t*)malloc(bSize);
      if (tbuf) {
        int packCount = 0;
        if (sdMutex) xSemaphoreTake(sdMutex, portMAX_DELAY);
        while (f.available() && dataConn.connected()) {
          int n2 = f.read(tbuf, bSize);
          if (n2 > 0) {
            dataConn.write(tbuf, n2);
          }
          // Her 16 pakette bir veriyolunu diger islere birak (Concurrency)
          if (++packCount >= 16) {
            if (sdMutex) xSemaphoreGive(sdMutex);
            vTaskDelay(1);
            if (sdMutex) xSemaphoreTake(sdMutex, portMAX_DELAY);
            packCount = 0;
          }
        }
        if (sdMutex) xSemaphoreGive(sdMutex);
        free(tbuf);
      }
      f.close(); dataConn.stop();
      turnOffStatusLED();
      ftpSend(ctrl, "226 Transfer complete");
    }

    // ── Yükleme (STOR) ───────────────────────────────────
    else if (cmd == "STOR") {
      if (!openDataConn()) continue;
      
      File f;
      if (sdMutex) xSemaphoreTake(sdMutex, portMAX_DELAY);
      f = SD.open(fullPath(arg), FILE_WRITE);
      if (sdMutex) xSemaphoreGive(sdMutex);

      if (!f) { 
        ftpSend(ctrl, "550 Can't create file"); dataConn.stop(); continue; 
      }

      setStatusLED(EYU_LED_BLUE);
      ctrl.printf("150 Opening data connection for %s\r\n", arg.c_str());
      
      const int bSize = 2048;
      uint8_t* tbuf = (uint8_t*)malloc(bSize);
      if (tbuf) {
        int packCount = 0;
        if (sdMutex) xSemaphoreTake(sdMutex, portMAX_DELAY);
        while (dataConn.connected() || dataConn.available()) {
          int navail = dataConn.available();
          if (navail > 0) {
            int n2 = dataConn.read(tbuf, min(navail, bSize));
            if (n2 > 0) {
              f.write(tbuf, n2);
              if (++packCount >= 16) {
                if (sdMutex) xSemaphoreGive(sdMutex);
                vTaskDelay(1);
                if (sdMutex) xSemaphoreTake(sdMutex, portMAX_DELAY);
                packCount = 0;
              }
            }
          } else {
            vTaskDelay(1);
          }
        }
        if (sdMutex) xSemaphoreGive(sdMutex);
        free(tbuf);
      }
      f.close(); dataConn.stop();
      turnOffStatusLED();
      ftpSend(ctrl, "226 Transfer complete");
    }

    else { ftpSend(ctrl, "502 Command not implemented"); }
  }

  ctrl.stop();
  vTaskDelete(NULL);
}

// ── FTP Listener Task (Core 0) ─────────────────────────────
static TaskHandle_t ftpListenerHandle = NULL;
static bool btWasActiveBeforeFtp = false; // Bluetooth durumunu yedeklemek icin
static void ftpListenerTask(void*) {
  ftpCtrlServer.begin();
  ftpDataServer.begin();
  while (ftpRunning) {
    if (WiFi.status() != WL_CONNECTED) {
      logToFile("[FTP] Ag baglantisi koptu, sunucu beklemeye alindi.");
      while(WiFi.status() != WL_CONNECTED && ftpRunning) vTaskDelay(pdMS_TO_TICKS(1000));
      if (!ftpRunning) break;
      logToFile("[FTP] Ag geri geldi, sunucu AKTIF.");
    }

    if (ftpCtrlServer.hasClient()) {
      WiFiClient* pClient = new WiFiClient(ftpCtrlServer.accept());
      xTaskCreatePinnedToCore(ftpClientTask, "ftp_cli", 8192, pClient, 1, NULL, 0);
    }
    vTaskDelay(pdMS_TO_TICKS(100));
  }
  ftpCtrlServer.stop();
  ftpDataServer.stop();
  ftpListenerHandle = NULL;
  vTaskDelete(NULL);
}

// ── Dışa açık API ──────────────────────────────────────────
void startFtpServer() {
  if (WiFi.status() != WL_CONNECTED) { 
    logToFile("[FTP] HATA: WiFi bagli degil!"); 
    wifiConnected = false; 
    return; 
  }
  if (ftpRunning) {
    logToFile("[FTP] Sunucu zaten calisiyor: " + WiFi.localIP().toString());
    return;
  }

  // ⚡ Bluetooth'un o anki durumunu yedekle
  btWasActiveBeforeFtp = btServiceActive;
  if (btServiceActive) {
    logToFile("[FTP] Hız için Bluetooth kapatılıyor...");
    toggleBluetoothService(false);
  }

  ftpRunning = true;
  xTaskCreatePinnedToCore(ftpListenerTask, "ftp_srv", 8192, NULL, 1, &ftpListenerHandle, 0);
  logToFile("[FTP] Sunucu baslatildi ok.");
  Serial.printf("[FTP-INFO] Sunucu IP: %s | Port: 21\n", WiFi.localIP().toString().c_str());
}

void stopFtpServer() {
  ftpRunning = false;
  logToFile("[FTP] Sunucu durduruldu");

  // ⚡ Eğer Bluetooth daha önce açıksa geri yükle
  if (btWasActiveBeforeFtp) {
    logToFile("[FTP] Bluetooth geri yükleniyor...");
    toggleBluetoothService(true);
    btWasActiveBeforeFtp = false;
  }
}
