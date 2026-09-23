#include "Globals.h"
#include <SD.h>
#include <ArduinoJson.h>

int SCREEN_WIDTH = DEFAULT_SCREEN_WIDTH;
int SCREEN_HEIGHT = DEFAULT_SCREEN_HEIGHT;
int currentResolutionIndex = 1; // 400x300 default
bool useDoubleBuffering = true;
bool wifiServiceActive = true;
bool btServiceActive = true;

VGA vga;
S3VGAWrapper videodisplay(vga);
Adafruit_NeoPixel statusLED(NEOPIXEL_COUNT, NEOPIXEL_PIN, NEO_GRB + NEO_KHZ800);
SemaphoreHandle_t systemMutex = NULL;
SemaphoreHandle_t sdMutex     = NULL;  // SD kart ve HTTP binary semaphore

volatile int usb_mouse_dx = 0, usb_mouse_dy = 0;
volatile bool usb_left_button = false, usb_right_button = false, usb_middle_button = false;
volatile bool usb_keyboard_data_available = false;
String usb_keyboard_buffer = "";

Window windows[MAX_WINDOWS];
int windowCount = 0;
int focusedWindowIndex = -1;

DesktopItem desktopItems[16];
int desktopItemCount = 0;
int selectedDesktopIndex = 0;
int firstDisplayedIndex = 0;
int firstDisplayedRow = 0;
bool desktopNeedsRedraw = true;
bool firstEntryToDesktop = true;
String desktopMessage = "";
bool scrollbarDragging = false;
int scrollbarDragStartY = 0;

// --- Eyudio System App ---
bool showEyudio = false;
int eyudioActiveTab = 0; // 0: Sohbet & Odalar, 1: Eşleşme (QR)
bool eyudioFullscreenQR = false;
String eyudioSelectedChatId = "notlarim";
String eyudioReplyToPostId = "";
String eyudioEditingPostId = "";
String eyudioPeerIP = "";
bool eyudioIsTransferring = false;
int eyudioTransferProgress = 0;
String eyudioTransferStatus = "";

std::vector<EyudioRoom> eyudioRooms;
int eyudioActiveRoomIndex = 0;

