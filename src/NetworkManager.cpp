#include "Globals.h"
#include "EyudioWiFiSync.h"
#include "EyudioFirebase.h"
#include "esp_wifi.h"

void loadWiFiCredentials() {
  loadSettings();
}

void saveWiFiCredentials() {
  saveSettings();
}

void clearWiFiCredentials() {
  wifiSSID = "";
  wifiPassword = "";
  saveSettings();
}

void connectToWiFi() {
  if (wifiSSID.length() == 0) return;
  WiFi.begin(wifiSSID.c_str(), wifiPassword.c_str());
  esp_wifi_set_ps(WIFI_PS_MIN_MODEM);
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) { delay(500); attempts++; Serial.print("."); }
  if (WiFi.status() == WL_CONNECTED) { 
    wifiConnected = true; currentSSID = WiFi.SSID(); 
    blinkStatusLED(EYU_LED_GREEN, 3, 150); // 3x Green Blink
  }
  else wifiConnected = false;
}



void scanWiFiNetworks() {
  wifiScanning = true; wifiNetworkCount = 0;
  int n = WiFi.scanNetworks();
  if (n > 0) { for (int i = 0; i < min(n, 10); i++) { wifiNetworks[i] = WiFi.SSID(i); wifiNetworkCount++; } }
  wifiScanning = false; enterMenu("NetworkList");
}

void testWiFi() {
  showSplash = true; splashMessage = "WiFi test ediliyor..."; bootMenuNeedsRedraw = true; staticBackgroundDrawn = false;
  if (wifiSSID.length() > 0) { connectToWiFi(); splashMessage = wifiConnected ? "WiFi test basarili!" : "WiFi test basarisiz!"; }
  else splashMessage = "WiFi bilgileri bulunamadi!";
}

// Sanal klavye callback - vkbCallbackType'a göre davranış
void handleVirtualKeyboardCallback(String result) {
  if (vkbCallbackType == "wifi") {
    String targetPass = result;
    String targetSSID = wifiSSID; // SSID already set in vkbCallbackParam usually or wifiSSID
    
    desktopMessage = "WiFi Baglaniliyor: " + targetSSID;
    
    WiFi.begin(targetSSID.c_str(), targetPass.c_str());
    esp_wifi_set_ps(WIFI_PS_MIN_MODEM);
    int connected = 0;
    for (int i = 0; i < 20; i++) {
       if (WiFi.status() == WL_CONNECTED) { connected = 1; break; }
       vTaskDelay(pdMS_TO_TICKS(500));
    }

    if (connected) {
      wifiPassword = targetPass;
      wifiConnected = true;
      currentSSID = targetSSID;
      saveWiFiCredentials();
      desktopMessage = "WiFi Baglandi: " + currentSSID;
    } else {
      wifiConnected = false;
      desktopMessage = "WiFi Baglanti Hatasi!";
    }
  } else if (vkbCallbackType == "newfile") {
    if (result.length() > 0 && sdCardPresent) {
      String fullPath = vkbCallbackParam + result;
      File nf = SD.open(fullPath, FILE_WRITE);
      if (nf) { nf.close(); desktopMessage = "Dosya olusturuldu: " + result; }
    }
    fileManagerNeedsRedraw = true;
  } else if (vkbCallbackType == "newfolder") {
    if (result.length() > 0 && sdCardPresent) {
      SD.mkdir(vkbCallbackParam + result);
      desktopMessage = "Klasor olusturuldu: " + result;
    }
    fileManagerNeedsRedraw = true;
  }
  // Klavyeyi kapat ve desktop'ı yenile
  closeVirtualKeyboard();
  staticBackgroundDrawn = false;
  desktopNeedsRedraw = true;
}

