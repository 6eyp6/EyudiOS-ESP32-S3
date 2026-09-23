#ifndef GLOBALS_H
#define GLOBALS_H

#include "SystemConfig.h"
#include "usb/usb_host.h"
#include <VGA.h>
#include <GfxWrapper.h>

// ============================================================
// === DATA STRUCTURES ===
// ============================================================
struct DesktopItem {
  String name;
  String icon;
  bool isApp;
  int appId;
};

struct ParsedKey {
  String normalizedKey;
  bool isEnter;
  bool isEscape;
  bool isBackspace;
  bool isArrow;
  bool isCmd;      // Command / GUI key basili mi?
  bool isCtrl;     // Control key basili mi?
  bool isAlt;      // Alt key basili mi?
  bool isShift;    // Shift key basili mi?
  int arrowDirection;  // 0=Up, 1=Down, 2=Left, 3=Right
  char printableChar;
};

struct Window {
  char title[32];
  char appName[32];
  int x, y, w, h;
  int prevX, prevY, prevW, prevH;  // Drag öncesi konum (ghost temizlemek için)
  bool focused;
  bool minimized;
  bool needsRedraw;
  bool dirtyRect;   // true = sadece pencere yeniden çiz (fillScreen yok)
  int id;
};

#define MAX_WINDOWS 5
extern Window windows[MAX_WINDOWS];
extern int windowCount;
extern int focusedWindowIndex;

typedef void (*VirtualKeyboardCallback)(String result);

struct ScriptLabel {
  String name;
  int lineNumber;
};

// ── EyuScript 3.0 Bytecode structures ──────────────────────
enum OpCode : uint8_t {
  OP_END = 0,
  OP_PRINT, OP_PRINTLN, OP_CLEAR, OP_SHOW,
  OP_SETCURSOR, OP_SETCOLOR, OP_SETBGCOLOR, OP_SETTEXTCOLOR,
  OP_CIRCLE, OP_FILLCIRCLE, OP_RECT, OP_DRAWRECT, OP_SQUARE, OP_LINE, OP_PIXEL,
  OP_GOTO, OP_GOSUB, OP_RETURN,
  OP_IF, OP_ELSE, OP_ELSEIF, OP_ENDIF, OP_WHILE, OP_WEND, OP_FOR, OP_NEXT,
  OP_FUNC, OP_ENDFUNC, OP_CALL,
  OP_ONCLICK, OP_ONTIMER, OP_ONGPIO, OP_EVENT_EXIT,
  OP_SET, OP_MATH, OP_ADD, OP_SUB, OP_MUL, OP_DIV, OP_MOD,
  OP_INC, OP_DEC, OP_RANDOM, OP_SEED,
  OP_ABS, OP_ROUND, OP_MIN, OP_MAX, OP_SQRT, OP_POW,
  OP_SIN, OP_COS, OP_TAN, OP_ATAN2,
  OP_VEC2_ADD, OP_VEC2_SUB, OP_VEC2_MUL, OP_VEC2_DOT,
  OP_CONCAT, OP_STRLEN, OP_SUBSTRING, OP_TOINT, OP_TOUPPER, OP_TOLOWER,
  OP_WAIT, OP_YIELD, OP_LOG, OP_TRY, OP_CATCH, OP_ENDTRY, OP_THROW,
  OP_HTTPGET, OP_HTTPPOST, OP_JSONPARSE, OP_JSONGET, OP_WSS_CONNECT, OP_WSS_SEND,
  OP_PLAY, OP_SFX, OP_AUDIO, OP_TONE, OP_DRAWBMP,
  OP_BUTTON, OP_PROGRESSBAR, OP_GETMOUSE, OP_GETSCREEN, OP_GETKEY,
  OP_WIFIBEGIN, OP_WIFIDISCONNECT, OP_WIFISTATUS,
  OP_GETIP, OP_GETMAC, OP_GETGW, OP_PING,
  OP_FILEREAD, OP_FILEWRITE, OP_FILEAPPEND, OP_FILESIZE, OP_FILECOPY, OP_FILEFIND,
  OP_PINMODE, OP_DIGITALWRITE, OP_DIGITALREAD, OP_ANALOGREAD,
  OP_ARRAY, OP_ASET, OP_AGET, OP_ALEN, OP_ASET2D, OP_AGET2D,
  OP_IPC, OP_PROFILE, OP_FLOOR, OP_CEIL,
  OP_FDEF, OP_FEND, OP_SETTEXT, OP_EXIT,
  OP_LOADSPRITE, OP_DRAWSPRITE,
  OP_ISKEYDOWN, OP_LASTKEY, OP_GETKEYEVENT,
  OP_GETGAMEPAD, OP_GETGAMEPADANALOG,
  // ── TURBO Register Opcodes (register-tabanlı, string pool yok) ──
  // p1 = dest reg (0-255), p2 = src reg A, p3 = src reg B
  OP_RMOV,
  OP_RADD,   // R[p1] = R[p2] + R[p3]
  OP_RSUB,   // R[p1] = R[p2] - R[p3]
  OP_RMUL,   // R[p1] = R[p2] * R[p3]
  OP_RDIV,   // R[p1] = R[p2] / R[p3]  sıfır → GSOD
  OP_RPIXEL,   // drawPixel(R[p1], R[p2], R[p3]==−1 ? themeAccent : R[p3])
  OP_RLINE,    // drawLine(R[p1],R[p2], R[p3],R[p4], R[p5]==−1 ? themeAccent : R[p5])
  OP_RSETCOLOR,// themeAccent = RGB(R[p1], R[p2], R[p3])
  OP_RGET_IND, // R[p1] = R[R[p2]]
  OP_RSET_IND, // R[R[p1]] = R[p2]
  OP_NOP = 254,
  OP_UNKNOWN = 255
};

