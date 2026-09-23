#include "Globals.h"

void enterMenu(String menuName) {
  inMenu = true; sysCurrentMenu = menuName; menuSelection = 0; menuNeedsRedraw = true;
  
  if (menuName == "Main") {
    menuItems[0] = "WiFi Services";
    menuItems[1] = "Bluetooth Services";
    menuItems[2] = "Audio Control";
    menuItems[3] = "System Settings";
    menuItems[4] = "Close Menu";
    menuItemCount = 5;
  }
  else if (menuName == "WiFi") { 
    menuItems[0] = "Scan Networks"; 
    menuItems[1] = "Network List"; 
    menuItems[2] = "Disconnect"; 
    menuItems[3] = wifiAutoConnect ? "Auto: [ON]" : "Auto: [OFF]";
    menuItems[4] = "Back"; 
    menuItemCount = 5; 
  }
  else if (menuName == "Bluetooth") { 
    menuItems[0] = "Pair (Closest)"; 
    menuItems[1] = btAutoConnect ? "Auto Join: [ON]" : "Auto Join: [OFF]";
    menuItems[2] = "Clear Saved"; 
    menuItems[3] = "Back"; 
    menuItemCount = 4; 
  }
  else if (menuName == "Audio") {
    menuItems[0] = "Volume Up (+10)";
    menuItems[1] = "Volume Down (-10)";
    menuItems[2] = "Stop Audio";
    menuItems[3] = "Pause/Resume";
    menuItems[4] = "Format SD Audio";
    menuItems[5] = "Back";
    menuItemCount = 6;
  }
  else if (menuName == "Settings") {
    menuItems[0] = "Buzzer Pin: [" + String(buzzerPin) + "]";
    menuItems[1] = "Theme: [" + String(currentTheme) + "]";
    menuItems[2] = "Wallpaper: [" + String(wallpaperMode) + "]";
    menuItems[3] = "Input Driver";
    menuItems[4] = "Back";
    menuItemCount = 5;
  }
  else if (menuName == "InputDriver") {
    menuItems[0] = (currentInputDriver == 0) ? "Normal (Kbd+Mouse) [*]" : "Normal (Kbd+Mouse)";
    menuItems[1] = (currentInputDriver == 1) ? "Controllers (USB) [*]" : "Controllers (USB)";
    menuItems[2] = (currentInputDriver == 2) ? "BT Dongle [*]" : "BT Dongle";
    
    if (analogStickPreference == 0)      menuItems[3] = "Analog: Auto/Left [*]";
    else if (analogStickPreference == 1) menuItems[3] = "Analog: Force Right [*]";
    else if (analogStickPreference == 2) menuItems[3] = "Analog: Disabled [*]";
    
    menuItems[4] = "Back";
    menuItemCount = 5;
  }
}

void exitMenu() { inMenu = false; desktopNeedsRedraw = true; }

void handleMenuInput(ParsedKey key, String originalLine) {
  if (key.normalizedKey == "w" || (key.isArrow && key.arrowDirection == 0)) { menuSelection = (menuSelection - 1 + menuItemCount) % menuItemCount; menuNeedsRedraw = true; }
  else if (key.normalizedKey == "s" || (key.isArrow && key.arrowDirection == 1)) { menuSelection = (menuSelection + 1) % menuItemCount; menuNeedsRedraw = true; }
  else if (key.isEnter) { handleMenuSelection(); menuNeedsRedraw = true; }
  else if (key.isEscape) { exitMenu(); }
}

void handleBootMenuInput(ParsedKey key) {
  if (key.normalizedKey == "w" || (key.isArrow && key.arrowDirection == 0)) { bootMenuSelection = (bootMenuSelection - 1 + bootMenuCount) % bootMenuCount; bootMenuNeedsRedraw = true; }
  else if (key.normalizedKey == "s" || (key.isArrow && key.arrowDirection == 1)) { bootMenuSelection = (bootMenuSelection + 1) % bootMenuCount; bootMenuNeedsRedraw = true; }
  else if (key.isEnter) {
    switch (bootMenuSelection) {
      case 0: showBootMenu = false; break;
      case 1: sdCardPresent = false; showBootMenu = false; break;
      case 2: clearWiFiCredentials(); showBootMenu = false; break;
      case 3: clearWiFiCredentials(); sdCardPresent = false; showBootMenu = false; break;
      case 4: retrySDCard(); break;
      case 5: testWiFi(); break;
    }
    bootMenuNeedsRedraw = true;
  }
}