// ── HTTP hata kodu açıklaması ──────────────────────────────
static String httpErrorDesc(int code) {
  switch (code) {
    case 400: return "Gecersiz Istek (400)";
    case 401: return "Yetkisiz - Giris Gerekiyor (401)";
    case 403: return "Erisim Engellendi (403)";
    case 404: return "Sayfa Bulunamadi (404)";
    case 405: return "Izin Verilmeyen Metod (405)";
    case 408: return "Istek Zaman Asimina Ugradi (408)";
    case 429: return "Cok Fazla Istek - Oran Siniri (429)";
    case 500: return "Sunucu Ic Hatasi (500)";
    case 502: return "Kotu Ag Gecidi (502)";
    case 503: return "Servis Kullanilamiyor (503)";
    case 504: return "Ag Gecidi Zaman Asimi (504)";
    case -1:  return "Baglanti Kurulamadi";
    case -11: return "Baglanti Zaman Asimi";
    case -4:  return "DNS Cozumlenemedi";
    default:  return code > 0 ? "HTTP Hatasi " + String(code) : "Ag Hatasi " + String(code);
  }
}

// ── JSON Pretty Printer (minimal, key:value satırları) ────
static String formatJSON(const String& raw) {
  String out = "[JSON]\n";
  int depth = 0;
  bool inStr = false;
  bool escaped = false;
  String key = "", val = "";
  bool inKey = false, inVal = false, colonSeen = false;

  for (int i = 0; i < (int)raw.length() && out.length() < 2400; i++) {
    char c = raw[i];
    if (escaped) { escaped = false; if (inStr) { if (inKey) key += c; else if (inVal) val += c; } continue; }
    if (c == '\\' && inStr) { escaped = true; continue; }
    if (c == '"') {
      if (!inStr) {
        inStr = true;
        if (!colonSeen) { inKey = true; inVal = false; key = ""; }
        else             { inVal = true; inKey = false; val = ""; }
      } else {
        inStr = false;
        if (inKey) { inKey = false; }
        else if (inVal) {
          inVal = false; colonSeen = false;
          // Print key: val
          for (int d = 0; d < depth; d++) out += "  ";
          out += key + ": " + val + "\n";
          key = ""; val = "";
        }
      }
      continue;
    }
    if (inStr) { if (inKey) key += c; else if (inVal) val += c; continue; }
    if (c == ':') { colonSeen = true; inVal = false; val = ""; continue; }
    if (c == '{' || c == '[') { depth++; key = ""; val = ""; colonSeen = false; continue; }
    if (c == '}' || c == ']') { if(depth>0) depth--; colonSeen = false; key = ""; continue; }
    if (c == ',') { colonSeen = false; continue; }
    // bare number/bool val
    if (colonSeen && !inStr && !inVal && (isDigit(c) || c == '-' || c == 't' || c == 'f' || c == 'n')) {
      val = "";
      while (i < (int)raw.length() && raw[i] != ',' && raw[i] != '}' && raw[i] != ']' && raw[i] != '\n') {
        val += raw[i++];
      }
      i--;
      val.trim();
      if (key.length() > 0) {
        for (int d = 0; d < depth; d++) out += "  ";
        out += key + ": " + val + "\n";
      }
      key = ""; val = ""; colonSeen = false;
    }
  }
  return out;
}