// ── Instruction Param Type Tags ─────────────────────────────
// Üst 3 bit'i tip etiketi olarak kullanıyoruz, kalan 29 bit değer.
// Bu sayede p1-p5 aynı int32_t alanında int, float veya string-pool
// offset'i ayırt ederek saklayabiliriz.
#define PTAG_POOL    (0 << 29)   // String pool offset (varsayılan)
#define PTAG_INT     (1 << 29)   // Gömülü int literal  (signed 29-bit)
#define PTAG_FLOAT   (2 << 29)   // float → int32_t bit-cast
#define PTAG_REG     (3 << 29)   // Register indeksi (0-255)
#define PTAG_MASK    (7 << 29)   // Tip maskesi
#define PTAG_VAL(x)  ((x) & ~PTAG_MASK)  // Değer kısmını al

// Yardımcı: p değerinin tipini döndür
inline int paramTag(int32_t p) { return p & PTAG_MASK; }

struct Instruction {
  uint8_t op;
  int32_t p1; // Hedef Var-ID veya string-pool offset (veya etiketli literal)
  int32_t p2;
  int32_t p3;
  int32_t p4;
  int32_t p5;
};

struct LoopInfo {
  int startLine;
  int endLine;
  int counterVarId; // ID instead of name
  float counterStart;
  float counterEnd;
  float counterStep;
};

// ── Background script task slots ──────────────────────────
#define MAX_BG_TASKS  3       // Aynı anda en fazla 3 bg script
#define BG_SCRIPT_VARS 10    // Bg task başına değişken sayısı

struct ScriptFunction {
  String name;
  int startLine;
};

struct ScriptEvent {
  uint8_t type; // 0=Click, 1=Timer, 2=GPIO
  int p1;       // Pin or Interval
  int startLine;
  bool active;
  unsigned long lastRun;
};

// --- Donanım Kesme (Interrupt) Sistemi ---
struct GpioInterrupt {
  int pin;
  int startLine;
  volatile bool triggered;
};
extern GpioInterrupt registeredInterrupts[4]; // Max 4 hardware interrupts
extern int gpioIntCount;
extern TaskHandle_t scriptTaskHandle;

struct BgTask {
  bool         active;
  String       filePath;
  int          ifDepth; // For IF skip logic in ScriptEngine.cpp
  TaskHandle_t handle;
  // IPC: bg → foreground mesaj tamponu (kilitli değil, tek yönlü)
  volatile bool msgPending;
  char         msgKey[24];
  char         msgVal[64];
};