void loadSessionRooms() {
    eyudioRooms.clear();

    // 1. Notlarim Room (Always available default room)
    EyudioRoom notesRoom;
    notesRoom.roomId = "notlarim";
    notesRoom.roomName = "Notlarim";
    notesRoom.roomType = "notes";
    notesRoom.peerUid = "";
    notesRoom.peerIp = "";
    notesRoom.unreadCount = 0;
    notesRoom.lastMessagePreview = "Kisisel Notlar";
    notesRoom.lastTimestamp = 0;
    eyudioRooms.push_back(notesRoom);

    // 2. Load /eyudio/session.json from SD card if available
    if (sdCardPresent && SD.exists("/eyudio/session.json")) {
        File sf = SD.open("/eyudio/session.json", FILE_READ);
        if (sf) {
            size_t sz = sf.size();
            if (sz > 0 && sz < 65536) {
                char *buf = (char*)heap_caps_malloc(sz + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
                if (!buf) buf = (char*)malloc(sz + 1);
                if (buf) {
                    sf.read((uint8_t*)buf, sz);
                    buf[sz] = '\0';

                    JsonDocument doc;
                    DeserializationError err = deserializeJson(doc, buf);
                    if (!err && doc.is<JsonObject>()) {
                        JsonObject obj = doc.as<JsonObject>();

                        // Friends & Friend Profiles
                        if (obj["friendProfiles"].is<JsonArray>()) {
                            JsonArray farr = obj["friendProfiles"].as<JsonArray>();
                            for (JsonObject fitem : farr) {
                                String fuid = fitem["uid"].as<String>();
                                if (fuid.length() == 0) fuid = fitem["maskedId"].as<String>();

                                String fname = fitem["username"].as<String>();
                                String fdisc = fitem["discriminator"].as<String>();
                                if (fuid.length() > 0) {
                                    if (fdisc.length() > 0 && fdisc != "[Erişim Kısıtlı]") {
                                        fname += "#" + fdisc;
                                    }
                                    if (fname.length() == 0) fname = fuid;

                                    bool exists = false;
                                    for (const auto &r : eyudioRooms) {
                                        if (r.roomId == fuid) { exists = true; break; }
                                    }
                                    if (!exists) {
                                        EyudioRoom r;
                                        r.roomId = fuid;
                                        r.roomName = fname;
                                        r.roomType = "private";
                                        r.peerUid = fuid;
                                        r.peerIp = "";
                                        r.unreadCount = 0;
                                        r.lastMessagePreview = "Ozel Sohbet";
                                        r.lastTimestamp = 0;
                                        eyudioRooms.push_back(r);
                                    }
                                }
                            }
                        } else if (obj["friends"].is<JsonArray>()) {
                            JsonArray farr = obj["friends"].as<JsonArray>();
                            for (JsonVariant fitem : farr) {
                                String fuid = fitem.as<String>();
                                if (fuid.length() > 0) {
                                    bool exists = false;
                                    for (const auto &r : eyudioRooms) {
                                        if (r.roomId == fuid) { exists = true; break; }
                                    }
                                    if (!exists) {
                                        EyudioRoom r;
                                        r.roomId = fuid;
                                        r.roomName = fuid;
                                        r.roomType = "private";
                                        r.peerUid = fuid;
                                        r.peerIp = "";
                                        r.unreadCount = 0;
                                        r.lastMessagePreview = "Ozel Sohbet";
                                        r.lastTimestamp = 0;
                                        eyudioRooms.push_back(r);
                                    }
                                }
                            }
                        }

                        // Group IDs & Group Names
                        JsonObject gNames = obj["groupNames"].as<JsonObject>();
                        if (obj["groupIds"].is<JsonArray>()) {
                            JsonArray garr = obj["groupIds"].as<JsonArray>();
                            for (JsonVariant gitem : garr) {
                                String gid = gitem.as<String>();
                                if (gid.length() > 0) {
                                    String gname = gNames[gid].as<String>();
                                    if (gname.length() == 0) gname = "Grup " + gid;

                                    bool exists = false;
                                    for (const auto &r : eyudioRooms) {
                                        if (r.roomId == gid) { exists = true; break; }
                                    }
                                    if (!exists) {
                                        EyudioRoom r;
                                        r.roomId = gid;
                                        r.roomName = gname;
                                        r.roomType = "group";
                                        r.peerUid = "";
                                        r.peerIp = "";
                                        r.unreadCount = 0;
                                        r.lastMessagePreview = "Grup Sohbeti";
                                        r.lastTimestamp = 0;
                                        eyudioRooms.push_back(r);
                                    }
                                }
                            }
                        }
                    }
                    if (heap_caps_get_allocated_size(buf) > 0) heap_caps_free(buf); else free(buf);
                }
            }
            sf.close();
        }
    }

    if (eyudioActiveRoomIndex < 0 || eyudioActiveRoomIndex >= (int)eyudioRooms.size()) {
        eyudioActiveRoomIndex = 0;
    }
    if (!eyudioRooms.empty()) {
        eyudioSelectedChatId = eyudioRooms[eyudioActiveRoomIndex].roomId;
    } else {
        eyudioSelectedChatId = "notlarim";
    }
}




int mouseX = 160, mouseY = 120;
int prevMouseX = -1, prevMouseY = -1;
int oldMouseX = 0, oldMouseY = 0;
bool leftButton = false, rightButton = false, middleButton = false;
bool prevLeftButton = false, prevRightButton = false, prevMiddleButton = false;
unsigned long lastMouseActivity = 0;
String keyboardBuffer = "";
unsigned long lastKeyboardActivity = 0;
bool keyboardActive = false;

bool mouse_connected = false, keyboard_connected = false, ble_connected = false;
bool staticBackgroundDrawn = false;
unsigned long lastFrameTime = 0;
unsigned long lastUpdateTime = 0;
int hoveredItemIndex = -1;
bool showAppMenu = false;
int menuX = 0, menuY = 0;
bool sdCardPresent = false;
bool usbPresent = false;
String lastUsbResponse = "";
volatile bool usbResponseReady = false;
String currentApp = "";
bool vgaInitialized = false;
bool showSplash = false;
String splashMessage = "";

bool showBootMenu = false;
int bootMenuSelection = 0;
String bootMenuItems[] = { "Continue", "Skip SD Card", "Reset WiFi", "Factory Reset", "Retry SD Card", "Test WiFi" };
int bootMenuCount = 6;
bool bootMenuNeedsRedraw = false;

bool inMenu = false;
String sysCurrentMenu = "";
int menuSelection = 0;
String menuItems[10];
int menuItemCount = 0;
bool menuNeedsRedraw = false;

String systemLogs[MAX_SYSTEM_LOGS];
int systemLogCount = 0;
bool showSystemLogs = false;
bool systemLogsNeedsRedraw = false;

uint8_t knownBleMacs[3][6];
int knownBleMacCount = 0;
bool wifiConnected = false;
bool ftpDebugMode = false; // Log modu kapali baslar
bool debugMode = true;     // Derinlemesine Serial loglama anahtari (Varsayilan: ACİK)
bool wifiAutoConnect = true;
bool btAutoConnect = true;
String wifiSSID = "", wifiPassword = "";
String currentSSID = "", currentPassword = "";

void logDebug(const String &msg) {
    if (debugMode) {
        Serial.println(msg);
    }
}

void logDebugf(const char *fmt, ...) {
    if (debugMode) {
        char buf[256];
        va_list args;
        va_start(args, fmt);
        vsnprintf(buf, sizeof(buf), fmt, args);
        va_end(args);
        Serial.print(buf);
    }
}

bool showVirtualKeyboard = false;
String keyboardInput = "";
int keyboardCursor = 0;
bool keyboardShift = false, keyboardNumbers = false;
bool keyboardNeedsRedraw = false;
int keyboardSelectedRow = 0, keyboardSelectedCol = 0;
String keyboardKeyRows[5];
int keyboardRowLengths[5];

int menuFirstVisible = 0;
const int menuMaxVisible = 7;
int menuScrollOffset = 0;
int bootMenuFirstVisible = 0;
const int bootMenuMaxVisible = 7;

bool showFileManager = false;
String fileList[20];
int fileCount = 0;
int selectedFile = 0;
int fmHoverIndex = -1;
int fileManagerFirstVisible = 0;
bool fileManagerNeedsRedraw = false;
String currentFilePath = "";
bool editingFile = false;
String fileContent = "";
String editingFilePath = "";
bool showFileMenu = false;
int fmMenuX = 0, fmMenuY = 0;
int fmMenuSelection = 0;
bool showFileHelp = false;
bool fileMetadataVisible = false;
String fileMenuAction = "";
String fileSourcePath = "";
String renamingFileName = "";
String renamingInput = "";
bool renamingActive = false;
String selectedWadPath = "/sd/doom1.wad";

volatile bool runningScript = false;
ScriptValue* scriptVariables = nullptr; // ~2.5KB PSRAM'a
String* scriptVarNames = nullptr;      // ~1.5KB PSRAM'a
ScriptValue* vmPsramRegs = nullptr;
int scriptVarCount = 0;
String scriptKeyBuffer = "";
volatile bool scriptKeyAvailable = false;
volatile bool heldKeys[256] = {false};
volatile uint8_t lastKeyDownHID = 0;
volatile uint8_t lastKeyUpHID = 0;
volatile bool keyEventAvailable = false;
uint8_t* wallpaperBuffer = nullptr;
bool wallpaperBufferLoaded = false;
volatile bool bmpPreviewActive = false;
volatile bool systemInErrorState = false;
String gsodTitle = "";
String gsodMessage = "";
volatile bool gsodPending = false;

// ── Input Driver Handshake ──────────────────────────────────
int currentInputDriver = 0;             // 0: Normal, 1: Controllers, 2: BT Dongle
int analogStickPreference = 0;          // 0: Auto/Left with Right Backup, 1: Force Right, 2: Disabled
bool inputDriverVerified = false;
bool inputDriverWaitingForFlash = false;
unsigned long lastDriverStatusSend = 0;

// ── Multi-player Gamepad States ──────────────────────────────
bool gamepadButtons[2][14] = {{false}};
int16_t gamepadAnalog[2][4] = {{0}};

JsonDocument lastJson;

char (*scriptLines)[MAX_SCRIPT_LINE_LEN] = nullptr;
Instruction* scriptProgram = nullptr;
char* scriptStringPool = nullptr;
int scriptStringPoolPtr = 0;

int scriptLineCount = 0;
int scriptCurrentLine = 0;
int lineNumber = 0;
ScriptLabel* scriptLabels = nullptr; // ~0.8KB PSRAM'a
int scriptLabelCount = 0;
int scriptErrorLine = 0;

ScriptFunction* scriptFunctions = nullptr; // ~0.6KB PSRAM'a
int scriptFunctionCount = 0;
ScriptEvent scriptEvents[MAX_EVENTS];
int scriptEventCount = 0;

const char* getPooledString(int offset) {
  if (offset < 0 || offset >= scriptStringPoolPtr) return "";
  return &scriptStringPool[offset];
}

int addToStringPool(const char* s) {
  int len = strlen(s);
  if (scriptStringPoolPtr + len + 1 >= STRING_POOL_SIZE) return -1;
  
  // Check if string already exists in pool (simple deduplication)
  int p = 0;
  while (p < scriptStringPoolPtr) {
    if (strcmp(&scriptStringPool[p], s) == 0) return p;
    p += strlen(&scriptStringPool[p]) + 1;
  }

  int offset = scriptStringPoolPtr;
  strcpy(&scriptStringPool[offset], s);
  scriptStringPoolPtr += len + 1;
  return offset;
}

int gosubStack[MAX_GOSUB_DEPTH];
int gosubTop = -1;

LoopInfo loopStack[MAX_LOOP_DEPTH];
int loopStackTop = -1;

bool vmDebugActive = false; // Set true only for debugging
bool vmShowFps = false;
bool showEsdos = false;
String esdosCommand = "";
int esdosCursorPos = 0;
String esdosOutput[15];
int esdosOutputCount = 0;
String esdosCurrentPath = "/";
int esdosHistoryIndex = 0;
String esdosHistory[10];
int esdosHistoryCount = 0;
bool esdosNeedsRedraw = false;
int displayAppSelection = 0;
String lastPingResult = "";

#ifdef __cplusplus
extern "C" {
#endif
volatile bool showDoom = false;
bool doomWadLoaded = false;
volatile bool doomFrameReady = false;
uint8_t* doomFrameBuffer = nullptr;
volatile int doomLoadingProgress = 0;
char doomLoadingText[64] = "";
volatile bool doomLoadingActive = false;
#ifdef __cplusplus
}
#endif
String wifiNetworks[10];
int wifiNetworkCount = 0;
int selectedNetwork = 0;
bool showNetworkList = false;

bool wifiScanning = false;
uint32_t freeHeap, minFreeHeap, maxAllocHeap, totalHeap;
uint32_t freePsram, totalPsram, cpuFreq, uptime;
uint32_t flashSize, flashSpeed, flashMode, chipModel, chipRevision, chipCores, chipFeatures;

bool showEyuditor = false;
String eyuditorPath = "";
char (*eyuLines)[EYU_MAX_COLS + 1] = nullptr;
int eyuditorLineCount = 0;
int eyuditorCursorRow = 0;
int eyuditorCursorCol = 0;
int eyuditorFirstVisibleRow = 0;
bool eyuditorNeedsRedraw = false;
bool eyuditorSaveAsActive = false;
char eyuSaveAsName[EYU_MAX_COLS + 1];

VirtualKeyboardCallback vkbCallback = NULL;
String vkbTitle = "";
String vkbInitialText = "";
bool vkbActive = false;
String vkbCallbackType = "";
String vkbCallbackParam = "";

// --- EyuMarket ---
String marketScripts[30];
String marketDescs[30];
String marketUrls[30];
String marketFiles[30];
int marketScriptCount = 0;
int marketHoverIdx = -1;
int marketSelection = 0;
int marketScrollOffset = 0;

// --- Task Handles ---
TaskHandle_t scriptTaskHandle     = NULL;
TaskHandle_t systemUpdateTaskHandle = NULL;

// --- Donanım Kesme (Interrupt) Sistemi Globals ---
GpioInterrupt registeredInterrupts[4]; // Defined here for the linker
int gpioIntCount = 0;

// --- Background Tasks ---
BgTask bgTasks[MAX_BG_TASKS];
QueueHandle_t ipcQueue = NULL;

// --- FTP / Browser ---
// ftpRunning FtpServer.cpp'de tanimlidir (extern Globals.h'da)
String webBrowserURL = "";
String webBrowserContent = "";
String webBrowserStatus = "";
int webBrowserScrollY = 0;
bool webBrowserLoading = false;


// --- EyuScript Arrays ---
ScriptArray* scriptArrays = nullptr;
int scriptArrayCount = 0;

ScriptArray* findArray(const char* name) {
  for (int i = 0; i < scriptArrayCount; i++)
    if (strncmp(scriptArrays[i].name, name, 23) == 0) return &scriptArrays[i];
  return nullptr;
}

// --- Sprite Engine (NEW) ---
Sprite globalSprites[MAX_SPRITES];
int spriteCount = 0;

void initSprites() {
  for (int i = 0; i < MAX_SPRITES; i++) {
    globalSprites[i].data = nullptr;
    globalSprites[i].loaded = false;
  }
}

void freeSprite(int id) {
  if (id >= 0 && id < MAX_SPRITES && globalSprites[id].loaded) {
    if (globalSprites[id].data) free(globalSprites[id].data);
    globalSprites[id].data = nullptr;
    globalSprites[id].loaded = false;
  }
}

// --- Audio state (AudioManager.cpp'de tanimli: audioReady, audioPlaying vb.) ---

// --- Theme / Wallpaper ---
int currentTheme = 0;
int wallpaperMode = 0;
int desktopGridMode = 0;  // 0=normal 1=large grid 2=minimal list
unsigned long wallpaperLastUpdate = 0;
uint16_t themeColorBG = 0, themeColorPanel = 0, themeColorAccent = 0, themeColorText = 0, themeColorGrey = 0;


int buzzerPin = 18; // Default pin (Audio PDM Pin)

void saveSettings() {
  EEPROM.write(410, buzzerPin);
  EEPROM.commit();

  if (sdCardPresent || usbPresent) {
    JsonDocument doc;
    
    JsonObject wifi = doc["wifi"].to<JsonObject>();
    wifi["ssid"] = wifiSSID;
    wifi["password"] = wifiPassword;
    wifi["auto_connect"] = wifiAutoConnect;
    
    JsonObject bt = doc["bluetooth"].to<JsonObject>();
    bt["auto_connect"] = btAutoConnect;
    
    JsonArray btMacs = bt["known_devices"].to<JsonArray>();
    for (int i = 0; i < 3; i++) {
      bool valid = false;
      for (int j = 0; j < 6; j++) {
        if (knownBleMacs[i][j] != 0xFF && knownBleMacs[i][j] != 0x00) {
          valid = true;
          break;
        }
      }
      if (valid) {
        char macStr[18];
        snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X",
                 knownBleMacs[i][0], knownBleMacs[i][1], knownBleMacs[i][2],
                 knownBleMacs[i][3], knownBleMacs[i][4], knownBleMacs[i][5]);
        btMacs.add(macStr);
      }
    }
    
    JsonObject theme = doc["theme"].to<JsonObject>();
    theme["current"] = currentTheme;
    theme["wallpaper_mode"] = wallpaperMode;
    theme["grid_mode"] = desktopGridMode;
    
    JsonObject input = doc["input"].to<JsonObject>();
    input["driver"] = currentInputDriver;
    input["analog_mode"] = analogStickPreference;
    
    JsonObject sys = doc["system"].to<JsonObject>();
    sys["debug_mode"] = debugMode;
    
    File file = SD.open("/settings.json", "w");
    if (file) {
      String jsonStr;
      serializeJson(doc, jsonStr);
      file.write((const uint8_t*)jsonStr.c_str(), jsonStr.length());
      file.close();
      Serial.println("[SETTINGS] JSON saved successfully.");
    } else {
      Serial.println("[SETTINGS] Failed to open /settings.json for writing!");
    }
  }
}