void handleMenuSelection() {
  if (sysCurrentMenu == "Main") {
    switch (menuSelection) {
      case 0: enterMenu("WiFi"); break;
      case 1: enterMenu("Bluetooth"); break;
      case 2: enterMenu("Audio"); break;
      case 3: enterMenu("Settings"); break;
      case 4: exitMenu(); break;
    }
  } else if (sysCurrentMenu == "WiFi") {
    switch (menuSelection) {
      case 0: scanWiFiNetworks(); break;
      case 1: enterMenu("NetworkList"); break;
      case 2: WiFi.disconnect(); wifiConnected = false; exitMenu(); break;
      case 3: wifiAutoConnect = !wifiAutoConnect; saveWiFiCredentials(); enterMenu("WiFi"); break;
      case 4: enterMenu("Main"); break;
    }
  } else if (sysCurrentMenu == "NetworkList") {
    if (menuSelection < wifiNetworkCount) {
      wifiSSID = wifiNetworks[menuSelection];
      inMenu = false;
      if (keyboard_connected) {
        showEsdos = true; currentApp = "esdos"; esdosOutputCount = 0;
        addEsdosOutput("=== WiFi Sifre Girisi ===");
        addEsdosOutput("SSID: " + wifiSSID);
        addEsdosOutput("Sifre girin ve Enter'a basin:");
        addEsdosOutput("(ESC = iptal)");
        esdosCommand = ""; vkbCallbackType  = "wifi_inline";
        vkbCallbackParam = wifiSSID; esdosNeedsRedraw = true;
      } else {
        vkbCallbackType  = "wifi"; vkbCallbackParam = "";
        showVirtualKeyboardWithCallback("Sifre: " + wifiSSID, "", handleVirtualKeyboardCallback);
        keyboardNeedsRedraw = true; desktopNeedsRedraw  = true;
      }
    } else {
      enterMenu("WiFi");
    }
  } else if (sysCurrentMenu == "Bluetooth") {
    switch (menuSelection) {
      case 0: pairNewBluetoothDevice(); exitMenu(); break;
      case 1: btAutoConnect = !btAutoConnect; EEPROM.write(401, btAutoConnect ? 1 : 0); EEPROM.commit(); enterMenu("Bluetooth"); break;
      case 2: clearBluetoothDevices(); exitMenu(); break;
      case 3: enterMenu("Main"); break;
    }
  } else if (sysCurrentMenu == "Audio") {
    switch (menuSelection) {
      case 0: audioSetVolume(min(100, audioVolume + 10)); enterMenu("Audio"); break;
      case 1: audioSetVolume(max(0, audioVolume - 10)); enterMenu("Audio"); break;
      case 2: audioStop(); exitMenu(); break;
      case 3: audioPause(); enterMenu("Audio"); break;
      case 4: /* Format SD logic can go here */ break;
      case 5: enterMenu("Main"); break;
    }
  } else if (sysCurrentMenu == "Settings") {
    switch (menuSelection) {
      case 0: 
        buzzerPin++; if (buzzerPin > 48) buzzerPin = 0;
        saveSettings(); enterMenu("Settings"); break;
      case 1:
        currentTheme = (currentTheme + 1) % 4;
        applyTheme(currentTheme); saveSettings(); enterMenu("Settings"); break;
      case 2:
        wallpaperMode = (wallpaperMode + 1) % 5;
        saveSettings(); enterMenu("Settings"); break;
      case 3: enterMenu("InputDriver"); break;
      case 4: enterMenu("Main"); break;
    }
  } else if (sysCurrentMenu == "InputDriver") {
    switch (menuSelection) {
      case 0:
        currentInputDriver = 0;
        inputDriverVerified = false;
        inputDriverWaitingForFlash = true;
        inMenu = false;
        desktopNeedsRedraw = true;
        break;
      case 1:
        currentInputDriver = 1;
        inputDriverVerified = false;
        inputDriverWaitingForFlash = true;
        inMenu = false;
        desktopNeedsRedraw = true;
        break;
      case 2:
        currentInputDriver = 2;
        inputDriverVerified = false;
        inputDriverWaitingForFlash = true;
        inMenu = false;
        desktopNeedsRedraw = true;
        break;
      case 3:
        analogStickPreference = (analogStickPreference + 1) % 3;
        saveSettings();
        enterMenu("InputDriver");
        break;
      case 4:
        enterMenu("Settings");
        break;
    }
  }
}