// IPC mesaj (bg task → foreground)
struct IpcMsg {
  char key[24];
  char val[64];
};

// ── EyuScript Values (Variant) ─────────────────────────────
enum ScriptValueType : uint8_t {
  VAL_NULL = 0,
  VAL_INT,
  VAL_FLOAT,
  VAL_STRING,
  VAL_OBJECT
};

struct ScriptValue {
  ScriptValueType type;
  union {
    int32_t iVal;
    float   fVal;
  } data;
  String sVal;

  ScriptValue() { type = VAL_NULL; data.iVal = 0; }
  ScriptValue(int32_t v) { type = VAL_INT; data.iVal = v; sVal = ""; }
  ScriptValue(float v) { type = VAL_FLOAT; data.fVal = v; sVal = ""; }
  ScriptValue(double v) { type = VAL_FLOAT; data.fVal = (float)v; sVal = ""; }
  void setInt(int32_t v) { type = VAL_INT; data.iVal = v; sVal = ""; }
  void setFloat(float v) { type = VAL_FLOAT; data.fVal = v; sVal = ""; }
  void setString(const String& v) { type = VAL_STRING; sVal = v; }
  
  float toFloat() const {
    if (type == VAL_FLOAT) return data.fVal;
    if (type == VAL_INT) return (float)data.iVal;
    if (type == VAL_STRING) return sVal.toFloat();
    return 0;
  }
  int32_t toInt() const {
    if (type == VAL_INT) return data.iVal;
    if (type == VAL_FLOAT) return (int32_t)data.fVal;
    if (type == VAL_STRING) return sVal.toInt();
    return 0;
  }
  String toString() const {
    if (type == VAL_STRING) return sVal;
    if (type == VAL_INT) return String(data.iVal);
    if (type == VAL_FLOAT) {
      if (data.fVal == (int32_t)data.fVal && data.fVal >= -1e9 && data.fVal <= 1e9)
        return String((int32_t)data.fVal);
      return String(data.fVal, 4);
    }
    return "";
  }
  bool toBool() const {
    if (type == VAL_INT) return data.iVal != 0;
    if (type == VAL_FLOAT) return data.fVal != 0.0f;
    if (type == VAL_STRING) return (sVal.length() > 0 && sVal != "0" && sVal != "false");
    return false;
  }
};

// ── EyuScript Array ────────────────────────────────────────
#define MAX_SCRIPT_ARRAYS 16
#define MAX_ARRAY_SIZE    128
struct ScriptArray {
  char name[24];
  ScriptValue values[MAX_ARRAY_SIZE];
  int size;
  int width; // 2D desteği için (0 ise 1D)
};

// --- Sprite Engine (NEW) ---
#define MAX_SPRITES 64
struct Sprite {
  uint8_t* data;   // 8-bit RGB332 (VGA Native)
  uint16_t w, h;
  bool loaded;
};
extern Sprite globalSprites[MAX_SPRITES];
extern int spriteCount;
void initSprites();
void freeSprite(int id);
extern ScriptArray* scriptArrays;
extern int scriptArrayCount;
ScriptArray* findArray(const char* name);

// ── Audio ──────────────────────────────────────────────────
extern bool audioReady;
extern bool audioPlaying;
extern int  audioVolume;
extern String audioCurrentFile;
void initAudio();
bool audioPlay(String path);
bool playRadioFromConfig(String txtFilePath = "/radio.txt");
void audioStop();
void audioSetVolume(int vol);
void audioPause();
void audioBeep(int hz, int ms);
String audioStatus();

// ── Rendering ──────────────────────────────────────────────
void renderBMP(const char* filename, int x, int y, bool internalCall = false);


// ── Theme / Wallpaper ──────────────────────────────────────
#define THEME_COUNT 4
extern int currentTheme;      // 0=GreenDark 1=BlueDark 2=AmberDark 3=CyanDark
extern int wallpaperMode;     // 0=static 1=matrix 2=starfield 3=plasma 4=raindrops
extern int desktopGridMode;   // 0=normal grid 1=large 4-col grid 2=minimal list
extern unsigned long wallpaperLastUpdate;
void applyTheme(int t);
void saveThemeSettings();
void loadThemeSettings();
void renderWallpaper();
void setStatusLED(uint32_t color);
void turnOffStatusLED();
void blinkStatusLED(uint32_t color, int times, int delayMs = 100);
extern uint16_t themeColorBG, themeColorPanel, themeColorAccent, themeColorText, themeColorGrey;