void loadSettings() {
  uint8_t val = EEPROM.read(410);
  if (val != 0xFF && val < 49) buzzerPin = val;

  if (sdCardPresent || usbPresent) {
    if (SD.exists("/settings.json")) {
      File file = SD.open("/settings.json", "r");
      if (file) {
        size_t size = file.size();
        std::unique_ptr<char[]> buf(new char[size + 1]);
        file.read((uint8_t*)buf.get(), size);
        buf[size] = '\0';
        file.close();
        
        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, buf.get());
        if (!error) {
          if (doc["wifi"]) {
            wifiSSID = doc["wifi"]["ssid"].as<String>();
            wifiPassword = doc["wifi"]["password"].as<String>();
            wifiAutoConnect = doc["wifi"]["auto_connect"].as<bool>();
          }
          
          if (doc["bluetooth"]) {
            btAutoConnect = doc["bluetooth"]["auto_connect"].as<bool>();
            
            memset(knownBleMacs, 0xFF, sizeof(knownBleMacs));
            knownBleMacCount = 0;
            
            if (doc["bluetooth"]["known_devices"]) {
              JsonArray btMacs = doc["bluetooth"]["known_devices"].as<JsonArray>();
              int idx = 0;
              for (JsonVariant v : btMacs) {
                if (idx >= 3) break;
                String macStr = v.as<String>();
                int macVal[6];
                if (sscanf(macStr.c_str(), "%02X:%02X:%02X:%02X:%02X:%02X",
                           &macVal[0], &macVal[1], &macVal[2],
                           &macVal[3], &macVal[4], &macVal[5]) == 6) {
                  for (int j = 0; j < 6; j++) {
                    knownBleMacs[idx][j] = (uint8_t)macVal[j];
                  }
                  idx++;
                }
              }
              knownBleMacCount = idx;
            }
          }
          
          if (doc["theme"]) {
            currentTheme = doc["theme"]["current"].as<int>();
            wallpaperMode = doc["theme"]["wallpaper_mode"].as<int>();
            desktopGridMode = doc["theme"]["grid_mode"].as<int>();
            applyTheme(currentTheme);
          }
          
          if (doc["input"]) {
            currentInputDriver = doc["input"]["driver"].as<int>();
            analogStickPreference = doc["input"]["analog_mode"].as<int>();
          }

          if (doc["system"]) {
            if (doc["system"]["debug_mode"]) {
              debugMode = doc["system"]["debug_mode"].as<bool>();
            }
          }
          Serial.println("[SETTINGS] JSON loaded successfully.");
        } else {
          Serial.println("[SETTINGS] JSON parse error, using defaults.");
        }
      }
    } else {
      Serial.println("[SETTINGS] /settings.json not found. Creating default.");
      saveSettings();
    }
  }
}