// ── HTML'den metin çıkar (akıllı mod: önce text etiketleri, yoksa her şey) ──
static String extractHTML(const String& raw, bool smartOnly) {
  String out = "";
  int i = 0;
  int rawLen = raw.length();

  while (i < rawLen && out.length() < 2500) {
    if (raw[i] == '<') {
      int tagEnd = raw.indexOf('>', i);
      if (tagEnd == -1) break;
      String tag = raw.substring(i + 1, tagEnd); tag.trim();
      String tagLow = tag; tagLow.toLowerCase();
      i = tagEnd + 1;

      // Smart mode: skip nav/header/footer/etc. blocks
      if (smartOnly &&
          (tagLow.startsWith("nav")   || tagLow.startsWith("header") ||
           tagLow.startsWith("footer")|| tagLow.startsWith("form")   ||
           tagLow.startsWith("aside") || tagLow.startsWith("menu")   ||
           tagLow.startsWith("script")|| tagLow.startsWith("style")  ||
           tagLow.startsWith("iframe")|| tagLow.startsWith("button") ||
           tagLow.startsWith("select")|| tagLow.startsWith("input")  ||
           tagLow.startsWith("noscript"))) {
        int firstSpace = tagLow.indexOf(' ');
        String tagName = (firstSpace == -1) ? tagLow : tagLow.substring(0, firstSpace);
        if (tagName.length() == 0) continue;
        String closeTag = "</" + tagName;
        String rawLow = raw.substring(i);
        rawLow.toLowerCase();
        int closePos = rawLow.indexOf(closeTag);
        if (closePos != -1) i = i + closePos + tagName.length() + 3;
        continue;
      }

      // Newline after block elements
      if (tagLow.startsWith("p")  || tagLow.startsWith("h1") || tagLow.startsWith("h2") ||
          tagLow.startsWith("h3") || tagLow.startsWith("h4") || tagLow.startsWith("h5") ||
          tagLow.startsWith("h6") || tagLow.startsWith("li") || tagLow.startsWith("br") ||
          tagLow.startsWith("tr") || tagLow.startsWith("pre")|| tagLow.startsWith("blockquote") ||
          tagLow.startsWith("div") || tagLow.startsWith("span")) {
        if (!smartOnly && out.length() > 0 && out[out.length()-1] != '\n') out += '\n';
        else if (smartOnly && out.length() > 0 && out[out.length()-1] != '\n') out += '\n';
      }
    } else {
      char c = raw[i++];
      if (c == '&') {
        String ent = ""; int j = i;
        while (j < rawLen && raw[j] != ';' && j < i + 8) ent += raw[j++];
        if (j < rawLen && raw[j] == ';') {
          if (ent == "amp") out += '&';
          else if (ent == "lt") out += '<';
          else if (ent == "gt") out += '>';
          else if (ent == "nbsp" || ent == "#160") out += ' ';
          i = j + 1;
        } else { out += c; }
      } else if (c != '\r') {
        out += c;
      }
    }
  }
  return out;
}

// ── Browser Arka Plan Task'i ──────────────────────────────
static void webBrowserTask(void* pv) {
  String url = *((String*)pv);
  delete (String*)pv;

  webBrowserLoading = true;
  webBrowserStatus = "Baglaniliyor...";
  setStatusLED(EYU_LED_YELLOW);
  webBrowserContent = "";
  
  HTTPClient http;
  http.setTimeout(10000); // 10s timeout
  http.begin(url);
  http.addHeader("User-Agent", "EyudiOS/1.0 (ESP32-S3)");
  http.addHeader("Accept", "text/html,application/json,*/*");
  
  int code = http.GET();
  webBrowserStatus = "Veri isleniyor...";

  if (code == 301 || code == 302 || code == 307 || code == 308) {
    String loc = http.header("Location");
    http.end();
    if (loc.length() > 0) {
      webBrowserStatus = "Yonlendiriliyor...";
      // Redirect için yeni task açmak yerine mevcut task'ta devam et
      http.begin(loc);
      code = http.GET();
    }
  }

  if (code == 200) {
    String contentType = http.header("Content-Type");
    contentType.toLowerCase();
    String raw = http.getString();
    http.end();

    if (contentType.indexOf("json") != -1 || (raw.length() > 0 && (raw[0] == '{' || raw[0] == '['))) {
      webBrowserContent = formatJSON(raw);
    } else {
      String smart = extractHTML(raw, true);
      String smartTrim = smart; smartTrim.trim();
      if (smartTrim.length() < 30) {
        webBrowserContent = "[Ham Metin]\n" + extractHTML(raw, false);
      } else {
        webBrowserContent = smart;
      }
      // Compact whitespace
      String compact = ""; int blanks = 0;
      for (int j = 0; j < (int)webBrowserContent.length(); j++) {
        char c = webBrowserContent[j];
        if (c == '\n') { blanks++; if (blanks <= 1) compact += '\n'; }
        else { blanks = 0; compact += c; }
      }
      webBrowserContent = compact;
    }
  } else {
    http.end();
    webBrowserContent = "*** " + httpErrorDesc(code) + " ***\n\nURL: " + url +
                        "\n\nYapilabilecekler:\n" +
                        "- WiFi baglantisini kontrol et\n- URL'yi dogru yazdiginizdan emin olun";
  }

  webBrowserStatus = "";
  webBrowserLoading = false;
  turnOffStatusLED();
  desktopNeedsRedraw = true; // İçerik hazır, ekranı tazele
  vTaskDelete(NULL);
}