// ── ZIP ────────────────────────────────────────────────────
bool unzipFile(String zipPath, String destDir, String& errMsg);

// Script Engine
void runEyuScript(String filePath);
void setScriptVariable(int id, const ScriptValue& value);
void setScriptVariableByName(const String& name, const String& value);
ScriptValue getScriptVariable(int id);
ScriptValue getScriptVariableByName(const String& name);
int getVariableId(const String& name, bool createIfMissing = true);
void addScriptLabel(const String& name, int line);
int findScriptLabel(const String& name);
String evaluateValue(const String& expr);
bool compareValues(const String& left, const String& op, const String& right);
void parseJsonDynamic(JsonVariant val, const String& prefix = "");
void setScriptValue(int id, const String& val); // Convenience: set by id from String

// String-name overloads — resolve name->id then delegate (keeps legacy call sites compiling)
inline void setScriptVariable(const String& name, const String& val) { setScriptVariableByName(name, val); }
inline ScriptValue getScriptVariable(const String& name) { return getScriptVariableByName(name); }

// System Settings
extern int buzzerPin;
void saveSettings();
void loadSettings();
float getChipTemperature(); // From Applications.cpp

// ============================================================
// === CLASSES ===
// ============================================================
class S3VGAWrapper : public GfxWrapper<VGA> {
public:
  S3VGAWrapper(VGA &v) : GfxWrapper<VGA>(v, SCREEN_WIDTH, SCREEN_HEIGHT) {}
  bool init(const Mode &m, int rPin, int gPin, int bPin, int hsPin, int vsPin);
  void show() { base.show(); }
  void clear(uint16_t color = 0);
  void setFont(const GFXfont *f = NULL) { Adafruit_GFX::setFont(f); }
  uint16_t RGB(uint8_t r, uint8_t g, uint8_t b);
  void line(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint16_t color) { drawLine(x0, y0, x1, y1, color); }
};

// ============================================================
// === EXTERN GLOBALS ===
// ============================================================
extern VGA vga;
extern S3VGAWrapper videodisplay;
extern Adafruit_NeoPixel statusLED;
extern SemaphoreHandle_t systemMutex;   // UI / framebuffer lock
extern SemaphoreHandle_t sdMutex;       // SD kart & HTTP lock

extern volatile int usb_mouse_dx, usb_mouse_dy;
extern volatile bool usb_left_button, usb_right_button, usb_middle_button;
extern volatile bool usb_keyboard_data_available;
extern String usb_keyboard_buffer;

extern DesktopItem desktopItems[16];
extern int desktopItemCount;
extern int selectedDesktopIndex;
extern int firstDisplayedIndex;
extern int firstDisplayedRow;
extern bool desktopNeedsRedraw;
extern bool firstEntryToDesktop;
extern String desktopMessage;
extern bool scrollbarDragging;
extern int scrollbarDragStartY;

extern int mouseX, mouseY;
extern int prevMouseX, prevMouseY;
extern int oldMouseX, oldMouseY;
extern bool leftButton, rightButton, middleButton;
extern bool prevLeftButton, prevRightButton, prevMiddleButton;
extern unsigned long lastMouseActivity;
extern String keyboardBuffer;
extern unsigned long lastKeyboardActivity;
extern bool keyboardActive;

extern bool mouse_connected, keyboard_connected, ble_connected;
extern bool staticBackgroundDrawn;
extern unsigned long lastFrameTime;
extern unsigned long lastUpdateTime;
extern int hoveredItemIndex;
extern bool showAppMenu;
extern int menuX, menuY;
extern bool sdCardPresent;
extern bool usbPresent;
extern String lastUsbResponse;
extern volatile bool usbResponseReady;
extern String currentApp;
extern bool vgaInitialized;
extern bool showSplash;
extern String splashMessage;

extern bool showBootMenu;
extern int bootMenuSelection;
extern String bootMenuItems[];
extern int bootMenuCount;
extern bool bootMenuNeedsRedraw;