void addSystemLog(String msg) {
  if (systemLogCount < MAX_SYSTEM_LOGS) {
    systemLogs[systemLogCount++] = msg;
  } else {
    for (int i = 1; i < MAX_SYSTEM_LOGS; i++) {
      systemLogs[i - 1] = systemLogs[i];
    }
    systemLogs[MAX_SYSTEM_LOGS - 1] = msg;
  }
  systemLogsNeedsRedraw = true;
}

void toggleSystemLogs() {
  showSystemLogs = !showSystemLogs;
  if (showSystemLogs) {
    currentApp = "System Logs";
  } else {
    if (currentApp == "System Logs") currentApp = "";
  }
  desktopNeedsRedraw = true;
}

void drawSystemLogs() {
  // Stub for linker
}

#include <new>
void allocatePSRAMGlobals() {
    Serial.println("[SYSTEM] Allocating EyuScript global variables in PSRAM...");
    
    // Allocate and construct scriptVariables in internal DRAM (SRAM) to avoid PSRAM String alignment deadlocks
    ScriptValue* vars = (ScriptValue*)heap_caps_malloc(MAX_SCRIPT_VARS * sizeof(ScriptValue), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    for (int i = 0; i < MAX_SCRIPT_VARS; i++) {
        new (&vars[i]) ScriptValue();
    }
    scriptVariables = vars;
    
    // Allocate and construct scriptVarNames in internal DRAM (SRAM)
    String* names = (String*)heap_caps_malloc(MAX_SCRIPT_VARS * sizeof(String), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    for (int i = 0; i < MAX_SCRIPT_VARS; i++) {
        new (&names[i]) String();
    }
    scriptVarNames = names;
    
    scriptLines = (char(*)[MAX_SCRIPT_LINE_LEN])heap_caps_malloc(MAX_SCRIPT_LINES * MAX_SCRIPT_LINE_LEN, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    scriptProgram = (Instruction*)heap_caps_malloc(MAX_SCRIPT_LINES * sizeof(Instruction), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    scriptStringPool = (char*)heap_caps_malloc(STRING_POOL_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    
    // Allocate and construct scriptLabels
    ScriptLabel* labels = (ScriptLabel*)heap_caps_malloc(MAX_LABELS * sizeof(ScriptLabel), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    for (int i = 0; i < MAX_LABELS; i++) {
        new (&labels[i]) ScriptLabel();
    }
    scriptLabels = labels;
    
    // Allocate and construct scriptFunctions
    ScriptFunction* funcs = (ScriptFunction*)heap_caps_malloc(MAX_FUNCTIONS * sizeof(ScriptFunction), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    for (int i = 0; i < MAX_FUNCTIONS; i++) {
        new (&funcs[i]) ScriptFunction();
    }
    scriptFunctions = funcs;
    
    eyuLines = (char(*)[EYU_MAX_COLS + 1])heap_caps_malloc(EYU_MAX_LINES * (EYU_MAX_COLS + 1), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    
    // Allocate and construct scriptArrays
    ScriptArray* arrays = (ScriptArray*)heap_caps_malloc(MAX_SCRIPT_ARRAYS * sizeof(ScriptArray), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    for (int i = 0; i < MAX_SCRIPT_ARRAYS; i++) {
        new (&arrays[i]) ScriptArray();
    }
    scriptArrays = arrays;
    
    // Allocate and construct vmPsramRegs back in PSRAM to free up internal DRAM for VGA DMA / USB
    ScriptValue* regs = (ScriptValue*)heap_caps_malloc(VM_PSRAM_COUNT * sizeof(ScriptValue), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    for (int i = 0; i < VM_PSRAM_COUNT; i++) {
        new (&regs[i]) ScriptValue();
    }
    vmPsramRegs = regs;

    Serial.printf("[SYSTEM] PSRAM Globals Allocation:\n");
    Serial.printf("  - scriptVariables: %p\n", scriptVariables);
    Serial.printf("  - scriptVarNames: %p\n", scriptVarNames);
    Serial.printf("  - scriptArrays: %p\n", scriptArrays);
    Serial.printf("  - vmPsramRegs: %p\n", vmPsramRegs);
    Serial.println("[SYSTEM] PSRAM Globals allocation complete.");
}