void webBrowserLoad(String url) {
  if (WiFi.status() != WL_CONNECTED) {
    wifiConnected = false;
    webBrowserContent = "[Hata] WiFi baglantisi yok!\nOnce wifi ile baglanin (Ayarlar -> WiFi).";
    return;
  }
  wifiConnected = true;
  webBrowserURL = url;
  
  // URL kopyasını heap'e alıp task'a pasla
  String* urlPtr = new String(url);
  // SSL overhead için stack boyutu 12KB'a çıkarıldı
  xTaskCreate(webBrowserTask, "browser_task", 12288, urlPtr, 1, NULL);
}

void toggleWifiService(bool on) {

  if (on) {
    WiFi.mode(WIFI_STA);

    wifiServiceActive = true;
    logToFile("[SERVICE] WiFi Service Started");
  } else {
    WiFi.disconnect(true, true);
    WiFi.mode(WIFI_OFF);
    wifiConnected = false;
    wifiServiceActive = false;
    logToFile("[SERVICE] WiFi Service Killed (RAM Saved)");
  }
}

void pingHost(String host) {
  if (!wifiConnected) { lastPingResult = "WiFi bagli degil!"; return; }
  
  IPAddress ip;
  if (!WiFi.hostByName(host.c_str(), ip)) {
    lastPingResult = "IP cozumlenemedi: " + host;
    return;
  }
  
  lastPingResult = "Ping " + host + " [" + ip.toString() + "]:\n";
  
  for (int i = 0; i < 3; i++) {
    unsigned long start = millis();
    WiFiClient client;
    client.setTimeout(2000);
    if (client.connect(ip, 80)) {
      unsigned long ms = millis() - start;
      lastPingResult += "  Yanit: 64 byte, sure=" + String(ms) + "ms\n";
      client.stop();
    } else {
      lastPingResult += "  Zaman asimi.\n";
    }
    vTaskDelay(pdMS_TO_TICKS(500));
  }
}

void handleDisplayInputKey(ParsedKey key) {
  if (key.isArrow) {
    if (key.arrowDirection == 0) { // Up
      displayAppSelection = (displayAppSelection > 0) ? displayAppSelection - 1 : 8;
    } else if (key.arrowDirection == 1) { // Down
      displayAppSelection = (displayAppSelection < 8) ? displayAppSelection + 1 : 0;
    } else if (key.arrowDirection == 2) { // Left
      if (displayAppSelection >= 4 && displayAppSelection < 8) displayAppSelection -= 4;
    } else if (key.arrowDirection == 3) { // Right
      if (displayAppSelection < 4) displayAppSelection += 4;
    }
  } else if (key.isEnter) {
    if (displayAppSelection < 8) {
      changeResolution(displayAppSelection, useDoubleBuffering);
    } else {
      useDoubleBuffering = !useDoubleBuffering;
      changeResolution(currentResolutionIndex, useDoubleBuffering);
    }
  } else if (key.isEscape) {
    // Escape handeled in main usually to close app, but good to ensure
    currentApp = "";
    desktopNeedsRedraw = true;
  }
}