extern bool inMenu;
extern String sysCurrentMenu;
extern int menuSelection;
extern String menuItems[10];
extern int menuItemCount;
extern bool menuNeedsRedraw;

#define MAX_SYSTEM_LOGS 15
extern String systemLogs[MAX_SYSTEM_LOGS];
extern int systemLogCount;
extern bool showSystemLogs;
extern bool systemLogsNeedsRedraw;

extern uint8_t knownBleMacs[3][6];
extern int knownBleMacCount;
extern bool wifiConnected;
extern bool wifiAutoConnect;
extern bool btAutoConnect;
extern String wifiSSID, wifiPassword;
extern bool ftpRunning;
extern bool ftpDebugMode; // Yeni: EsDOS log takibi icin
extern bool debugMode;    // Tek bir yerden tum derinlemesine Serial loglarini yonetir
void logDebug(const String &msg);
void logDebugf(const char *fmt, ...);
void startFtpServer();
void stopFtpServer();

// --- FTP / Browser ---
extern String webBrowserURL;
extern String webBrowserContent;
extern String webBrowserStatus;  // loading animation status text
extern int webBrowserScrollY;
extern bool webBrowserLoading;
void webBrowserLoad(String url);

extern String currentSSID, currentPassword;

extern bool showVirtualKeyboard;
extern String keyboardInput;
extern int keyboardCursor;
extern bool keyboardShift, keyboardNumbers;
extern bool keyboardNeedsRedraw;
extern int keyboardSelectedRow, keyboardSelectedCol;
extern String keyboardKeyRows[5];
extern int keyboardRowLengths[5];

extern int menuFirstVisible;
extern const int menuMaxVisible;
extern int menuScrollOffset;
extern int bootMenuFirstVisible;
extern const int bootMenuMaxVisible;

extern bool showFileManager;
extern String fileList[20];
extern int fileCount;
extern int selectedFile;
extern int fmHoverIndex;
extern int fileManagerFirstVisible;
extern bool fileManagerNeedsRedraw;
extern String currentFilePath;
extern bool editingFile;
extern String fileContent;
extern String editingFilePath;
extern bool showFileMenu;
extern int fmMenuX, fmMenuY;
extern int fmMenuSelection;
extern bool showFileHelp;
extern bool fileMetadataVisible;
extern String fileMenuAction;
extern String fileSourcePath;
extern String renamingFileName;
extern String renamingInput;
extern bool renamingActive;

#define MAX_SCRIPT_VARS 128
extern volatile bool runningScript;
extern ScriptValue* scriptVariables;
extern String* scriptVarNames;
extern int scriptVarCount;
extern String scriptKeyBuffer;
extern volatile bool scriptKeyAvailable;

// ── TURBO Hibrit Register Dosyası (DRAM + PSRAM) ──────────────
// Mimari: "Hücum Kıtası" Hibrit Bellek Stratejisi
//   R0  -  R7  → DRAM (iç RAM)  : Döngü sayaçları, koordinatlar, sabitler  ⚡⚡
//   R8  - R1023 → EXT_RAM_ATTR PSRAM : Renk, hedef koord, parçacık durumu    🗄️
//
// ⚠️ NEDEN IRAM_ATTR DEĞİL: ScriptValue.sVal (String) byte-level heap erişim
//    yapıyor. ESP32-S3 IRAM sadece 32-bit hizalı erişimi destekler.
//    IRAM_ATTR + String = LoadStoreError (EXCCAUSE:3) crash!
//    DRAM (iç RAM) PSRAM'dan 5-10x hızlı, güvenli, yeter.
//
// Erişim: vmReg(idx) — index'e göre otomatik yönlendirme
// Pointer Karışıklığı Riski: SIFIR — tek erişim noktası vmReg(idx)
#define VM_REGISTER_COUNT   1024
#define VM_SRAM_COUNT          64    // R0-R63  : DRAM "Altın Rezerv" (hızlı, güvenli) ⚡
#define VM_PSRAM_COUNT        960    // R64-R1023 : PSRAM "Mühimmat Depo"

