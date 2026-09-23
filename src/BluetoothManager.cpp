#include <NimBLEDevice.h>
#include <EEPROM.h>
#include "Globals.h"

#define MAX_BLE_CLIENTS 3
#define BLE_EEPROM_START 100
static NimBLEClient* clients[MAX_BLE_CLIENTS];
static int clientCount = 0;
static uint32_t scanTime = 5;

static bool foundKnownDevice = false;
static String closestName = "";
static NimBLEAddress closestAddr("");
static int highestRSSI = -999;

// Flags for reconnection
static volatile bool shouldScan = false;

/**
 * EEPROM Helpers
 */
void saveBluetoothDevice(NimBLEAddress addr) {
    uint8_t const* mac = addr.getNative();
    int emptySlot = -1;
    for (int i = 0; i < MAX_BLE_CLIENTS; i++) {
        bool match = true;
        bool isEmpty = true;
        for (int j = 0; j < 6; j++) {
            if (knownBleMacs[i][j] != mac[j]) match = false;
            if (knownBleMacs[i][j] != 0xFF && knownBleMacs[i][j] != 0x00) isEmpty = false;
        }
        if (match) return; 
        if (isEmpty && emptySlot == -1) emptySlot = i;
    }
    if (emptySlot != -1) {
        for (int j = 0; j < 6; j++) knownBleMacs[emptySlot][j] = mac[j];
        if (emptySlot >= knownBleMacCount) knownBleMacCount = emptySlot + 1;
        saveSettings();
        logToFile("[BLE] Cihaz hafizaya kaydedildi.");
    }
}

void clearBluetoothDevices() {
    memset(knownBleMacs, 0xFF, sizeof(knownBleMacs));
    knownBleMacCount = 0;
    saveSettings();
    logToFile("[BLE] Cihaz hafizasi temizlendi.");
}

/**
 * Handle HID Reports
 */
static uint8_t last_hid_keys[6] = {0,0,0,0,0,0};