// ── DRAM Register Bankası (R0-R63) ─────────────────────────────
// NO attribute → ESP32-S3 iç DRAM'inde kalır (String byte erişimi OK)
extern ScriptValue vmSramRegs[VM_SRAM_COUNT];

// ── PSRAM Register Bankası (R64-R1023) ─────────────────────────
// EXT_RAM_ATTR → SPIRAM (PSRAM) üzerinde yer alır
extern ScriptValue* vmPsramRegs;

// ── vmReg(idx) — Tek Güvenli Erişim Noktası ───────────────────
// R0-R63    → vmSramRegs[idx]          (DRAM, PSRAM'dan 5-10x hızlı)
// R64-R1023  → vmPsramRegs[idx - 64]    (PSRAM, yavaş ama büyük)
// "Mayınlı Bölge" koruması: bounds check her zaman yapılır
inline ScriptValue& vmReg(int idx) {
  if (idx < 0) idx = 0;
  if (idx < VM_SRAM_COUNT)     return vmSramRegs[idx];
  if (idx < VM_REGISTER_COUNT) return vmPsramRegs[idx - VM_SRAM_COUNT];
  return vmSramRegs[0]; // Güvenli fallback
}

void vmGSOD(const String& reason, int line); // Yeşil Ölüm Ekranı

// Key-Up / Key-Down durum takibi (HID keycode 0x00–0xFF)
// InputManager tarafından güncellenir, EyuScript tarafından okunur
extern volatile bool heldKeys[256];          // heldKeys[hid] = true → o tuş şu an basılı
extern volatile uint8_t lastKeyDownHID;      // En son basılan HID kodu
extern volatile uint8_t lastKeyUpHID;        // En son bırakılan HID kodu
extern volatile bool    keyEventAvailable;   // Yeni bir KD/KU olayı geldi mi?
extern uint8_t* wallpaperBuffer;             // PSRAM wallpaper cache (400x300)
extern bool wallpaperBufferLoaded;
extern volatile bool bmpPreviewActive;
extern volatile bool systemInErrorState;
extern String gsodTitle;
extern String gsodMessage;
extern volatile bool gsodPending;

// ── Input Driver Handshake ──────────────────────────────────
extern int currentInputDriver;              // 0: Normal, 1: Controllers, 2: BT Dongle
extern int analogStickPreference;           // 0: Auto/Left with Right Backup, 1: Force Right, 2: Disabled
extern bool inputDriverVerified;            // Handshake completed?
extern bool inputDriverWaitingForFlash;     // True when waiting for new sketch flash
extern unsigned long lastDriverStatusSend;  // Periodic status transmission tracking
void drawInputDriverWaitingScreen();

// ── Multi-player Gamepad States ──────────────────────────────
extern bool gamepadButtons[2][14];
extern int16_t gamepadAnalog[2][4];

extern JsonDocument lastJson;

#define MAX_SCRIPT_LINES 400
#define MAX_SCRIPT_LINE_LEN 256
#define MAX_LABELS       40
#define STRING_POOL_SIZE 8192
// Moving script lines to PSRAM to save internal RAM
extern char (*scriptLines)[MAX_SCRIPT_LINE_LEN];
extern Instruction* scriptProgram;
extern char* scriptStringPool;
extern int scriptStringPoolPtr;

extern int scriptLineCount;
extern int scriptCurrentLine;
extern int lineNumber;
extern ScriptLabel* scriptLabels;
extern int scriptLabelCount;
extern int scriptErrorLine;

#define MAX_FUNCTIONS    32
#define MAX_EVENTS       8
extern ScriptFunction* scriptFunctions;
extern int scriptFunctionCount;
extern ScriptEvent scriptEvents[MAX_EVENTS];
extern int scriptEventCount;

const char* getPooledString(int offset);
int addToStringPool(const char* s);

// ── Stack-based GOSUB (8 seviye) ──────────────────────────
#define MAX_GOSUB_DEPTH  8
extern int gosubStack[MAX_GOSUB_DEPTH];
extern int gosubTop;


#define MAX_LOOP_DEPTH 8
extern LoopInfo loopStack[MAX_LOOP_DEPTH];
extern int loopStackTop;