void notifyCallback(NimBLERemoteCharacteristic* pRemoteCharacteristic, uint8_t* pData, size_t length, bool isNotify) {
    if (length == 0) return;

    // Debug raw data if needed
    // Serial.printf("[BLE] Data len: %d, Byte0: 0x%02X\n", length, pData[0]);

    if (length >= 8) { // Keyboard (could be 8 or 9 with Report ID)
        int offset = (length == 9) ? 1 : 0;
        uint8_t modifiers = pData[offset];
        uint8_t* keys = &pData[offset + 2];
        int keyCount = length - (offset + 2);
        if (keyCount > 6) keyCount = 6;
        keyboard_connected = true;

        bool changed = false;
        for (int i = 0; i < keyCount; i++) {
            uint8_t key = keys[i];
            if (key != 0) {
                // Check if this key was already reported in the same position last time
                // (Simple de-duplication for non-repeat states)
                bool isNew = true;
                for(int j=0; j<6; j++) { if(last_hid_keys[j] == key) isNew = false; }

                if (isNew) {
                    String keyStr = "";
                    switch (key) {
                        case 0x28: keyStr = "0x28"; break; // Enter
                        case 0x29: keyStr = "0x29"; break; // Escape
                        case 0x2A: keyStr = "0x2A"; break; // Backspace
                        case 0x2B: keyStr = "0x2B"; break; // Tab
                        case 0x2C: keyStr = "0x2C"; break; // Space
                        case 0x3A ... 0x45: { // F1 ... F12
                            int fNum = (key - 0x3A) + 1;
                            keyStr = "F" + String(fNum);
                            break;
                        }
                        case 0x4F: keyStr = "0x4F"; break; // Right
                        case 0x50: keyStr = "0x50"; break; // Left
                        case 0x51: keyStr = "0x51"; break; // Down
                        case 0x52: keyStr = "0x52"; break; // Up
                        case 0x04 ... 0x1D: { // a-z
                            char c = 'a' + (key - 0x04);
                            if (modifiers & 0x22) c = toupper(c);
                            keyStr = String(c);
                            break;
                        }
                        case 0x1E ... 0x26: { // 1-9
                            if (modifiers & 0x22) {
                                const char shiftNums[] = "!@#$%^&*()";
                                keyStr = String(shiftNums[key - 0x1E]);
                            } else {
                                keyStr = String((char)('1' + (key - 0x1E)));
                            }
                            break;
                        }
                        case 0x27: keyStr = (modifiers & 0x22) ? ")" : "0"; break;
                    }
                    if (keyStr.length() > 0) {
                        String finalBuffer = "MOD:0x" + String(modifiers, HEX) + ":" + keyStr;
                        if (systemMutex != nullptr && xSemaphoreTakeRecursive(systemMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
                            usb_keyboard_buffer += finalBuffer + "\n";
                            usb_keyboard_data_available = true;
                            lastKeyboardActivity = millis();
                            xSemaphoreGiveRecursive(systemMutex);
                        }
                    }
                }
            }
        }
        // Save state
        for(int i=0; i<6; i++) last_hid_keys[i] = (i < keyCount) ? keys[i] : 0;
    } 
    else if (length >= 3 && length <= 7) { // Mouse HID report
        // === OFFSET DETECTION ===
        // Standard HID mouse report formats:
        //   3 bytes: [buttons, dx, dy]              → offset=0
        //   4 bytes: [buttons, dx, dy, wheel]        → offset=0  (MOST COMMON)
        //   5 bytes: [reportID, buttons, dx, dy, wheel] → offset=1 (if reportID<=7)
        //   6 bytes: [reportID, buttons, dx, dy, wheel, hwheel] → offset=1
        // NOTE: With offset=1 on a 4-byte report, dy reads from pData[3]=wheel (≈0)
        // which is WHY vertical movement was broken!
        int offset = 0;
        if (length >= 5) {
            // Byte[0] is a report ID if it's a small integer (1-15) AND
            // byte[1] looks like a button bitmask (fits in 3 bits = 0..7)
            if (pData[0] >= 1 && pData[0] <= 15 && pData[1] <= 0x07) {
                offset = 1;
            }
        }
        // Safety: make sure we have enough bytes after offset
        if ((int)length < offset + 3) return;

        uint8_t buttons = pData[offset];
        int8_t dx       = (int8_t)pData[offset + 1];
        int8_t dy       = (int8_t)pData[offset + 2];
        int8_t wheel    = ((int)length > offset + 3) ? (int8_t)pData[offset + 3] : 0;

        usb_mouse_dx += dx;
        usb_mouse_dy += dy;
        // Scroll wheel: map to vertical cursor movement (3px per notch)
        if (wheel != 0) usb_mouse_dy += wheel * 3;

        // Volatile değişkenlere yaz — processUSBInput main loop'ta okuyacak
        // leftButton gibi non-volatile değişkenlere DOKUNMA (race condition!)
        usb_left_button   = (buttons & 0x01);
        usb_right_button  = (buttons & 0x02);
        usb_middle_button = (buttons & 0x04);
        mouse_connected = true;
        lastMouseActivity = millis();

        // Debug (gerekirse aç):
        // Serial.printf("[MOUSE] len=%d off=%d btn=%02X dx=%d dy=%d wh=%d\n",
        //               length, offset, buttons, dx, dy, wheel);
    }



}

class ClientCallbacks : public NimBLEClientCallbacks {
    void onConnect(NimBLEClient* pClient) {
        logToFile("[BLE] Fiziksel baglanti kuruldu.");
        ble_connected = true;
        blinkStatusLED(EYU_LED_BLUE, 3, 150); // 3x Blue Blink
    }
    void onDisconnect(NimBLEClient* pClient) {
        ble_connected = false;
        logToFile("[BLE] Baglanti koptu! Reconnect baslatiliyor...");
        for (int i = 0; i < clientCount; i++) {
            if (clients[i] == pClient) {
                for (int j = i; j < clientCount - 1; j++) clients[j] = clients[j+1];
                clientCount--;
                break;
            }
        }
        shouldScan = true; 
    }
    void onAuthenticationComplete(NimBLEConnInfo& connInfo) {
        if (connInfo.isEncrypted()) {
            Serial.println("[BLE] Guvenli Bag (Bonding) OK.");
            saveBluetoothDevice(connInfo.getAddress());
        }
    }
};

static ClientCallbacks clientCB;

bool connectToDevice(NimBLEAddress addr) {
    if (addr.equals(NimBLEAddress(""))) return false;
    if (clientCount >= MAX_BLE_CLIENTS) return false;
    
    NimBLEClient* pClient = NimBLEDevice::createClient();
    pClient->setClientCallbacks(&clientCB, false);
    
    // Bağlantı parametrelerini iyileştir: 15ms-15ms interval, 0 latency, 6s timeout
    pClient->setConnectionParams(12, 12, 0, 600);
    
    // Cihaza bağlan (false = don't wait for MTU/discovery initially)
    if (!pClient->connect(addr, false)) { 
        NimBLEDevice::deleteClient(pClient); 
        return false; 
    }

    logToFile("[BLE] Servisler kesfediliyor...");
    
    // Servis keşfi için bekle; RC=7 (EDONE) durumunu NimBLE dahili yönetir
    NimBLERemoteService* pHidSvc = pClient->getService(NimBLEUUID((uint16_t)0x1812));
    if (pHidSvc) {
        auto chrs = pHidSvc->getCharacteristics(true);
        if (!chrs) return false;
        
        bool subscribed = false;
        for (auto &chr : *chrs) {
            // Report Map (0x2A4B) veya Input Report (0x2A4D)
            if (chr->getUUID() == NimBLEUUID((uint16_t)0x2A4D) && chr->canNotify()) {
                if (chr->subscribe(true, notifyCallback)) {
                    logToFile("[BLE] HID Subscribed: " + String(chr->getUUID().toString().c_str()));
                    subscribed = true;
                }
            }
        }
        if (subscribed) {
            clients[clientCount++] = pClient;
            logToFile("[BLE] Cihaz TAMAM.");
            return true;
        }
    }

    logToFile("[BLE] HID Servisi bulunamadi veya reddedildi.");
    pClient->disconnect();
    return false;
}

void loadBluetoothDevices() {
    loadSettings();
}

class MyScanCallbacks: public NimBLEAdvertisedDeviceCallbacks {
    void onResult(NimBLEAdvertisedDevice* advertisedDevice) {
        if (!advertisedDevice->haveServiceUUID() || !advertisedDevice->isAdvertisingService(NimBLEUUID((uint16_t)0x1812))) return;
        
        NimBLEAddress addr = advertisedDevice->getAddress();
        uint8_t const* mac = addr.getNative();
        
        bool isKnown = false;
        for (int i = 0; i < MAX_BLE_CLIENTS; i++) {
            bool match = true;
            for (int j = 0; j < 6; j++) {
                if (knownBleMacs[i][j] != mac[j]) { match = false; break; }
            }
            if (match) { isKnown = true; break; }
        }

        if (isKnown) {
            NimBLEDevice::getScan()->stop();
            closestAddr = addr;
            closestName = advertisedDevice->getName().c_str();
            foundKnownDevice = true;
            return;
        } else if (!foundKnownDevice) {
            int rssi = advertisedDevice->getRSSI();
            if (rssi > highestRSSI) { 
                highestRSSI = rssi; 
                closestAddr = addr;
                closestName = advertisedDevice->getName().c_str();
            }
        }
    }
};

void pairNewBluetoothDevice() {
    foundKnownDevice = false;
    closestName = "";
    highestRSSI = -999;
    updateBootStatus("Bluetooth taraniyor...");
    
    NimBLEScan* pScan = NimBLEDevice::getScan();
    pScan->start(scanTime); // Blocking scan
    
    if (closestAddr != NimBLEAddress("") && closestName.length() > 0) {
        updateBootStatus("Pairing: " + closestName);
        connectToDevice(closestAddr);
    }
}
 
static volatile bool bleScanningActive = false;

void bluetoothTask(void* parameter) {
    vTaskDelay(pdMS_TO_TICKS(2000)); // Allow setup to finish
    while(true) {
        if (btServiceActive && NimBLEDevice::getInitialized() && !showDoom && !showEyudio && !ble_connected && (clientCount < MAX_BLE_CLIENTS || shouldScan)) {
            Serial.println("[BLE] Tarama baslatiliyor...");
            shouldScan = false;
            foundKnownDevice = false;
            highestRSSI = -999;
            closestName = "";
            
            NimBLEScan* pScan = NimBLEDevice::getScan();
            if (pScan && btServiceActive && NimBLEDevice::getInitialized()) {
                bleScanningActive = true;
                pScan->start(scanTime); // Blocking scan
                bleScanningActive = false;
                
                if (btServiceActive && NimBLEDevice::getInitialized() && (foundKnownDevice || (closestAddr != NimBLEAddress("") && closestName.length() > 0))) {
                    logToFile("[BLE] Cihaza baglaniliyor: " + closestName);
                    connectToDevice(closestAddr);
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(3000));
    }
}

static bool taskCreated = false;

void initBluetoothHost() {
    if (!NimBLEDevice::getInitialized()) {
        NimBLEDevice::init("EyudiOS S3");
        NimBLEDevice::setSecurityAuth(true, true, true);
        NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);
        NimBLEDevice::setSecurityRespKey(BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID);
    }
    
    NimBLEScan* pScan = NimBLEDevice::getScan();
    pScan->setAdvertisedDeviceCallbacks(new MyScanCallbacks(), false);
    pScan->setActiveScan(true); // Active scan requests Scan Response, needed for many HID mice
    pScan->setInterval(150);
    pScan->setWindow(100);
    
    if (!taskCreated) {
        xTaskCreatePinnedToCore(bluetoothTask, "ble_task", 8192, NULL, 1, NULL, 0); // Reduced to 8K
        taskCreated = true;
    }
    
    loadBluetoothDevices();
    btAutoConnect = (EEPROM.read(401) != 0);
    shouldScan = btAutoConnect; 
}

void toggleBluetoothService(bool on) {
  if (on) {
    btServiceActive = true;
    if (!NimBLEDevice::getInitialized()) {
      NimBLEDevice::init("EyudiOS S3");
      initBluetoothHost();
    }
    logToFile("[SERVICE] Bluetooth Service Started");
  } else {
    btServiceActive = false;
    ble_connected = false;
    mouse_connected = false;
    keyboard_connected = false;

    if (NimBLEDevice::getInitialized()) {
      NimBLEScan* pScan = NimBLEDevice::getScan();
      if (pScan && pScan->isScanning()) {
          pScan->stop();
      }
      int waitMs = 0;
      while (bleScanningActive && waitMs < 500) {
          vTaskDelay(pdMS_TO_TICKS(50));
          waitMs += 50;
      }
      vTaskDelay(pdMS_TO_TICKS(100));
      NimBLEDevice::deinit(true); // true = clear all memory
      logToFile("[SERVICE] Bluetooth Service Killed (RAM Saved)");
    }
  }
}