extern bool showEsdos;
extern String esdosCommand;
extern int esdosCursorPos;
extern String esdosOutput[15];
extern int esdosOutputCount;
extern String esdosCurrentPath;
void addEsdosOutput(String line);
extern bool vmDebugActive;
extern bool vmShowFps;
extern int esdosHistoryIndex;
extern String esdosHistory[10];
extern int esdosHistoryCount;
extern bool esdosNeedsRedraw;
extern int displayAppSelection; // For keyboard support in Display app
extern String lastPingResult;

extern bool wifiScanning;
extern String wifiNetworks[10];
extern int wifiNetworkCount;
extern int selectedNetwork;
extern bool showNetworkList;

extern uint32_t freeHeap, minFreeHeap, maxAllocHeap, totalHeap;
extern uint32_t freePsram, totalPsram, cpuFreq, uptime;
extern uint32_t flashSize, flashSpeed, flashMode, chipModel, chipRevision, chipCores, chipFeatures;

extern bool showEyuditor;
extern String eyuditorPath;
extern String selectedWadPath;
#define EYU_MAX_LINES 400
#define EYU_MAX_COLS 80
// Heavy editor buffer moved to PSRAM
extern char (*eyuLines)[EYU_MAX_COLS + 1];
extern int eyuditorLineCount;
extern int eyuditorCursorRow;
extern int eyuditorCursorCol;
extern int eyuditorFirstVisibleRow;
extern bool eyuditorNeedsRedraw;
extern bool eyuditorSaveAsActive;
extern char eyuSaveAsName[EYU_MAX_COLS + 1];

// --- EyuMarket ---
extern String marketScripts[30];
extern String marketDescs[30];
extern String marketUrls[30];
extern String marketFiles[30];
extern int marketScriptCount;
extern int marketHoverIdx;
extern int marketSelection;
extern int marketScrollOffset;

// Task Handles
extern TaskHandle_t scriptTaskHandle;
extern TaskHandle_t systemUpdateTaskHandle;

// Background script tasks
extern BgTask bgTasks[MAX_BG_TASKS];
extern QueueHandle_t ipcQueue;   // bg → fg IPC (IpcMsg)

extern VirtualKeyboardCallback vkbCallback;
extern String vkbTitle;
extern String vkbInitialText;
extern bool vkbActive;
extern String vkbCallbackType;
extern String vkbCallbackParam;

// --- Eyudio System App ---
struct EyudioRoom {
    String roomId;
    String roomName;
    String roomType; // "notes", "private", "group"
    String peerUid;
    String peerIp;
    int unreadCount;
    String lastMessagePreview;
    uint32_t lastTimestamp;
};

extern bool showEyudio;
extern int eyudioActiveTab; // 0: Sohbet & Odalar, 1: Eşleşme (QR)
extern bool eyudioFullscreenQR;
extern String eyudioSelectedChatId;
extern String eyudioReplyToPostId;
extern String eyudioEditingPostId;
extern String eyudioPeerIP;
extern bool eyudioIsTransferring;
extern int eyudioTransferProgress;
extern String eyudioTransferStatus;

extern std::vector<EyudioRoom> eyudioRooms;
extern int eyudioActiveRoomIndex;
void loadSessionRooms();





// ============================================================
// === FUNCTION PROTOYPES ===
// ============================================================
// Display
void setStatusLED(uint32_t color);
void turnOffStatusLED();
void blinkStatusLED(uint32_t color, int times, int delayMs);
void showStartupPhase(const char* phaseName, uint32_t color);
void drawRect(int x, int y, int width, int height, uint16_t color);
void drawCursor();
void clearCursorArea();

// Input
void initUSBHost();
void processUSBInput();
void onMouseEvent(uint8_t buttons, int8_t x, int8_t y, int8_t wheel, int8_t horiz);
void onKeyboardEvent(uint8_t modifiers, uint8_t key_code);
void processKeyboardInput(String line);
ParsedKey parseKey(String rawKey);
void handleMouseClick();
void processSerialInput(String line);

// WiFi
void loadWiFiCredentials();
void saveWiFiCredentials();
void clearWiFiCredentials();
void connectToWiFi();
void scanWiFiNetworks();
void testWiFi();
void addEsdosOutput(String line); // main.cpp'den erisim icin

// Menu
void enterMenu(String menuName);
void exitMenu();
void handleMenuInput(ParsedKey key, String originalLine);
void handleBootMenuInput(ParsedKey key);
void handleMenuSelection();

// Desktop
void initializeDesktop();
void renderDesktop();
void handleDesktopInput(ParsedKey key);
void openApplication(String appName);
void calculateDesktopIndices();

// Window Manager
void createWindow(String appName, String title, int x, int y, int w, int h);
void closeWindow(int index);
void focusWindow(int index);
void drawWindowDecoration(int index);
int getWindowAt(int x, int y);
void moveWindow(int index, int dx, int dy);
void cycleFocus();

// System Logs
void addSystemLog(String message);
void toggleSystemLogs();
void drawSystemLogs();
void handleSystemLogsInput(ParsedKey key);

// File Manager
void openFileManager();
void scanFiles();
void editFile(String fileName);
void saveFile(String filePath);
void showFileMetadata(String fileName);
void deleteFile(String fileName);
void renameFile(String oldName, String newName);
void copyFile(String sourceName, String destPath);
void moveFile(String sourceName, String destPath);
void handleFileManagerInputKey(ParsedKey key);

// Esdos
void executeEsdosCommand(String cmd);
void addEsdosOutput(String line);
void handleEsdosInputKey(ParsedKey key);

// EyuMarket
void drawMarket();
void scanMarketScripts();
void handleMarketClick(int x, int y);
void handleMarketInput(ParsedKey key);

// Eyuditor
void openEyuditor(String fullPath);
void saveEyuditor();
void handleEyuditorInputKey(ParsedKey key);


// Virtual Keyboard
void showVirtualKeyboardWithCallback(String title, String initialText, VirtualKeyboardCallback callback);
void closeVirtualKeyboard();
void handleVirtualKeyboardClick();
void handleVirtualKeyboardPress();
void handleVirtualKeyboardSpecialKey(int keyIndex);
void handleVirtualKeyboardInput(ParsedKey key);
void handleVirtualKeyboardCallback(String result);

// Rendering
void renderBootMenu();
void renderMenu();
void renderApplication();
void drawSplashScreen();
void drawBootMenu();
void drawMenu();
void drawVirtualKeyboard();
void drawSystemMonitor();
void drawTaskManager();
extern int taskManagerTabGlobal; // 0=FreeRTOS 1=BG Scripts
void drawSettings();
void drawFileManager();
void drawDisplaySettings();

void drawServices();
void drawFileMenu();
void drawFileHelp();
void drawNetworkTools();
void drawAbout();
void drawEsdos();
void drawEyuditor();
void drawAppMenu();
void drawCurrentApplication();
void postRender();

// Controls
void changeResolution(int modeIndex, bool doubleBuffer);
void toggleWifiService(bool on);
void toggleBluetoothService(bool on);
extern int currentResolutionIndex;
extern bool useDoubleBuffering;
extern bool wifiServiceActive;
extern bool btServiceActive;

// Utility
void logToFile(String message);
void updateSystemInfo();
void retrySDCard();

// Boot Log Display
void updateBootStatus(String status);
void showSystemError(String title, String message);
void pingHost(String host); // Real network test
void handleDisplayInputKey(ParsedKey key); // Display app keyboard handler

// Services
void toggleBluetoothService(bool on);
void toggleWifiService(bool on);

// Bluetooth HID Host
void initBluetoothHost();
void loadBluetoothDevices();
void clearBluetoothDevices();
void pairNewBluetoothDevice();

// Background Script Engine
int  runBgScript(const String& filePath);   // returns slot index or -1
void stopBgScript(int slot);

// Doom Engine
#ifdef __cplusplus
extern "C" {
#endif
extern volatile bool showDoom;
extern bool doomWadLoaded;
extern volatile bool doomFrameReady;
extern uint8_t* doomFrameBuffer;
extern volatile int doomLoadingProgress;
extern char doomLoadingText[64];
extern volatile bool doomLoadingActive;
#ifdef __cplusplus
}
#endif
void openDoom(String wadPath = "");
void closeDoom();
void renderDoomFrame();
void handleDoomInput(ParsedKey key);

#endif

void allocatePSRAMGlobals();
