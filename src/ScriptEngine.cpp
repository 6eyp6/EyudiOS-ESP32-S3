// ============================================================
// ScriptEngine.cpp — EyuScript 3.0 TURBO (Bytecode VM)
// ============================================================
#include "Globals.h"
#include <esp_task_wdt.h>
#include <HTTPClient.h>
#include <math.h>
#include <esp_task_wdt.h>
#include <esp_heap_caps.h>  // heap_caps_malloc (MALLOC_CAP_SPIRAM / MALLOC_CAP_INTERNAL)

static String globalScriptPath;
static uint8_t* vgaScanlineBuffer = nullptr;
static size_t vgaScanlineBufferSize = 0;
static bool holdingVgaLock = false; // Tracks if script task holds systemMutex

// (EyuTurbo stack artık adaptif xTaskCreatePinnedToCore ile iç DRAM'da yönetilir)

// ── TURBO Hibrit Register Bankası (DRAM + PSRAM) ───────────────
// Globals.h'deki vmReg(idx) inline fonks. üzerinden erişilir.
// ⚠️ IRAM_ATTR YASAK: ScriptValue.sVal (String) byte-level erişim yapar.
//    ESP32-S3 IRAM = yalnızca 32-bit hizalı erişim → IRAM+String = LoadStoreError!
//    ÇÖZÜM: vmSramRegs → düz DRAM (attribute YOK), PSRAM'dan 5-10x hızlı ✅
ScriptValue              vmSramRegs [VM_SRAM_COUNT];   // R0-R63  : DRAM
// vmPsramRegs is now dynamically allocated in Globals.cpp

// ── Array Cache (Hot Loop Optimization) ──────────────────
static ScriptArray* lastArrayCache = nullptr;
static char lastArrayCacheName[24] = "";

ScriptArray* findArrayCached(const char* name) {
  if (!name) return nullptr;
  if (lastArrayCache && strcmp(lastArrayCache->name, name) == 0) return lastArrayCache;
  lastArrayCache = findArray(name);
  if (lastArrayCache) strncpy(lastArrayCacheName, name, 23);
  return lastArrayCache;
}

// ── TURBO Math Pool Pool (Pointer tabanlı) ───────────────────
#define VM_MATH_RECURSION 12
#define VM_MATH_STACK_SIZE 16
static ScriptValue* vmValuePool = nullptr;
static char*        vmOpPool    = nullptr;


// Helper: Bir string'in sayı veya register (R0-R1023) olup olmadığını kontrol eder
static bool isActuallyNum(const String& s) {
  if (s.length() == 0) return false;
  String t = s; t.trim();
  if (t.length() == 0) return false;
  if (isdigit(t[0]) || t[0] == '-' || t[0] == '.') return true;
  if (t.length() >= 2 && (t[0] == 'R' || t[0] == 'r') && isdigit(t[1])) return true;
  return false;
}

// ── GSOD entegrasyonu — mevcut showSystemError'a yönlendirme ──────
void vmGSOD(const String& reason, int line) {
  gsodTitle = "EyuScript VM Hatasi";
  gsodMessage = reason + "\nSatir: " + String(line);
  gsodPending = true;
  systemInErrorState = true;
  runningScript = false;

  // Eyer cagiran task EyuTurbo ise hemen kendini silerek kaynaklari bosalt
  if (scriptTaskHandle && xTaskGetCurrentTaskHandle() == scriptTaskHandle) {
    setStatusLED(EYU_LED_RED);
    scriptTaskHandle = nullptr;
    vTaskDelete(NULL);
  }
}

// ── Forward declarations (resolveParam için gerekli) ─────────────────
// Bu fonksiyonlar dosyanın ilerleyen bölümlerinde tanımlanıyor;
// resolveParam onlardan önce geldiği için önceden bildirmeliyiz.
static String expandVars(const String& s);
ScriptValue evaluateValueToSV(const String& expr, int recursionDepth);

// ── resolveParam — PTAG-aware evrensel parametre çözücü ─────────────
// Bir Instruction parametresinin tag bit'ine bakarak:
//   PTAG_POOL  → string pool'dan okuyup evaluateValueToSV çağır
//   PTAG_INT   → 29-bit sign-extend int, string pool'a hiç gitme  ⚡
//   PTAG_REG   → vmRegisters[idx] doğrudan döndür            ⚡⚡
//   p == -1    → boş ("parametre yok" sentinel)
static ScriptValue resolveParam(int32_t p) {
  if (p == -1) return ScriptValue();
  uint32_t up = (uint32_t)p;
  uint32_t tag = up & 0xE0000000u;
  if (tag == PTAG_INT) {
    int32_t raw = p & 0x1FFFFFFF;
    if (raw & (1 << 28)) raw |= (int32_t)0xE0000000;
    return ScriptValue(raw);
  }
  if (tag == PTAG_REG) {
    int ridx = p & 0x3FF;
    if (ridx >= 0 && ridx < VM_REGISTER_COUNT) return vmReg(ridx);
    return ScriptValue();
  }
  
  const char* rawC = getPooledString(p);
  if (!rawC) return ScriptValue();
  
  // ⚡ AGRESİF BYPASS: Eğer ifade saf bir matematiksel blok ise { }
  // String birleştirme hatasını (1124) önlemek için expandVars'ı baypas et!
  size_t rLen = strlen(rawC);
  if (rLen >= 2 && rawC[0] == '{' && rawC[rLen-1] == '}') {
     String content = String(rawC).substring(1, rLen-1);
     return evaluateValueToSV(content, 0);
  }

  ScriptValue res;
  if (strchr(rawC, '{') == NULL) res = evaluateValueToSV(rawC, 0);
  else res = evaluateValueToSV(expandVars(rawC), 0);

  // 🔥 DAHİ MOTOR PROTOKOLÜ (Sayıya Çökme): 
  // Eğer sonuç sinsi bir string ise ("1124") ama aslında sayısal bir değerse, 
  // sisteme register hatası verdirtmeden önce onu sayıya zorla!
  if (res.type == VAL_STRING && isActuallyNum(res.sVal)) {
      res.setFloat(atof(res.sVal.c_str())); 
  }
  return res;
}


void vgaCleanupScanline() { if(vgaScanlineBuffer) { free(vgaScanlineBuffer); vgaScanlineBuffer=nullptr; vgaScanlineBufferSize=0; } }

// --- Hardware Interrupt Bridge ---
void IRAM_ATTR gpio_isr_handler(void* arg) {
  int idx = (int)((uint32_t)arg);
  if (idx >= 0 && idx < 4) {
    registeredInterrupts[idx].triggered = true;
    if (scriptTaskHandle) vTaskNotifyGiveFromISR(scriptTaskHandle, NULL);
  }
}

// ─────────────────────────────────────────────
// Variable helpers — Optimized for Performance & Health
// ─────────────────────────────────────────────
bool isNumberLiteral(const String& s) {
  if (s.length() == 0) return false;
  char c = s[0];
  if (!isdigit(c) && c != '.' && c != '-') return false;
  for(int i=1; i<(int)s.length(); i++) {
    if(!isdigit(s[i]) && s[i] != '.') return false;
  }
  return true;
}

// Caller must pass already-lowercased, trimmed name for performance
int getVariableId(const String& name, bool createIfMissing) {
  for (int i = 0; i < scriptVarCount; i++) {
    if (scriptVarNames[i] == name) return i;
  }
  if (createIfMissing && scriptVarCount < MAX_SCRIPT_VARS) {
    scriptVarNames[scriptVarCount] = name;
    scriptVariables[scriptVarCount] = ScriptValue();
    return scriptVarCount++;
  }
  return -1;
}

void setScriptVariable(int id, const ScriptValue& value) {
  if (id >= 0 && id < MAX_SCRIPT_VARS) {
    scriptVariables[id] = value;
  }
}

void setScriptVariableByName(const String& name, const String& value) {
  // Stack buf avoids heap allocation — hot path optimization
  char nBuf[32]; int ni = 0;
  const char* src = name.c_str();
  while (*src == ' ') src++;
  while (*src && ni < 31) { nBuf[ni++] = tolower((unsigned char)*src++); }
  while (ni > 0 && nBuf[ni-1] == ' ') ni--;
  nBuf[ni] = 0;
  String n(nBuf); // for getVariableId (already lowercased)
  if (vmDebugActive) Serial.printf("[VM-VAR] Set '%s' = '%s'\n", nBuf, value.c_str());
  int id = getVariableId(n, true);
  if (id != -1) {
    ScriptValue sv;
    String valStr = value; valStr.trim();
    if (isNumberLiteral(valStr)) {
      if (valStr.indexOf('.') != -1) sv.setFloat(valStr.toFloat());
      else sv.setInt(valStr.toInt());
    } else {
      sv.setString(valStr);
    }
    scriptVariables[id] = sv;
  }
}

// Script tracking variables provided via Globals.h
// Array helpers use the existing ScriptArray system from Globals.h
void releaseScriptArrays() {
    for (int i = 0; i < scriptArrayCount; i++) {
        memset(scriptArrays[i].name, 0, sizeof(scriptArrays[i].name));
        scriptArrays[i].size = 0;
        scriptArrays[i].width = 0;
        // 🛡️ Dizi Elemanlarını Temizle (Heap Leak engelleyici!)
        for (int j = 0; j < MAX_ARRAY_SIZE; j++) {
            scriptArrays[i].values[j] = ScriptValue();
        }
    }
    scriptArrayCount = 0;
}

ScriptValue getScriptVariable(int id) {
  if (id >= 0 && id < MAX_SCRIPT_VARS) return scriptVariables[id];
  return ScriptValue();
}

ScriptValue getScriptVariableByName(const String& name) {
  const char* nm = name.c_str();
  // FAST SCAN
  for (int i = 0; i < scriptVarCount; i++) {
    if (strcmp(scriptVarNames[i].c_str(), nm) == 0) return scriptVariables[i];
  }
  return ScriptValue();
}
void addScriptLabel(const String& name, int line) {
  String n = name; n.trim(); n.toLowerCase();
  if (scriptLabelCount < MAX_LABELS) {
    scriptLabels[scriptLabelCount].name       = n;
    scriptLabels[scriptLabelCount].lineNumber  = line;
    scriptLabelCount++;
  }
}
int findScriptLabel(const String& name) {
  String n = name; n.trim(); n.toLowerCase();
  if (n.startsWith(":")) n = n.substring(1);
  for (int i = 0; i < scriptLabelCount; i++)
    if (scriptLabels[i].name == n) return scriptLabels[i].lineNumber;
  return -1;
}


bool manualPing(const char* host) {
  WiFiClient client; client.setTimeout(1000);
  if (client.connect(host, 80)) { client.stop(); return true; }
  return false;
}

void renderBMP(const char* filename, int x, int y, bool internalCall) {
  if (!sdCardPresent || !filename) return;
  
  if (xSemaphoreTake(sdMutex, pdMS_TO_TICKS(2000)) != pdTRUE) return;

  File f = SD.open(filename);
  if (!f) {
    xSemaphoreGive(sdMutex);
    Serial.printf("[VM-ERR] BMP Not Found: %s\n", filename);
    return;
  }
  
  // Header Parse
  f.seek(10); int32_t offset; f.read((uint8_t*)&offset, 4);
  f.seek(18); int32_t w, h; f.read((uint8_t*)&w, 4); f.read((uint8_t*)&h, 4);
  f.seek(28); uint16_t bpp; f.read((uint8_t*)&bpp, 2);
  
  Serial.printf("[BMP] Loading: %s (%dx%d, %dbpp)\n", filename, w, h, bpp);

  if (bpp != 24 && bpp != 32) {
    Serial.printf("[VM-ERR] Unsupported BPP: %d\n", bpp);
    f.close(); xSemaphoreGive(sdMutex); return;
  }

  f.seek(offset); 
  int absH = abs(h), absW = abs(w);
  size_t bytesPerPixel = bpp / 8;
  size_t rowSize = (absW * bytesPerPixel + 3) & ~3;
  
  if (vgaScanlineBufferSize < rowSize || !vgaScanlineBuffer) {
    uint8_t* nb = (uint8_t*)realloc(vgaScanlineBuffer, rowSize);
    if (!nb) { f.close(); xSemaphoreGive(sdMutex); return; }
    vgaScanlineBuffer = nb; vgaScanlineBufferSize = rowSize;
  }

  if (!internalCall && systemMutex) xSemaphoreTake(systemMutex, portMAX_DELAY);
  
  for (int j = 0; j < absH; j++) {
    if (f.read(vgaScanlineBuffer, rowSize) != rowSize) break;
    int py = (h > 0) ? (y + h - 1 - j) : (y + j);
    
    if (py >= 0 && py < SCREEN_HEIGHT) {
      for (int i = 0; i < absW; i++) {
        int px = x + i;
        if (px >= 0 && px < SCREEN_WIDTH) {
          uint8_t b = vgaScanlineBuffer[i*bytesPerPixel];
          uint8_t g = vgaScanlineBuffer[i*bytesPerPixel + 1];
          uint8_t r = vgaScanlineBuffer[i*bytesPerPixel + 2];
          // 32-bit ise 4. byte (Alpha) atlanır
          videodisplay.drawPixel(px, py, videodisplay.RGB(r, g, b));
        }
      }
    }
    
    if (j % 16 == 0) {
      yield();
      if (systemMutex) {
        xSemaphoreGive(systemMutex); vTaskDelay(1); xSemaphoreTake(systemMutex, portMAX_DELAY);
      } else { vTaskDelay(1); }
    }
  }

  if (!internalCall && systemMutex) xSemaphoreGive(systemMutex);
  f.close();
  xSemaphoreGive(sdMutex);
}

// Helper to avoid recursion for plain numbers
// isNumberLiteral moved to the top

void setScriptValue(int id, const String& val) {
  if (id < 0 || id >= MAX_SCRIPT_VARS) return;
  ScriptValue sv;
  if (isNumberLiteral(val)) {
    if (val.indexOf('.') != -1) sv.setFloat(val.toFloat());
    else sv.setInt(val.toInt());
  } else {
    sv.setString(val);
  }
  scriptVariables[id] = sv;
}

// ─────────────────────────────────────────────
// Shunting-Yard math engine
// Desteklenen: + - * / % operatörleri, parantezler,
// değişkenler, sayılar, FLOOR(), CEIL(), ABS() çağrıları
// ─────────────────────────────────────────────
static int opPrec(char op) {
  if (op == '+' || op == '-') return 1;
  if (op == '*' || op == '/' || op == '%') return 2;
  return 0;
}

// ─────────────────────────────────────────────
static bool scriptHasError = false;
static String scriptErrorMsg = "";

void reportError(const String& msg) {
  scriptHasError = true;
  scriptErrorMsg = msg;
  logToFile("[VM-FATAL] " + msg + " at L" + String(lineNumber));
  desktopMessage = "ERROR: " + msg;
}

// ─────────────────────────────────────────────
// Evaluation helpers — Native Variant Arithmetic
// ─────────────────────────────────────────────
// Forward declaration needed because evaluateValueToSV calls expandVars
static String expandVars(const String& s);

ScriptValue evaluateValueToSV(const char* nm, int len, int recursionDepth) {
    if (recursionDepth > 8 || !nm || len <= 0) return ScriptValue();
    
    // 1. FAST VARIABLE LOOKUP
    for (int k = 0; k < scriptVarCount; k++) {
        const char* vname = scriptVarNames[k].c_str();
        if (vname[0] == nm[0] && (int)strlen(vname) == len && strncmp(vname, nm, len) == 0) {
            ScriptValue v = scriptVariables[k];
            if (v.type == VAL_STRING) {
                const char* s = v.sVal.c_str(); 
                if (isdigit(s[0]) || s[0] == '-') {
                   if (strchr(s, '.')) return ScriptValue(atof(s));
                   else return ScriptValue((int)atol(s));
                }
                if (recursionDepth < 4) {
                   ScriptValue sub = evaluateValueToSV(s, recursionDepth+1);
                   if (sub.type != VAL_NULL) return sub;
                }
            }
            return v;
        }
    }

    // 2. REGISTER ACCESS (e.g., R3, r100)
    if (len >= 2 && (nm[0] == 'R' || nm[0] == 'r') && isdigit(nm[1])) {
       int ridx = atoi(nm + 1);
       if (ridx >= 0 && ridx < VM_REGISTER_COUNT) return vmReg(ridx);
    }

    // 3. SYSTEM CONSTANTS
    if (len == 2 && nm[0] == 's') {
      if (nm[1] == 'w') return ScriptValue(SCREEN_WIDTH);
      if (nm[1] == 'h') return ScriptValue(SCREEN_HEIGHT);
    }
    if (len == 3 && nm[0] == 'f' && nm[1] == 'p' && nm[2] == 's') {
      static unsigned long lastF = 0; static int fCount = 0; static int lFps = 60;
      fCount++;
      if(millis() - lastF >= 1000) { lFps = fCount; fCount = 0; lastF = millis(); }
      return ScriptValue(lFps);
    }
    if (nm[0] == 'm') {
      if (len == 6 && strncmp(nm, "mouseX", 6) == 0) return ScriptValue(mouseX);
      if (len == 6 && strncmp(nm, "mouseY", 6) == 0) return ScriptValue(mouseY);
      if (len == 6 && strncmp(nm, "millis", 6) == 0) return ScriptValue((int)millis());
    }

    // 3. LITERALS
    if (isdigit(nm[0]) || nm[0] == '-') {
      char buf[34]; if (len > 32) return ScriptValue();
      strncpy(buf, nm, len); buf[len] = 0;
      if (strchr(buf, '.')) return ScriptValue(atof(buf));
      else return ScriptValue((int)atol(buf));
    }
    return ScriptValue();
}

ScriptValue evaluateValueToSV(const char* nm, int recursionDepth) {
  return evaluateValueToSV(nm, (int)strlen(nm), recursionDepth);
}

ScriptValue evaluateValueToSV(const String& expr, int recursionDepth) {
  if (recursionDepth > 10) return ScriptValue();
  String ex = expr; ex.trim();
  if (ex.length() == 0) return ScriptValue();

  if (ex.indexOf('{') != -1) {
    String expanded = expandVars(ex);
    return evaluateValueToSV(expanded, recursionDepth + 1);
  }

  if (ex.length() >= 2 && ex.startsWith("\"") && ex.endsWith("\"")) {
    ScriptValue sv; sv.setString(ex.substring(1, ex.length() - 1));
    return sv;
  }

  bool hasOp = false;
  int depthChk = 0;
  for (int k = 0; k < (int)ex.length(); k++) {
    char c = ex[k];
    if (c == '(') depthChk++;
    else if (c == ')') depthChk--;
    else if (depthChk == 0 && (c == '+' || c == '-' || c == '*' || c == '/' || c == '%')) { hasOp = true; break; }
  }

  if (!hasOp) return evaluateValueToSV(ex.c_str(), recursionDepth);

  // PSRAM Pool Access: Stack yerine PSRAM kullan — DRAM tasarrufu + Derinlik ✅
  int stackOffset = recursionDepth * VM_MATH_STACK_SIZE;
  ScriptValue* numStack = &vmValuePool[stackOffset];
  char*        opStack  = &vmOpPool[stackOffset];
  int numTop = -1;
  int opTop  = -1;

  // Cleanup strings from previous usage if any (RAII wouldn't work on PSRAM raw buffer)
  for (int k = 0; k < VM_MATH_STACK_SIZE; k++) {
    numStack[k] = ScriptValue(); // Clear string refs
    opStack[k] = 0;
  }

  auto applyOp = [&]() {
    if (numTop < 1 || opTop < 0) {
      if (opTop >= 0) opTop--; // Prevent infinite loop on invalid syntax / double operators
      return;
    }
    ScriptValue b = numStack[numTop--];
    ScriptValue a = numStack[numTop--];
    char  o = opStack[opTop--];
    ScriptValue r;

    // Sayısal Öncelik Mutlak: Turbo motoru kelime birleştirmez!
    float f1 = a.toFloat(), f2 = b.toFloat();
    
    if (o == '+') r.setFloat(f1 + f2);
    else if (o == '-') r.setFloat(f1 - f2);
    else if (o == '*') r.setFloat(f1 * f2);
    else if (o == '/') r.setFloat(f2 != 0 ? f1 / f2 : 0);
    else if (o == '%') r.setInt((int)f1 % (int)(f2 != 0 ? f2 : 1));

    if (numTop < 15) numStack[++numTop] = r;
    else reportError("Math Stack Overflow");
  };

  const char* base = ex.c_str();
  int len = ex.length();
  int i = 0;
  while (i < len) {
    char c = base[i];
    if (isspace(c)) { i++; continue; }

    if (c == '(') {
      int d2 = 1; int j = i + 1;
      while (j < len && d2 > 0) {
        if (base[j] == '(') d2++; else if (base[j] == ')') d2--;
        j++;
      }
      ScriptValue v = evaluateValueToSV(ex.substring(i+1, j-1), recursionDepth+1);
      if (numTop < 15) numStack[++numTop] = v;
      else reportError("Math Stack Overflow");
      i = j; continue;
    }

    if ((c == '+' || c == '-' || c == '*' || c == '/' || c == '%') &&
        !(c == '-' && numTop < 0)) {
      while (opTop >= 0 && opStack[opTop] != '(' && opPrec(opStack[opTop]) >= opPrec(c))
        applyOp();
      if (opTop < 15) opStack[++opTop] = c;
      i++; continue;
    }

    if (c == '-' && numTop < 0) {
      i++; int j = i;
      while (j < len && !isspace(base[j]) && !strchr("+-*/%()", base[j])) j++;
      ScriptValue v = evaluateValueToSV(base + i, j - i, recursionDepth+1);
      ScriptValue rv;
      if (v.type == VAL_FLOAT) rv.setFloat(-v.data.fVal);
      else rv.setInt(-v.data.iVal);
      if (numTop < 15) numStack[++numTop] = rv;
      else reportError("Math Stack Overflow");
      i = j; continue;
    }

    int j = i;
    while (j < len && !isspace(base[j]) && !strchr("+-*/%()", base[j])) j++;
    if (j == i) { i++; continue; }
    ScriptValue v = evaluateValueToSV(base + i, j - i, recursionDepth+1);
    if (numTop < 15) numStack[++numTop] = v;
    else reportError("Math Stack Overflow");
    i = j;
  }

  while (opTop >= 0) applyOp();
  ScriptValue finalRes = (numTop >= 0) ? numStack[numTop] : ScriptValue();
  
  // Cleanup strings before exit to prevent leaks in shared PSRAM buffer
  for (int k = 0; k < VM_MATH_STACK_SIZE; k++) numStack[k] = ScriptValue();
  
  return finalRes;
}

String evaluateValue(const String& expr) { return evaluateValueToSV(expr, 0).toString(); }

static String expandVars(const String& s) {
  String ex = s;
  if (ex.length() >= 2 && ex.startsWith("\"") && ex.endsWith("\"")) ex = ex.substring(1, ex.length() - 1);
  if (ex.indexOf('{') == -1) return ex;
  String result; result.reserve(ex.length() + 64);
  int lastPos = 0, open;
  while ((open = ex.indexOf('{', lastPos)) != -1) {
    result += ex.substring(lastPos, open);
    int close = ex.indexOf('}', open);
    if (close == -1) { lastPos = open; break; }
    String content = ex.substring(open + 1, close);
    content.trim();
    result += evaluateValue(content);
    lastPos = close + 1;
  }
  result += ex.substring(lastPos);
  return result;
}

bool compareValues(const String& left, const String& op, const String& right) {
  ScriptValue lSV = evaluateValueToSV(expandVars(left), 0);
  ScriptValue rSV = evaluateValueToSV(expandVars(right), 0);
  
  bool isNum = (lSV.type != VAL_STRING && rSV.type != VAL_STRING) || 
               (isNumberLiteral(lSV.toString()) && isNumberLiteral(rSV.toString()));
               
  bool res = false;
  if (isNum) {
    float lf = lSV.toFloat(), rf = rSV.toFloat();
    // Smart coordinate landing: If both are within 0.1 of an integer, round them
    if (fabs(lf - round(lf)) < 0.1f) lf = round(lf);
    if (fabs(rf - round(rf)) < 0.1f) rf = round(rf);

    if      (op == "==") res = (fabs(lf - rf) < 0.1f);
    else if (op == "!=") res = (fabs(lf - rf) >= 0.1f);
    else if (op == ">")  res = (lf >  rf + 0.1f);
    else if (op == "<")  res = (lf <  rf - 0.1f);
    else if (op == ">=") res = (lf >= rf - 0.1f);
    else if (op == "<=") res = (lf <= rf + 0.1f);
  } else {
    String ls = lSV.toString(), rs = rSV.toString();
    if      (op == "==") res = ls.equalsIgnoreCase(rs);
    else if (op == "!=") res = !ls.equalsIgnoreCase(rs);
    else if (op == ">")  res = (ls >  rs);
    else if (op == "<")  res = (ls <  rs);
    else if (op == ">=") res = (ls >= rs);
    else if (op == "<=") res = (ls <= rs);
  }
  /*
  if (vmDebugActive) {
    Serial.printf("  [COMP] %s %s %s -> %s\n", lSV.toString().c_str(), op.c_str(), rSV.toString().c_str(), res?"TRUE":"FALSE");
  }
  */
  return res;
}

static bool evalSingleCond(const String& cond) {
  String ec = cond;
  if (ec.indexOf('{') != -1) ec = expandVars(ec);
  
  String op;
  int opIdx = -1, opLen = 2;
  if      (ec.indexOf("==") != -1) { op = "=="; opIdx = ec.indexOf("=="); }
  else if (ec.indexOf("!=") != -1) { op = "!="; opIdx = ec.indexOf("!="); }
  else if (ec.indexOf(">=") != -1) { op = ">="; opIdx = ec.indexOf(">="); }
  else if (ec.indexOf("<=") != -1) { op = "<="; opIdx = ec.indexOf("<="); }
  else if (ec.indexOf(">")  != -1) { op = ">";  opIdx = ec.indexOf(">"); opLen = 1; }
  else if (ec.indexOf("<")  != -1) { op = "<";  opIdx = ec.indexOf("<"); opLen = 1; }
  if (opIdx != -1) {
    String left = ec.substring(0, opIdx); left.trim();
    String right = ec.substring(opIdx + opLen); right.trim();
    return compareValues(left, op, right);
  }
  // Koşulsuz: değer 0 değilse true
  return evaluateValue(ec).toFloat() != 0;
}

bool evalCondition(const String& cond) {
  String c = cond; c.trim();

  // || operatörü — en düşük öncelik, soldan sağa tara
  // Parantez içini atla
  int depth = 0;
  for (int i = 0; i < (int)c.length() - 1; i++) {
    if (c[i] == '(') depth++;
    else if (c[i] == ')') depth--;
    else if (depth == 0 && c[i] == '|' && c[i+1] == '|') {
      bool left = evalCondition(c.substring(0, i));
      if (left) return true;  // Short-circuit OR
      return evalCondition(c.substring(i + 2));
    }
  }

  // && operatörü — || 'den yüksek öncelik
  depth = 0;
  for (int i = 0; i < (int)c.length() - 1; i++) {
    if (c[i] == '(') depth++;
    else if (c[i] == ')') depth--;
    else if (depth == 0 && c[i] == '&' && c[i+1] == '&') {
      bool left = evalCondition(c.substring(0, i));
      if (!left) return false; // Short-circuit AND
      return evalCondition(c.substring(i + 2));
    }
  }

  // Tekil koşul
  return evalSingleCond(c);
}

// ─────────────────────────────────────────────
// JSON helpers
// ─────────────────────────────────────────────
void parseJsonDynamic(JsonVariant val, const String& prefix) {
  if (val.is<JsonObject>()) {
    for (JsonPair p : val.as<JsonObject>()) {
      String np = prefix.length() > 0 ? prefix + "." + p.key().c_str() : p.key().c_str();
      parseJsonDynamic(p.value(), np);
    }
  } else if (val.is<JsonArray>()) {
    JsonArray arr = val.as<JsonArray>();
    for (size_t i = 0; i < arr.size(); i++)
      parseJsonDynamic(arr[i], prefix + "[" + String(i) + "]");
  } else {
    String k = prefix; k.toLowerCase(); // Keep dots for struct support
    String v;
    if (val.is<float>()) v = String(val.as<float>(), 2);
    else if (val.is<int>()) v = String(val.as<int>());
    else if (val.is<bool>()) v = val.as<bool>() ? "1" : "0";
    else v = val.as<String>();
    if (k.length() > 0 && v.length() > 0) setScriptVariableByName(k, v);
  }
}

// ─────────────────────────────────────────────
// Compiler (Tokenizer 3.0)
// ─────────────────────────────────────────────
// ── splitToInst ─────────────────────────────────────────────────
// Parametreleri boşluk/virgül'e göre bölüyor.
// Önemli: Köşeli parantezli ifadeler ({nx + 1}) tek token olarak alınıyor.
// Eksik parametreler -1 ile işaretleniyor (0 gerçek bir pool offset olabilir).
static void splitToInst(Instruction& inst, String p, int count, int startIdx = 0) {
  int start = 0, found = startIdx;
  int32_t* outputs[] = { &inst.p1, &inst.p2, &inst.p3, &inst.p4, &inst.p5 };
  for (int j = startIdx; j < 5; j++) *outputs[j] = -1;

  while (found < 5 && found < count + startIdx && start < (int)p.length()) {
    while (start < (int)p.length() && (p[start] == ' ' || p[start] == ',')) start++;
    if (start >= (int)p.length()) break;

    int end = start;
    int braceDepth = 0;
    while (end < (int)p.length()) {
      if (p[end] == '{') braceDepth++;
      else if (p[end] == '}') { braceDepth--; if(braceDepth<0) braceDepth=0; }
      else if (braceDepth == 0 && (p[end] == ' ' || p[end] == ',')) break;
      end++;
    }

    String token = p.substring(start, end);
    token.trim();
    
    // --- TURBO AKILLI AYRISTIRICI ---
    // 1. REGISTER Tespiti: R0, R1... R255
    if (token.length() >= 2 && (token[0] == 'R' || token[0] == 'r') && isdigit(token[1])) {
      int regIdx = token.substring(1).toInt();
      if (regIdx >= 0 && regIdx < VM_REGISTER_COUNT) {
        *outputs[found++] = (int32_t)(PTAG_REG | (regIdx & 0x3FF));
        start = end; continue;
      }
    }

    // 2. TAM SAYI Tespiti: "123", "-45" (Formul icermeyen saf sayilar)
    if (isNumberLiteral(token) && token.indexOf('.') == -1) {
      long val = token.toInt();
      // 29-bit sinirina sığmalı (-268M ile +268M)
      if (val >= -268435456 && val <= 268435455) {
        *outputs[found++] = (int32_t)(PTAG_INT | (val & 0x1FFFFFFF));
        start = end; continue;
      }
    }

    // 3. Fallback: Pool + Evaluate (Degiskenler {x}, formuller, stringler)
    *outputs[found++] = addToStringPool(token.c_str());
    start = end;
  }
}
bool compileProgram() {
  int unknown_ops = 0;
  scriptStringPoolPtr = 0;
  scriptVarCount = 0; // Reset vars for new script
  memset(scriptProgram, 0, sizeof(scriptProgram));
  for (int i = 0; i < scriptLineCount; i++) {
    if (i % 20 == 0) { yield(); esp_task_wdt_reset(); }
    String line = String(scriptLines[i]);
    line.trim();
    // 1. Strip comments
    int hashIdx = line.indexOf('#');
    if (hashIdx != -1) { line = line.substring(0, hashIdx); line.trim(); }
    int slashIdx = line.indexOf("//");
    if (slashIdx != -1) {
      String prefix = line.substring(0, slashIdx);
      prefix.trim();
      if (!prefix.endsWith("http:") && !prefix.endsWith("https:")) {
        line = line.substring(0, slashIdx); 
        line.trim();
      }
    }

    if (line.length() == 0 || line.startsWith(":")) {
      scriptProgram[i].op = OP_NOP; continue;
    }
    
    String lineLower = line;
    lineLower.toLowerCase();
    if (lineLower.startsWith("label ")) {
      scriptProgram[i].op = OP_NOP; continue;
    }

    int spIdx = line.indexOf(' ');
    String cmd = (spIdx == -1) ? line : line.substring(0, spIdx);
    String params = (spIdx == -1) ? "" : line.substring(spIdx + 1);
    cmd.toLowerCase(); cmd.trim(); params.trim();
    Instruction& inst = scriptProgram[i]; inst.op = OP_UNKNOWN;

    // Command Mapping Debug
    if (vmDebugActive && cmd.length() > 0) Serial.printf("[COMP] L%d: %s (params: %s)\n", i+1, cmd.c_str(), params.c_str());

    if      (cmd == "print")    { inst.op = OP_PRINT; inst.p1 = addToStringPool(params.c_str()); }
    else if (cmd == "println")  { inst.op = OP_PRINTLN; inst.p1 = addToStringPool(params.c_str()); }
    else if (cmd == "clear")    { inst.op = OP_CLEAR; }
    else if (cmd == "show")     { inst.op = OP_SHOW; }
    else if (cmd == "getscreen") { 
      inst.op = OP_GETSCREEN; 
      int sp = params.indexOf(' ');
      if(sp != -1) {
        inst.p1 = getVariableId(params.substring(0, sp), true);
        inst.p2 = getVariableId(params.substring(sp+1), true);
      }
    }
    else if (cmd == "setcursor") { inst.op = OP_SETCURSOR; splitToInst(inst, params, 2); }
    else if (cmd == "setcolor")     { inst.op = OP_SETCOLOR;     splitToInst(inst, params, 3); }
    else if (cmd == "setbgcolor")   { inst.op = OP_SETBGCOLOR;   splitToInst(inst, params, 3); }
    else if (cmd == "settextcolor") { inst.op = OP_SETTEXTCOLOR; splitToInst(inst, params, 3); }
    else if (cmd == "func")    {
      inst.op = OP_FUNC; inst.p1 = addToStringPool(params.c_str());
      if (scriptFunctionCount < MAX_FUNCTIONS) {
        scriptFunctions[scriptFunctionCount++] = { params, i }; // Fix: current line index 'i', not scriptLineCount
      }
    }
    else if (cmd == "endfunc") { inst.op = OP_ENDFUNC; }
    else if (cmd == "call")    { inst.op = OP_CALL; inst.p1 = addToStringPool(params.c_str()); }
    
    else if (cmd == "onclick") { params.trim(); inst.op = OP_ONCLICK; inst.p1 = addToStringPool(params.c_str()); }
    else if (cmd == "ontimer") { 
      int sp = params.indexOf(' ');
      if (sp != -1) {
        inst.op = OP_ONTIMER;
        String p1 = params.substring(0, sp); p1.trim();
        inst.p1 = addToStringPool(p1.c_str()); // ms
        String p2 = params.substring(sp + 1); p2.trim();
        inst.p2 = addToStringPool(p2.c_str()); // label
      }
    }
    else if (cmd == "ongpio")  {
      int sp = params.indexOf(' ');
      if (sp != -1) {
        inst.op = OP_ONGPIO;
        String p1 = params.substring(0, sp); p1.trim();
        inst.p1 = addToStringPool(p1.c_str()); // pin
        String p2 = params.substring(sp + 1); p2.trim();
        inst.p2 = addToStringPool(p2.c_str()); // label
      }
    }
    else if (cmd == "event_exit") { inst.op = OP_EVENT_EXIT; }

    else if (cmd == "circle")       { inst.op = OP_CIRCLE; splitToInst(inst, params, 4); }
    else if (cmd == "fillcircle" || cmd == "fcircle")   { inst.op = OP_FILLCIRCLE; splitToInst(inst, params, 4); }
    else if (cmd == "rect" || cmd == "fillrect") { inst.op = OP_RECT; splitToInst(inst, params, 5); }
    else if (cmd == "drawrect" || cmd == "drect")     { inst.op = OP_DRAWRECT; splitToInst(inst, params, 5); }
    else if (cmd == "square")       { inst.op = OP_SQUARE; splitToInst(inst, params, 4); }
    else if (cmd == "line")         { inst.op = OP_LINE; splitToInst(inst, params, 5); }
    else if (cmd == "pixel")        { inst.op = OP_PIXEL; splitToInst(inst, params, 3); }
    else if (cmd == "drawbmp")      { inst.op = OP_DRAWBMP; splitToInst(inst, params, 3); }
    else if (cmd == "settext")      { inst.op = OP_SETTEXT; splitToInst(inst, params, 4); }
    else if (cmd == "progressbar")  { inst.op = OP_PROGRESSBAR; inst.p1 = addToStringPool(params.c_str()); }
    else if (cmd == "button")       { inst.op = OP_BUTTON; inst.p1 = addToStringPool(params.c_str()); }
    
    else if (cmd == "goto")         { inst.op = OP_GOTO; inst.p1 = addToStringPool(params.c_str()); }
    else if (cmd == "gosub")        { inst.op = OP_GOSUB; inst.p1 = addToStringPool(params.c_str()); }
    else if (cmd == "return")       { inst.op = OP_RETURN; }
    
    else if (cmd == "if") { 
      inst.op = OP_IF; 
      String p = params;
      String pL = p; pL.toLowerCase();
      int thenIdx = pL.indexOf("then");
      if (thenIdx != -1) {
          String cond = p.substring(0, thenIdx); cond.trim();
          String action = p.substring(thenIdx + 4); action.trim();
          inst.p1 = addToStringPool(cond.c_str());
          if (action.length() > 0) {
            inst.p2 = -2; // Marker for single-line IF
            inst.p3 = addToStringPool(action.c_str());
          }
      } else {
          inst.p1 = addToStringPool(p.c_str()); 
      }
    }
    else if (cmd == "else")         { inst.op = OP_ELSE; }
    else if (cmd == "elseif" || cmd == "elif") { inst.op = OP_ELSEIF; inst.p1 = addToStringPool(params.c_str()); }
    else if (cmd == "endif")        { inst.op = OP_ENDIF; }
    else if (cmd == "while")        { inst.op = OP_WHILE; inst.p1 = addToStringPool(params.c_str()); }
    else if (cmd == "wend" || cmd == "endwhile") { inst.op = OP_WEND; }
    else if (cmd == "for") {
      inst.op = OP_FOR; 
      // Parse FOR x = start TO end STEP s
      String p = params; String pU = p; pU.toUpperCase();
      int eq = p.indexOf('='); int to = pU.indexOf(" TO ");
      if (eq != -1 && to != -1) {
        String vn = p.substring(0, eq); vn.trim();
        inst.p1 = getVariableId(vn, true);
        
        auto parseVal = [](String s) -> int32_t {
          s.trim();
          if (isNumberLiteral(s) && s.indexOf('.') == -1) {
            long v = s.toInt();
            if (v >= -(1L<<28) && v < (1L<<28)) return (int32_t)(PTAG_INT | (v & 0x1FFFFFFF));
          }
          return addToStringPool(s.c_str());
        };

        inst.p2 = parseVal(p.substring(eq + 1, to));
        
        int stepIdx = pU.indexOf(" STEP ");
        if (stepIdx != -1) {
          inst.p3 = parseVal(p.substring(to + 4, stepIdx)); // end
          inst.p4 = parseVal(p.substring(stepIdx + 6));    // step
        } else {
          inst.p3 = parseVal(p.substring(to + 4)); // end
          inst.p4 = (int32_t)(PTAG_INT | 1); // Varsayılan step = 1
        }
      }
    }
    else if (cmd == "next")         { inst.op = OP_NEXT; }

    else if (cmd == "set" || cmd == "math") {
      // Hem 'set x = 10' hem de 'set x 10' formatlarını destekle
      int eq = params.indexOf('=');
      String vn, valStr;
      if (eq != -1) {
        vn = params.substring(0, eq); vn.trim();
        valStr = params.substring(eq + 1); valStr.trim();
      } else {
        int sp = params.indexOf(' ');
        if (sp != -1) {
          vn = params.substring(0, sp); vn.trim();
          valStr = params.substring(sp + 1); valStr.trim();
        }
      }
      if (vn.length() > 0) {
        inst.op = (cmd == "set") ? OP_SET : OP_MATH;
        inst.p1 = getVariableId(vn, true);
        // Değer bir sayı ise PTAG_INT olarak göm, değilse Pool'a at
        if (isNumberLiteral(valStr) && valStr.indexOf('.') == -1) {
           long v = valStr.toInt();
           if (v >= -268435456 && v <= 268435455) {
             inst.p2 = (int32_t)(PTAG_INT | (v & 0x1FFFFFFF));
           } else inst.p2 = addToStringPool(valStr.c_str());
        } else {
           inst.p2 = addToStringPool(valStr.c_str());
        }
      }
    }
    else if (cmd == "add" || cmd == "sub" || cmd == "mul" || cmd == "div" || cmd == "mod") {
      int s1 = params.indexOf(' ');
      if (s1 != -1) {
        String vn = params.substring(0, s1); vn.trim();
        inst.p1 = getVariableId(vn, true);
        String amtStr = params.substring(s1+1); amtStr.trim();
        inst.p2 = addToStringPool(amtStr.c_str()); // amt
        uint8_t op=OP_ADD;
        if(cmd=="sub")op=OP_SUB;else if(cmd=="mul")op=OP_MUL;else if(cmd=="div")op=OP_DIV;else if(cmd=="mod")op=OP_MOD;
        inst.op=op;
      }
    }
    else if (cmd == "inc") { inst.p1 = getVariableId(params, true); inst.op = OP_INC; }
    else if (cmd == "dec") { inst.p1 = getVariableId(params, true); inst.op = OP_DEC; }
    
    else if (cmd == "seed") { inst.op = OP_SEED; inst.p1 = addToStringPool(params.c_str()); }
    else if (cmd == "sqrt") { inst.op = OP_SQRT; inst.p1 = addToStringPool(params.c_str()); }
    else if (cmd == "pow")  { inst.op = OP_POW;  splitToInst(inst, params, 2); }
    else if (cmd == "sin")  { inst.op = OP_SIN;  inst.p1 = addToStringPool(params.c_str()); }
    else if (cmd == "cos")  { inst.op = OP_COS;  inst.p1 = addToStringPool(params.c_str()); }
    else if (cmd == "tan")  { inst.op = OP_TAN;  inst.p1 = addToStringPool(params.c_str()); }
    else if (cmd == "atan2"){ inst.op = OP_ATAN2;splitToInst(inst, params, 2); }
    
    else if (cmd == "vec2_add") { inst.op = OP_VEC2_ADD; inst.p1 = addToStringPool(params.c_str()); }
    else if (cmd == "vec2_sub") { inst.op = OP_VEC2_SUB; inst.p1 = addToStringPool(params.c_str()); }
    else if (cmd == "vec2_mul") { inst.op = OP_VEC2_MUL; inst.p1 = addToStringPool(params.c_str()); }
    else if (cmd == "vec2_dot") { inst.op = OP_VEC2_DOT; inst.p1 = addToStringPool(params.c_str()); }

    else if (cmd == "try")   { inst.op = OP_TRY; }
    else if (cmd == "catch") { inst.op = OP_CATCH; inst.p1 = addToStringPool(params.c_str()); }
    else if (cmd == "endtry") { inst.op = OP_ENDTRY; }
    else if (cmd == "throw") { inst.op = OP_THROW; inst.p1 = addToStringPool(params.c_str()); }

    else if (cmd == "wss_connect") { inst.op = OP_WSS_CONNECT; inst.p1 = addToStringPool(params.c_str()); }
    else if (cmd == "wss_send")    { inst.op = OP_WSS_SEND;    inst.p1 = addToStringPool(params.c_str()); }
    

    else if (cmd == "random") { inst.op = OP_RANDOM; splitToInst(inst, params, 2); }
    else if (cmd == "abs")    { inst.op = OP_ABS;    inst.p1 = addToStringPool(params.c_str()); }
    else if (cmd == "round")  { inst.op = OP_ROUND;  inst.p1 = addToStringPool(params.c_str()); }
    else if (cmd == "min")    { inst.op = OP_MIN;    inst.p1 = addToStringPool(params.c_str()); }
    else if (cmd == "max")    { inst.op = OP_MAX;    inst.p1 = addToStringPool(params.c_str()); }
    
    else if (cmd == "concat")    { inst.op = OP_CONCAT;    inst.p1 = addToStringPool(params.c_str()); }
    else if (cmd == "strlen")    { inst.op = OP_STRLEN;    inst.p1 = addToStringPool(params.c_str()); }
    else if (cmd == "substring") { inst.op = OP_SUBSTRING; inst.p1 = addToStringPool(params.c_str()); }
    else if (cmd == "toint")     { inst.op = OP_TOINT;     inst.p1 = addToStringPool(params.c_str()); }
    else if (cmd == "toupper")   { inst.op = OP_TOUPPER;   inst.p1 = addToStringPool(params.c_str()); }
    else if (cmd == "tolower")   { inst.op = OP_TOLOWER;   inst.p1 = addToStringPool(params.c_str()); }

    else if (cmd == "wait")  { inst.op = OP_WAIT; inst.p1 = addToStringPool(params.c_str()); }
    else if (cmd == "yield") { inst.op = OP_YIELD; }
    else if (cmd == "log")   { inst.op = OP_LOG; inst.p1 = addToStringPool(params.c_str()); }

    else if (cmd == "httpget")   { inst.op = OP_HTTPGET;   inst.p1 = addToStringPool(params.c_str()); }
    else if (cmd == "httppost")  { inst.op = OP_HTTPPOST;  inst.p1 = addToStringPool(params.c_str()); }
    else if (cmd == "jsonparse") { inst.op = OP_JSONPARSE; }
    else if (cmd == "jsonget")   { inst.op = OP_JSONGET;   inst.p1 = addToStringPool(params.c_str()); }

    else if (cmd == "play")  { inst.op = OP_PLAY;  inst.p1 = addToStringPool(params.c_str()); }
    else if (cmd == "audio") { inst.op = OP_AUDIO; inst.p1 = addToStringPool(params.c_str()); }
    else if (cmd == "tone" || cmd == "beep") { inst.op = OP_TONE; inst.p1 = addToStringPool(params.c_str()); }
    else if (cmd == "getmouse") { 
      inst.op = OP_GETMOUSE; 
      String p = params; p.replace(",", " ");
      int sp1 = p.indexOf(' '); if(sp1==-1) return false;
      String v1 = p.substring(0, sp1); v1.trim(); inst.p1 = getVariableId(v1, true);
      String rem = p.substring(sp1+1); rem.trim();
      int sp2 = rem.indexOf(' '); if(sp2==-1) return false;
      String v2 = rem.substring(0, sp2); v2.trim(); inst.p2 = getVariableId(v2, true);
      rem = rem.substring(sp2+1); rem.trim();
      int sp3 = rem.indexOf(' '); if(sp3==-1) return false;
      String v3 = rem.substring(0, sp3); v3.trim(); inst.p3 = getVariableId(v3, true);
      rem = rem.substring(sp3+1); rem.trim();
      int sp4 = rem.indexOf(' '); if(sp4==-1) return false;
      String v4 = rem.substring(0, sp4); v4.trim(); inst.p4 = getVariableId(v4, true);
      String v5 = rem.substring(sp4+1); v5.trim(); inst.p5 = getVariableId(v5, true);
    }
    else if (cmd == "getkey")   { inst.op = OP_GETKEY;   inst.p1 = getVariableId(params, true); }
    else if (cmd == "iskeydown") { 
      inst.op = OP_ISKEYDOWN; 
      int sp = params.indexOf(' ');
      if (sp != -1) {
        String hidStr = params.substring(0, sp); hidStr.trim();
        String varStr = params.substring(sp+1); varStr.trim();
        // First param can be number or expr (pool)
        if (isNumberLiteral(hidStr)) inst.p1 = (int32_t)(PTAG_INT | (hidStr.toInt() & 0x1FFFFFFF));
        else inst.p1 = addToStringPool(hidStr.c_str());
        // Second param MUST be variable ID
        inst.p2 = getVariableId(varStr, true);
      }
    }
    else if (cmd == "lastkeydown") { inst.op = OP_LASTKEY; inst.p1 = getVariableId(params, true); }
    else if (cmd == "getkeyevent") { 
       inst.op = OP_GETKEYEVENT; 
       int sp = params.indexOf(' ');
       if (sp != -1) {
         inst.p1 = getVariableId(params.substring(0, sp), true);
         inst.p2 = getVariableId(params.substring(sp+1), true);
       }
    }
    else if (cmd == "gamepad") {
      inst.op = OP_GETGAMEPAD;
      int sp = params.indexOf(' ');
      if (sp != -1) {
        String pStr = params.substring(0, sp); pStr.trim();
        String rem = params.substring(sp+1); rem.trim();
        int sp2 = rem.indexOf(' ');
        if (sp2 != -1) {
          String btnStr = rem.substring(0, sp2); btnStr.trim();
          String varStr = rem.substring(sp2+1); varStr.trim();
          if (isNumberLiteral(pStr)) inst.p1 = (int32_t)(PTAG_INT | (pStr.toInt() & 0x1FFFFFFF));
          else inst.p1 = addToStringPool(pStr.c_str());
          inst.p2 = addToStringPool(btnStr.c_str());
          inst.p3 = getVariableId(varStr, true);
        }
      }
    }
    else if (cmd == "gamepadanalog") {
      inst.op = OP_GETGAMEPADANALOG;
      int sp = params.indexOf(' ');
      if (sp != -1) {
        String pStr = params.substring(0, sp); pStr.trim();
        String rem = params.substring(sp+1); rem.trim();
        int sp2 = rem.indexOf(' ');
        if (sp2 != -1) {
          String axStr = rem.substring(0, sp2); axStr.trim();
          String varStr = rem.substring(sp2+1); varStr.trim();
          if (isNumberLiteral(pStr)) inst.p1 = (int32_t)(PTAG_INT | (pStr.toInt() & 0x1FFFFFFF));
          else inst.p1 = addToStringPool(pStr.c_str());
          inst.p2 = addToStringPool(axStr.c_str());
          inst.p3 = getVariableId(varStr, true);
        }
      }
    }


    else if (cmd == "wifibegin")     { inst.op = OP_WIFIBEGIN;     inst.p1 = addToStringPool(params.c_str()); }
    else if (cmd == "wifidisconnect") { inst.op = OP_WIFIDISCONNECT; }
    else if (cmd == "wifistatus")    { inst.op = OP_WIFISTATUS; }
    else if (cmd == "getip")         { inst.op = OP_GETIP; }
    else if (cmd == "getmac")        { inst.op = OP_GETMAC; }
    else if (cmd == "getgateway")    { inst.op = OP_GETGW; }
    else if (cmd == "ping")          { inst.op = OP_PING; inst.p1 = addToStringPool(params.c_str()); }

    else if (cmd == "fileread")   { inst.op = OP_FILEREAD;   inst.p1 = addToStringPool(params.c_str()); }
    else if (cmd == "filewrite")  { inst.op = OP_FILEWRITE;  inst.p1 = addToStringPool(params.c_str()); }
    else if (cmd == "fileappend") { inst.op = OP_FILEAPPEND; inst.p1 = addToStringPool(params.c_str()); }
    else if (cmd == "pinmode")    { inst.op = OP_PINMODE;    inst.p1 = addToStringPool(params.c_str()); }
    else if (cmd == "digitalwrite"){ inst.op = OP_DIGITALWRITE; inst.p1 = addToStringPool(params.c_str()); }
    else if (cmd == "digitalread") { inst.op = OP_DIGITALREAD; inst.p1 = addToStringPool(params.c_str()); }
    else if (cmd == "analogread")  { inst.op = OP_ANALOGREAD;  inst.p1 = addToStringPool(params.c_str()); }
    else if (cmd == "filesize")   { inst.op = OP_FILESIZE;   inst.p1 = addToStringPool(params.c_str()); }
    else if (cmd == "filecopy")   { inst.op = OP_FILECOPY;   inst.p1 = addToStringPool(params.c_str()); }
    else if (cmd == "filefind")   { inst.op = OP_FILEFIND;   inst.p1 = addToStringPool(params.c_str()); }

    else if (cmd == "array")   { inst.op = OP_ARRAY;   inst.p1 = addToStringPool(params.c_str()); }
    else if (cmd == "aset")    { inst.op = OP_ASET;    splitToInst(inst, params, 3); }
    else if (cmd == "aget")    { inst.op = OP_AGET;    splitToInst(inst, params, 3); }
    else if (cmd == "alen")    { inst.op = OP_ALEN;    inst.p1 = addToStringPool(params.c_str()); }
    else if (cmd == "aset2d")  { inst.op = OP_ASET2D;  inst.p1 = addToStringPool(params.c_str()); }
    else if (cmd == "aget2d")  { inst.op = OP_AGET2D;  inst.p1 = addToStringPool(params.c_str()); }
    else if (cmd == "floor")   { inst.op = OP_FLOOR;   inst.p1 = addToStringPool(params.c_str()); }
    else if (cmd == "ceil")    { inst.op = OP_CEIL;    inst.p1 = addToStringPool(params.c_str()); }
    else if (cmd == "fdef" || cmd == "func") { inst.op = OP_FUNC;    inst.p1 = addToStringPool(params.c_str()); }
    else if (cmd == "fend" || cmd == "endfunc") { inst.op = OP_ENDFUNC; }
    else if (cmd == "call")   { inst.op = OP_CALL;   inst.p1 = addToStringPool(params.c_str()); }
    
    else if (cmd == "ipc")   { inst.op = OP_IPC; inst.p1 = addToStringPool(params.c_str()); }
    else if (cmd == "settext") { inst.op = OP_SETTEXT; inst.p1 = addToStringPool(params.c_str()); }
    else if (cmd == "loadsprite") { inst.op = OP_LOADSPRITE; splitToInst(inst, params, 2); }
    else if (cmd == "drawsprite") { inst.op = OP_DRAWSPRITE; splitToInst(inst, params, 3); }
    else if (cmd == "profile") { inst.op = OP_PROFILE; inst.p1 = addToStringPool(params.c_str()); }

    // ── TURBO Register Komutları ────────────────────────────────
    // Sözdizimi:
    //   rload  RD <expr>        — R[d] = expr değeri
    //   rsave  <var> RD         — var = R[d]
    //   radd   RD RA RB         — R[d] = R[a] + R[b]
    //   rsub   RD RA RB         — R[d] = R[a] - R[b]
    //   rmul   RD RA RB         — R[d] = R[a] * R[b]
    //   rdiv   RD RA RB         — R[d] = R[a] / R[b]
    //   rmov   RD RS            — R[d] = R[s]
    //
    // p1 = dest register (raw int), p2/p3 = src registers (raw int)
    // Ya da rload için p1 = dest, p2 = pool offset of expr
    else if (cmd == "rload") {
      // rload RD <expr>  — R[d] = expr
      // ÖNEMLİ: expr saf bir integer ise PTAG_INT ile göm — string pool kullanma!
      int sp = params.indexOf(' ');
      if (sp != -1) {
        inst.op = OP_RMOV; // p3=-1: rload path
        String rd = params.substring(0, sp); rd.trim();
        int ridx = (rd.length() > 1 && (rd[0]=='R'||rd[0]=='r')) ? rd.substring(1).toInt() : rd.toInt();
        inst.p1 = ridx;
        inst.p3 = -1; // rload sentinel
        String exprStr = params.substring(sp+1); exprStr.trim();
        if (isNumberLiteral(exprStr)) {
          if (exprStr.indexOf('.') == -1) {
            long ival = exprStr.toInt();
            if (ival >= -(1L << 28) && ival < (1L << 28)) {
              inst.p2 = (int32_t)(PTAG_INT | ((int32_t)ival & 0x1FFFFFFF));
            } else inst.p2 = addToStringPool(exprStr.c_str());
          } else {
            float fval = exprStr.toFloat();
            uint32_t bits; memcpy(&bits, &fval, 4);
            inst.p2 = (int32_t)(PTAG_FLOAT | (bits & 0x1FFFFFFF)); // Still limited to 29-bit? No, float is 32-bit.
            // Wait, PTAG uses 3 bits. 29 bits left.
            // Float is 32 bits. I can't fit it.
            // I'll stick to String Pool for floats then, OR just keep it in Pool as before.
            inst.p2 = addToStringPool(exprStr.c_str());
          }
        } else {
          // Expr string pool
          inst.p2 = addToStringPool(exprStr.c_str());
        }
      }
    }
    else if (cmd == "rsave") {
      // rsave <var> RD  ->  SET <var> R[d]
      int sp = params.indexOf(' ');
      if (sp != -1) {
        inst.op = OP_SET;
        String varN = params.substring(0, sp); varN.trim(); varN.toLowerCase();
        inst.p1 = getVariableId(varN, true);
        String rd = params.substring(sp+1); rd.trim();
        int ridx = (rd.length() > 1 && (rd[0]=='R'||rd[0]=='r')) ? rd.substring(1).toInt() : rd.toInt();
        // p2'yi PTAG_REG ile işaretle — resolveParam bunu otomatik olarak R[ridx] değerine dönüştürecektir! ⚡
        inst.p2 = (int32_t)(PTAG_REG | (ridx & 0x3FF));
      }
    }
    else if (cmd == "rmov") {
      // rmov RD RS
      inst.op = OP_RMOV;
      int sp = params.indexOf(' ');
      if (sp != -1) {
        String rd = params.substring(0, sp); rd.trim();
        String rs = params.substring(sp+1);  rs.trim();
        inst.p1 = (rd.length()>1&&(rd[0]=='R'||rd[0]=='r')) ? rd.substring(1).toInt() : rd.toInt();
        inst.p2 = (rs.length()>1&&(rs[0]=='R'||rs[0]=='r')) ? rs.substring(1).toInt() : rs.toInt();
        inst.p3 = 0; // sentinel: "load from register, not expr"
      }
    }
    else if (cmd == "radd" || cmd == "rsub" || cmd == "rmul" || cmd == "rdiv") {
      // radd RD RA RB
      inst.op = (cmd=="radd")?OP_RADD:(cmd=="rsub")?OP_RSUB:(cmd=="rmul")?OP_RMUL:OP_RDIV;
      char rd[8],ra[8],rb[8];
      if (sscanf(params.c_str(), "%s %s %s", rd, ra, rb) >= 3) {
        auto parseReg = [](const char* s) -> int {
          return (strlen(s)>1 && (s[0]=='R'||s[0]=='r')) ? atoi(s+1) : atoi(s);
        };
        inst.p1 = parseReg(rd);
        inst.p2 = parseReg(ra);
        inst.p3 = parseReg(rb);
      }
    }
    // ── TURBO Grafik Register Komutları ────────────────────────────────
    // rpixel  RX RY [RC]           — piksel çiz, renk opsiyonel
    // rline   RX1 RY1 RX2 RY2 [RC] — çizgi çiz, renk opsiyonel
    // rsetcolor RR RG RB           — themeAccent'i register'lardan ayarla
    else if (cmd == "rpixel") {
      inst.op = OP_RPIXEL;
      char ra[8], rb[8], rc[8];
      int n = sscanf(params.c_str(), "%s %s %s", ra, rb, rc);
      auto pr = [](const char* s) -> int {
        return (strlen(s)>1 && (s[0]=='R'||s[0]=='r')) ? atoi(s+1) : atoi(s);
      };
      inst.p1 = (n >= 1) ? pr(ra) : -1; // X reg
      inst.p2 = (n >= 2) ? pr(rb) : -1; // Y reg
      inst.p3 = (n >= 3) ? pr(rc) : -1; // Color reg (-1 = themeAccent kullan)
    }
    else if (cmd == "rline") {
      inst.op = OP_RLINE;
      char r1[8],r2[8],r3[8],r4[8],r5[8];
      int n = sscanf(params.c_str(), "%s %s %s %s %s", r1,r2,r3,r4,r5);
      auto pr = [](const char* s) -> int {
        return (strlen(s)>1 && (s[0]=='R'||s[0]=='r')) ? atoi(s+1) : atoi(s);
      };
      inst.p1 = (n>=1)?pr(r1):-1; // X1
      inst.p2 = (n>=2)?pr(r2):-1; // Y1
      inst.p3 = (n>=3)?pr(r3):-1; // X2
      inst.p4 = (n>=4)?pr(r4):-1; // Y2
      inst.p5 = (n>=5)?pr(r5):-1; // Color (-1 = themeAccent)
    }
    else if (cmd == "rsetcolor") {
      inst.op = OP_RSETCOLOR;
      char rr[8],rg[8],rb[8];
      int n = sscanf(params.c_str(), "%s %s %s", rr,rg,rb);
      auto pr = [](const char* s) -> int {
        return (strlen(s)>1 && (s[0]=='R'||s[0]=='r')) ? atoi(s+1) : atoi(s);
      };
      inst.p1 = (n>=1)?pr(rr):-1;
      inst.p2 = (n>=2)?pr(rg):-1;
      inst.p3 = (n>=3)?pr(rb):-1;
    }
    else if (cmd == "rget_ind" || cmd == "rset_ind") {
      inst.op = (cmd == "rget_ind") ? OP_RGET_IND : OP_RSET_IND;
      char r1[8], r2[8];
      if (sscanf(params.c_str(), "%s %s", r1, r2) >= 2) {
        auto pr = [](const char* s) -> int {
          return (strlen(s)>1 && (s[0]=='R'||s[0]=='r')) ? atoi(s+1) : atoi(s);
        };
        inst.p1 = pr(r1);
        inst.p2 = pr(r2);
      }
    }
    else if (cmd == "exit" || cmd == "end") { inst.op = OP_EXIT; }
    else if (cmd == "nop")    { inst.op = OP_NOP; }

    // Fallback: If unknown but has '=', assume SET
    // Fallback: If unknown but has '=', assume SET
    // Assignment: x = 5 (Immediate execution)
    if (inst.op == OP_UNKNOWN && line.indexOf('=') != -1) {
      int eq = line.indexOf('=');
      String vn = line.substring(0, eq); vn.trim();
      String p2 = line.substring(eq + 1); p2.trim();
      
      // If it's a known variable or looks like one, treat as SET
      inst.op = OP_SET; 
      inst.p1 = getVariableId(vn, true);
      inst.p2 = addToStringPool(p2.c_str());
    }

    if (inst.op == OP_UNKNOWN) {
      logToFile("[VM-ERR] Unknown command at L" + String(i+1) + ": " + cmd);
      scriptErrorLine = i + 1;
      unknown_ops++;
    }
  }
  // --- PASS 2: Labels & Control Flow Fixes ---
  int ifStack[16]; int ifStackTop = -1;
  int whileStack[16]; int whileStackTop = -1;
  int forStack[16]; int forStackTop = -1;

  for (int i = 0; i < scriptLineCount; i++) {
    uint8_t op = scriptProgram[i].op;
    
   // Label resolution for jumps
    if (op == OP_GOTO || op == OP_GOSUB || op == OP_ONCLICK) {
      const char* lblName = getPooledString(scriptProgram[i].p1);
      int tgt = findScriptLabel(lblName);
      if (tgt != -1) {
        scriptProgram[i].p1 = tgt; // Etiket bulundu, satır indeksine dönüştür
      } else {
        unknown_ops++; // Derlemeyi başarısız say ki VM bozuk bytecode çalıştırmasın!
      }
    }
    else if (op == OP_ONTIMER || op == OP_ONGPIO) {
      int tgt = findScriptLabel(getPooledString(scriptProgram[i].p2));
      if (tgt != -1) scriptProgram[i].p2 = tgt;
    }

    // Control Flow Pre-calculation (JUMP Targets)
    else if (op == OP_IF) {
       if (ifStackTop < 15) ifStack[++ifStackTop] = i;
    }
    else if (op == OP_ELSE || op == OP_ELSEIF) {
       if (ifStackTop >= 0) {
          scriptProgram[ifStack[ifStackTop]].p2 = i; // IF/ELSEIF jumps here if false
          ifStack[ifStackTop] = i; // New anchor
       }
    }
    else if (op == OP_ENDIF) {
       if (ifStackTop >= 0) {
          scriptProgram[ifStack[ifStackTop]].p2 = i; 
          ifStackTop--;
       }
    }
    else if (op == OP_WHILE) {
       if (whileStackTop < 15) whileStack[++whileStackTop] = i;
    }
    else if (op == OP_WEND) {
       if (whileStackTop >= 0) {
          int start = whileStack[whileStackTop];
          scriptProgram[i].p1 = start; // WEND/ENDWHILE jumps to WHILE
          scriptProgram[start].p2 = i; // WHILE jumps to WEND+1 if false
          whileStackTop--;
       }
    }
    else if (op == OP_FOR) {
       if (forStackTop < 15) forStack[++forStackTop] = i;
    }
    else if (op == OP_NEXT) {
       if (forStackTop >= 0) {
          int start = forStack[forStackTop];
          scriptProgram[i].p1 = start; // NEXT jumps to FOR
          scriptProgram[start].p5 = i; // FOR jumps to NEXT+1 if false (p5 used because p1-p4 occupied)
          forStackTop--;
       }
    }
  }
  
  if (unknown_ops > 0) {
    Serial.printf("[VM] Compilation Failed: %d unknown commands.\n", unknown_ops);
    return false;
  }
  Serial.printf("[VM] Script Compiled: %d lines, %d labels. Memory Pool: %d bytes.\n", scriptLineCount, scriptLabelCount, scriptStringPoolPtr);
  return true;
}

// Helper to load files recursively for INCLUDE
static void internalLoadLines(String filePath, int recursion) {
  if (recursion > 5) { logToFile("[VM-ERR] INCLUDE recursion limit!"); return; }
  if (!sdCardPresent || !SD.exists(filePath)) { logToFile("[VM-ERR] File not found: " + filePath); return; }
  
  File f = SD.open(filePath);
  char lineBuf[MAX_SCRIPT_LINE_LEN + 16]; // Static buffer
  while (f.available() && scriptLineCount < MAX_SCRIPT_LINES) {
    int len = f.readBytesUntil('\n', lineBuf, MAX_SCRIPT_LINE_LEN + 15);
    lineBuf[len] = 0; if (len > 0 && lineBuf[len-1] == '\r') lineBuf[--len] = 0;
    
    // Auto-expand single-line IF THEN (v3.2 Robust)
    String rawLine(lineBuf); rawLine.trim();
    String rawLineL = rawLine; rawLineL.toLowerCase();
    
    if (rawLineL.startsWith("if ")) {
      int thenPos = rawLineL.indexOf("then");
      if (thenPos != -1) {
        String cond = rawLine.substring(3, thenPos); cond.trim();
        String cmd = rawLine.substring(thenPos + 4); cmd.trim();
        
        // Single-line IF: 'if cond then cmd'
        if (cmd.length() > 0 && !cmd.equalsIgnoreCase("then")) {
           if (scriptLineCount < MAX_SCRIPT_LINES - 3) {
             String ifLine = "IF " + cond;
             strncpy(scriptLines[scriptLineCount++], ifLine.c_str(), MAX_SCRIPT_LINE_LEN-1);
             strncpy(scriptLines[scriptLineCount++], cmd.c_str(), MAX_SCRIPT_LINE_LEN-1);
             strncpy(scriptLines[scriptLineCount++], "ENDIF", MAX_SCRIPT_LINE_LEN-1);
           }
           continue;
        } else {
           // Block IF start: 'if cond then' -> just rewrite as 'if cond'
           String ifLine = "IF " + cond;
           strncpy(scriptLines[scriptLineCount++], ifLine.c_str(), MAX_SCRIPT_LINE_LEN-1);
           continue;
        }
      }
    }

    char* lPtr = lineBuf;
    while (*lPtr == ' ' || *lPtr == '\t') lPtr++;
    if (*lPtr == 0 || *lPtr == '#' || (*lPtr == '/' && *(lPtr+1) == '/')) continue;

    String fullLine(lPtr);
    int startPos = 0;
    while (startPos < (int)fullLine.length()) {
      int semiIdx = fullLine.indexOf(';', startPos);
      String l;
      if (semiIdx != -1) { l = fullLine.substring(startPos, semiIdx); startPos = semiIdx + 1; }
      else { l = fullLine.substring(startPos); startPos = fullLine.length(); }
      l.trim();
      if (l.length() == 0) continue;

      // INCLUDE desteği
      String lLower = l; lLower.toLowerCase();
      if (lLower.startsWith("include ")) {
        String incFile = l.substring(8); incFile.trim();
        if (incFile.startsWith("\"")) incFile = incFile.substring(1, incFile.length()-1);
        String fullIncPath = incFile.startsWith("/") ? incFile : (filePath.substring(0, filePath.lastIndexOf('/')+1) + incFile);
        internalLoadLines(fullIncPath, recursion + 1);
        continue;
      }

      if (scriptLineCount < MAX_SCRIPT_LINES) {
        strncpy(scriptLines[scriptLineCount], l.c_str(), MAX_SCRIPT_LINE_LEN - 1);
        if (l.startsWith(":")) { 
          String ls = l.substring(1); ls.trim(); 
          addScriptLabel(ls, scriptLineCount); 
        } else if (lLower.startsWith("label ")) { 
          String ls = l.substring(6); ls.trim(); 
          addScriptLabel(ls, scriptLineCount); 
        }
        scriptLineCount++;
      }
    }
  }
  f.close();
}

static void executeCommandInline(String line, int lineNum) {
  line.trim();
  if (line.length() == 0) return;
  int spIdx = line.indexOf(' ');
  String cmd = (spIdx == -1) ? line : line.substring(0, spIdx);
  String params = (spIdx == -1) ? "" : line.substring(spIdx + 1);
  cmd.toLowerCase(); cmd.trim(); params.trim();
  if (cmd == "set") {
      int eq = params.indexOf('=');
      if (eq != -1) {
        String vn = params.substring(0, eq); vn.trim();
        String val = params.substring(eq + 1); val.trim();
        scriptVariables[getVariableId(vn, true)] = evaluateValueToSV(val, 0);
      } else {
        int sp = params.indexOf(' ');
        if (sp != -1) {
          String vn = params.substring(0, sp); vn.trim();
          String val = params.substring(sp + 1); val.trim();
          scriptVariables[getVariableId(vn, true)] = evaluateValueToSV(val, 0);
        }
      }
  } else if (cmd == "gosub") {
      int tgt = findScriptLabel(params);
      if (tgt != -1 && gosubTop < MAX_GOSUB_DEPTH - 1) {
          gosubStack[++gosubTop] = scriptCurrentLine + 1;
          scriptCurrentLine = tgt - 1;
      }
  } else if (cmd == "goto") {
      int tgt = findScriptLabel(params);
      if (tgt != -1) scriptCurrentLine = tgt - 1;
  } else if (cmd == "exit") {
      runningScript = false;
  } else if (line.indexOf('=') != -1) {
      int eq = line.indexOf('=');
      String vn = line.substring(0, eq); vn.trim();
      String val = line.substring(eq + 1); val.trim();
      scriptVariables[getVariableId(vn, true)] = evaluateValueToSV(val, 0);
  }
}

// ─────────────────────────────────────────────
// VM Task
// ─────────────────────────────────────────────
// --- VM Debug Helper: Opcode to String ---
const char* getOpName(uint8_t op) {
    switch(op) {
        case OP_END: return "END"; case OP_PRINT: return "PRINT"; case OP_PRINTLN: return "PRINTLN";
        case OP_CLEAR: return "CLEAR"; case OP_SETCURSOR: return "SETCURSOR"; case OP_SETBGCOLOR: return "SETBGCOLOR";
        case OP_SETTEXTCOLOR: return "SETTEXTCOLOR"; case OP_WAIT: return "WAIT"; case OP_SHOW: return "SHOW";
        case OP_RMOV: return "RMOV"; case OP_RADD: return "RADD"; case OP_RSUB: return "RSUB";
        case OP_RMUL: return "RMUL"; case OP_RDIV: return "RDIV"; case OP_RPIXEL: return "RPIXEL";
        case OP_RLINE: return "RLINE"; case OP_FOR: return "FOR"; case OP_NEXT: return "NEXT";
        case OP_IF: return "IF"; case OP_GOTO: return "GOTO"; case OP_ARRAY: return "ARRAY";
        case OP_ASET: return "ASET"; case OP_AGET: return "AGET"; case OP_PROFILE: return "PROFILE";
        case OP_SET: return "SET"; case OP_RANDOM: return "RANDOM"; case OP_ISKEYDOWN: return "ISKEYDOWN";
        case OP_ADD: return "ADD"; case OP_SUB: return "SUB"; case OP_MUL: return "MUL"; case OP_DIV: return "DIV";
        case OP_ELSE: return "ELSE"; case OP_ENDIF: return "ENDIF";
        case OP_GETMOUSE: return "GETMOUSE"; case OP_RECT: return "RECT"; case OP_DRAWRECT: return "DRAWRECT";
        case OP_SQUARE: return "SQUARE"; case OP_LINE: return "LINE"; case OP_PIXEL: return "PIXEL";
        case OP_GOSUB: return "GOSUB"; case OP_RETURN: return "RETURN"; case OP_ELSEIF: return "ELSEIF";
        case OP_WHILE: return "WHILE"; case OP_WEND: return "WEND"; case OP_FUNC: return "FUNC";
        case OP_ENDFUNC: return "ENDFUNC"; case OP_CALL: return "CALL"; case OP_ONCLICK: return "ONCLICK";
        case OP_ONTIMER: return "ONTIMER"; case OP_ONGPIO: return "ONGPIO"; case OP_EVENT_EXIT: return "EVENT_EXIT";
        case OP_MATH: return "MATH"; case OP_MOD: return "MOD"; case OP_INC: return "INC";
        case OP_DEC: return "DEC"; case OP_SEED: return "SEED"; case OP_ABS: return "ABS";
        case OP_ROUND: return "ROUND"; case OP_MIN: return "MIN"; case OP_MAX: return "MAX";
        case OP_SQRT: return "SQRT"; case OP_POW: return "POW"; case OP_SIN: return "SIN";
        case OP_COS: return "COS"; case OP_TAN: return "TAN"; case OP_ATAN2: return "ATAN2";
        case OP_VEC2_ADD: return "VEC2_ADD"; case OP_VEC2_SUB: return "VEC2_SUB";
        case OP_VEC2_MUL: return "VEC2_MUL"; case OP_VEC2_DOT: return "VEC2_DOT";
        case OP_CONCAT: return "CONCAT"; case OP_STRLEN: return "STRLEN";
        case OP_SUBSTRING: return "SUBSTRING"; case OP_TOINT: return "TOINT";
        case OP_TOUPPER: return "TOUPPER"; case OP_TOLOWER: return "TOLOWER";
        case OP_YIELD: return "YIELD"; case OP_LOG: return "LOG"; case OP_TRY: return "TRY";
        case OP_CATCH: return "CATCH"; case OP_ENDTRY: return "ENDTRY"; case OP_THROW: return "THROW";
        case OP_HTTPGET: return "HTTPGET"; case OP_HTTPPOST: return "HTTPPOST";
        case OP_JSONPARSE: return "JSONPARSE"; case OP_JSONGET: return "JSONGET";
        case OP_WSS_CONNECT: return "WSS_CONNECT"; case OP_WSS_SEND: return "WSS_SEND";
        case OP_PLAY: return "PLAY"; case OP_SFX: return "SFX"; case OP_AUDIO: return "AUDIO";
        case OP_TONE: return "TONE"; case OP_DRAWBMP: return "DRAWBMP";
        case OP_BUTTON: return "BUTTON"; case OP_PROGRESSBAR: return "PROGRESSBAR";
        case OP_GETSCREEN: return "GETSCREEN"; case OP_GETKEY: return "GETKEY";
        case OP_WIFIBEGIN: return "WIFIBEGIN"; case OP_WIFIDISCONNECT: return "WIFIDISCONNECT";
        case OP_WIFISTATUS: return "WIFISTATUS"; case OP_GETIP: return "GETIP";
        case OP_GETMAC: return "GETMAC"; case OP_GETGW: return "GETGW"; case OP_PING: return "PING";
        case OP_FILEREAD: return "FILEREAD"; case OP_FILEWRITE: return "FILEWRITE";
        case OP_FILEAPPEND: return "FILEAPPEND"; case OP_FILESIZE: return "FILESIZE";
        case OP_FILECOPY: return "FILECOPY"; case OP_FILEFIND: return "FILEFIND";
        case OP_PINMODE: return "PINMODE"; case OP_DIGITALWRITE: return "DIGITALWRITE";
        case OP_DIGITALREAD: return "DIGITALREAD"; case OP_ANALOGREAD: return "ANALOGREAD";
        case OP_ASET2D: return "ASET2D"; case OP_AGET2D: return "AGET2D";
        case OP_IPC: return "IPC"; case OP_FLOOR: return "FLOOR"; case OP_CEIL: return "CEIL";
        case OP_FDEF: return "FDEF"; case OP_FEND: return "FEND"; case OP_SETTEXT: return "SETTEXT";
        case OP_EXIT: return "EXIT"; case OP_LOADSPRITE: return "LOADSPRITE";
        case OP_DRAWSPRITE: return "DRAWSPRITE"; case OP_LASTKEY: return "LASTKEY";
        case OP_GETKEYEVENT: return "GETKEYEVENT"; case OP_GETGAMEPAD: return "GETGAMEPAD";
        case OP_GETGAMEPADANALOG: return "GETGAMEPADANALOG";
        case OP_SETCOLOR: return "SETCOLOR";
        default: return "OP";
    }
}

unsigned long scriptStartTime = 0;

void runEyuScriptTask(void* pvParameters) {
  scriptStartTime = millis(); // Set startup time for key protection
  String filePath = globalScriptPath; runningScript = true;
  setStatusLED(EYU_LED_GREEN); // Green while running script
  desktopMessage = "Running Turbo Script...";
  logToFile("=== EyuScript 3.0 TURBO Başlatıldı ===");
  // Clear keyboard states to prevent key leaks from menus/ESDOS
  lastKeyDownHID = 0;
  lastKeyUpHID = 0;
  keyEventAvailable = false;
  scriptKeyAvailable = false;
  scriptKeyBuffer = "";
  for (int i = 0; i < 256; i++) {
    heldKeys[i] = false;
  }
  
  holdingVgaLock = false;
  vmDebugActive = false; // Force VM instruction debugging active

  if (!sdCardPresent || !SD.exists(filePath)) { runningScript = false; vTaskDelete(NULL); return; }
  
  if (xSemaphoreTake(sdMutex, pdMS_TO_TICKS(5000)) == pdTRUE) {
    scriptLineCount = 0; 
    scriptLabelCount = 0;
    scriptErrorLine = 0;
    scriptStringPoolPtr = 0;
    internalLoadLines(filePath, 0);
    xSemaphoreGive(sdMutex);
  }

  // 🛡️ Çelik Zırh 3.1: PSRAM bölgelerini güvenli uyandır (Destructor-Safe Initialization)
  // memset yerine doğrudan C++ kurallarına göre nesne bazlı temizlik
  for(int i=0; i<VM_PSRAM_COUNT; i++) {
    vmPsramRegs[i].type = VAL_NULL;
    vmPsramRegs[i].data.iVal = 0;
    vmPsramRegs[i].sVal = "";
  }
  for(int i=0; i<MAX_SCRIPT_VARS; i++) {
    scriptVariables[i].type = VAL_NULL;
    scriptVariables[i].sVal = "";
    scriptVarNames[i] = "";
  }
  for(int i=0; i<MAX_SCRIPT_ARRAYS; i++) {
    scriptArrays[i].size = 0;
    for(int j=0; j<MAX_ARRAY_SIZE; j++) {
      scriptArrays[i].values[j].type = VAL_NULL;
      scriptArrays[i].values[j].sVal = "";
    }
  }

  // Hibrit Register Temizleme: SRAM tabanlı olanları (R0-R63) sıfırla
  for (int i = 0; i < VM_SRAM_COUNT;   i++) vmSramRegs[i]  = ScriptValue(); 

  if (!compileProgram()) {
    vmGSOD("Derleme Hatasi: Hatali komut bulundu.\nDosya: " + filePath, scriptErrorLine);
  }
  scriptCurrentLine = 0;
  scriptEventCount = 0;
  unsigned long lastEventCheck = 0;
  int instrYieldCounter = 0;
  scriptHasError = false; scriptErrorMsg = "";
  
  // Reset input states for the new script
  bool prevLeftBtn = false;
  while (scriptCurrentLine < scriptLineCount && runningScript) {
    // 0. Emergency Exit & Yield (ALWAYS FIRST)
    esp_task_wdt_reset();
    
    if (instrYieldCounter % 20 == 0) {
      processUSBInput();
    }
    
    // ESC Kontrolü - Muteksten bağımsız hızlı çıkış
    // 🛡️ Çift Katmanlı Koruma: Hem scriptBuffer hem de doğrudan donanım tuş durumu
    if (scriptKeyAvailable || (heldKeys[0x29])) {
      if (scriptKeyBuffer == "0x29" || scriptKeyBuffer == "ESCAPE" || scriptKeyBuffer == "escape" || heldKeys[0x29]) {
        Serial.printf("[VM-EXIT] Emergency ESC exit triggered. scriptKeyBuffer: '%s', heldKeys[0x29]: %d\n", scriptKeyBuffer.c_str(), heldKeys[0x29]);
        runningScript = false;
        scriptKeyAvailable = false;
        if (holdingVgaLock) { xSemaphoreGive(systemMutex); holdingVgaLock = false; }
        logToFile("[VM] User Emergency Exit via ESC (HID 0x29)");
        break;
      }
    }

    // Her döngü başında sistemin nefes almasına izin ver (Daha hafif yield)
    if (++instrYieldCounter >= 500) { 
      instrYieldCounter = 0; 
      if (holdingVgaLock) { xSemaphoreGive(systemMutex); holdingVgaLock = false; }
      taskYIELD(); 
    }

    if (scriptHasError) {
      // Find nearest CATCH
      int catchLine = -1;
      // Simple implementation: scan forward for OP_CATCH at same level? 
      // Better: terminate for now with the error msg
      break; 
    }
    // 0. Hardware Interrupt Check (Highest Priority)
    for (int i = 0; i < gpioIntCount; i++) {
      if (registeredInterrupts[i].triggered) {
        registeredInterrupts[i].triggered = false;
        if (gosubTop < MAX_GOSUB_DEPTH - 1) {
          gosubStack[++gosubTop] = scriptCurrentLine;
          scriptCurrentLine = registeredInterrupts[i].startLine;
          logToFile("[VM] HW Interrupt Triggered Pin " + String(registeredInterrupts[i].pin));
          break; // Jump to ISR
        }
      }
    }

    unsigned long now = millis();
    if (now - lastEventCheck > 30) {
      lastEventCheck = now;
      for(int i=0; i<scriptEventCount; i++) {
        // 1. Timer Events
        if(scriptEvents[i].type == 1 && now - scriptEvents[i].lastRun >= (unsigned long)scriptEvents[i].p1) {
          scriptEvents[i].lastRun = now;
          if (gosubTop < MAX_GOSUB_DEPTH - 1) {
             gosubStack[++gosubTop] = scriptCurrentLine; scriptCurrentLine = scriptEvents[i].startLine;
          }
        }
        // 2. Click Events (Rising Edge detection)
        else if(scriptEvents[i].type == 0) {
           if (leftButton && !prevLeftBtn) {
             if (gosubTop < MAX_GOSUB_DEPTH - 1) {
               gosubStack[++gosubTop] = scriptCurrentLine; scriptCurrentLine = scriptEvents[i].startLine;
             }
           }
        }
        // 3. GPIO Events (Rising Edge logic simplified)
        else if(scriptEvents[i].type == 2) {
           if(digitalRead(scriptEvents[i].p1) == HIGH) {
              // Simplified: Only pulse on high (No static tracking for now to save complexity)
              if (gosubTop < MAX_GOSUB_DEPTH - 1) {
                gosubStack[++gosubTop] = scriptCurrentLine; scriptCurrentLine = scriptEvents[i].startLine;
              }
           }
        }
      }
      prevLeftBtn = leftButton;
    }

    // Feed WDT every instruction. Yield scheduler every 4000 instructions (no delay, no lock release).
    esp_task_wdt_reset();
    if (++instrYieldCounter >= 4000) {
      instrYieldCounter = 0;
      taskYIELD();
    }

    Instruction& inst = scriptProgram[scriptCurrentLine];
    lineNumber = scriptCurrentLine + 1;

    // Batching logic disabled to prevent systemMutex deadlocks 🛡️
    bool isGraphics = false;
    
    if (isGraphics && !holdingVgaLock) {
      if (xSemaphoreTake(systemMutex, 0) == pdTRUE) {
        holdingVgaLock = true;
      } else {
        // Yield shortly and retry once
        taskYIELD();
        if (xSemaphoreTake(systemMutex, 0) == pdTRUE) {
           holdingVgaLock = true;
        } else {
           continue; 
        }
      }
    } else if (inst.op == OP_WAIT || inst.op == OP_YIELD || inst.op == OP_SHOW || inst.op == OP_EXIT) {
      if (holdingVgaLock) { xSemaphoreGive(systemMutex); holdingVgaLock = false; }
    }

    // Debug logging only when explicitly enabled (vmDebugActive=true for now)
    // Debug logging disabled (v3.3 Stable)
    bool vmLogThis = false; // Turn off for production speed
    if (vmLogThis) {
      Serial.printf("[VM] L%d (Op:%d): P1=%X P2=%X P3=%X\n",
                    lineNumber, inst.op, (uint32_t)inst.p1, (uint32_t)inst.p2, (uint32_t)inst.p3);
    }
    if(inst.op == OP_UNKNOWN) Serial.printf("[VM-WARN] Unknown OP at L%d\n", lineNumber);
 
    // 🔍 Ultra-Okunabilir Telemetri (Her Parametre Deşifre Edilir)
    // 🔍 Ultra-Okunabilir Telemetri (Her Parametre Deşifre Edilir)
    if (vmDebugActive) {
      auto resolveDbg = [](int p, uint8_t op, int paramNum) -> String {
        if (p == -1) return "none";
        if (p & PTAG_INT) return String(p & 0x1FFFFFFF);
        if (p & PTAG_REG) return "R" + String(p & 0x3FF);
        
        // Eğer OP_GOTO/GOSUB ise ve Pass 2'de resolved olduysa p doğrudan Satır Numarasıdır!
        if ((op == OP_GOTO || op == OP_GOSUB) && paramNum == 1) return "L" + String(p + 1);

        if (p >= 0 && p < scriptStringPoolPtr) return String(getPooledString(p));
        return "ID:" + String(p);
      };
      String s1 = resolveDbg(inst.p1, inst.op, 1), s2 = resolveDbg(inst.p2, inst.op, 2), s3 = resolveDbg(inst.p3, inst.op, 3);
      Serial.printf("[VM-EXEC] L%d: %s (%s, %s, %s)\n", lineNumber, getOpName(inst.op), s1.c_str(), s2.c_str(), s3.c_str());
    }

    switch (inst.op) {
      case OP_END: 
      case OP_EXIT:
        Serial.printf("[VM-EXIT] Explicit EXIT or END opcode executed at L%d\n", lineNumber);
        if (holdingVgaLock) { xSemaphoreGive(systemMutex); holdingVgaLock = false; }
        runningScript = false; break;
      case OP_FUNC: { // Skip func body
        int depth = 1;
        while (scriptCurrentLine < scriptLineCount - 1 && depth > 0) {
          scriptCurrentLine++;
          if (scriptProgram[scriptCurrentLine].op == OP_FUNC) depth++;
          else if (scriptProgram[scriptCurrentLine].op == OP_ENDFUNC) depth--;
        }
        break;
      }
      case OP_CALL: {
        String fname = getPooledString(inst.p1);
        int start = -1;
        for(int i=0; i<scriptFunctionCount; i++) { if(scriptFunctions[i].name == fname) { start=scriptFunctions[i].startLine; break; } }
        if (start != -1 && gosubTop < MAX_GOSUB_DEPTH - 1) {
          gosubStack[++gosubTop] = scriptCurrentLine + 1; scriptCurrentLine = start; continue;
        }
        break;
      }
      case OP_ENDFUNC: if (gosubTop >= 0) { scriptCurrentLine = gosubStack[gosubTop--]; continue; } break;

      case OP_ONCLICK: {
        if (scriptEventCount < MAX_EVENTS) {
          scriptEvents[scriptEventCount++] = { 0, 0, inst.p1, true, 0 };
        }
        break;
      }
      case OP_ONTIMER: {
        if (scriptEventCount < MAX_EVENTS) {
          scriptEvents[scriptEventCount++] = { 1, evaluateValue(getPooledString(inst.p1)).toInt(), inst.p2, true, millis() };
        }
        break;
      }
      // case OP_ONGPIO (Polling version removed, using HW Interrupt version below)
      case OP_EVENT_EXIT: if (gosubTop >= 0) { scriptCurrentLine = gosubStack[gosubTop--]; continue; } break;

      // --- 1. Output & Console ---
      case OP_PRINT:
      case OP_PRINTLN: {
        String s = resolveParam(inst.p1).toString();
        bool gotLock = holdingVgaLock;
        if (!gotLock) gotLock = (xSemaphoreTake(systemMutex, pdMS_TO_TICKS(50)) == pdTRUE);
        
        if (gotLock) {
            videodisplay.setTextColor(themeColorText, themeColorBG);
            if (inst.op == OP_PRINTLN) videodisplay.println(s.c_str()); 
            else videodisplay.print(s.c_str());
            if (!holdingVgaLock) xSemaphoreGive(systemMutex);
        }
        break;
      }
      case OP_LOG: if(vmDebugActive) Serial.println("[TURBO] " + resolveParam(inst.p1).toString()); break;
      case OP_CLEAR: {
        bool gotLock = holdingVgaLock;
        if (!gotLock) gotLock = (xSemaphoreTake(systemMutex, pdMS_TO_TICKS(50)) == pdTRUE);
        if (gotLock) {
            videodisplay.fillScreen(themeColorBG); 
            if (!holdingVgaLock) xSemaphoreGive(systemMutex);
        }
        break;
      }
      case OP_SHOW: {
        if (holdingVgaLock) { xSemaphoreGive(systemMutex); holdingVgaLock = false; }
        if (xSemaphoreTake(systemMutex, pdMS_TO_TICKS(50)) == pdTRUE) { 
            if (vmShowFps) {
                int hudX = SCREEN_WIDTH - 60, hudY = 5, hudW = 55, hudH = 10;
                videodisplay.fillRect(hudX, hudY, hudW, hudH, themeColorBG); 
                videodisplay.setTextColor(videodisplay.RGB(30, 255, 30), themeColorBG);
                videodisplay.setCursor(hudX, hudY);
                videodisplay.printf("FPS: %d", evaluateValueToSV("fps", 0).toInt());
            }
            videodisplay.show(); 
            xSemaphoreGive(systemMutex); 
        } else {
            if (vmDebugActive) Serial.println("[VM-WARN] Mutex beklerken timeout! SHOW atlandi.");
        }
        break;
      }

      // --- 2. Graphics ---
      case OP_SETCURSOR: {
        int x = resolveParam(inst.p1).toInt();
        int y = resolveParam(inst.p2).toInt();
        if (holdingVgaLock || xSemaphoreTake(systemMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            videodisplay.setCursor(x * 8, y * 12); 
            if (!holdingVgaLock) xSemaphoreGive(systemMutex);
        }
        break;
      }
      case OP_SETCOLOR: {
        uint16_t color = themeColorAccent;
        if (inst.p2 != -1 && inst.p3 != -1) { // RGB modu
           int r = resolveParam(inst.p1).toInt();
           int g = resolveParam(inst.p2).toInt();
           int b = resolveParam(inst.p3).toInt();
           color = videodisplay.RGB(r, g, b);
        } else { // Tek deger (HEX/DEC)
           color = (uint16_t)resolveParam(inst.p1).toInt();
        }
        themeColorAccent = color;
        break;
      }
      case OP_SETBGCOLOR: {
        uint16_t color = themeColorBG;
        if (inst.p2 != -1 && inst.p3 != -1) { 
           int r = resolveParam(inst.p1).toInt();
           int g = resolveParam(inst.p2).toInt();
           int b = resolveParam(inst.p3).toInt();
           color = videodisplay.RGB(r, g, b);
        } else {
           color = (uint16_t)resolveParam(inst.p1).toInt();
        }
        themeColorBG = color;
        break;
      }
      case OP_SETTEXTCOLOR: {
        if (inst.p2 != -1 && inst.p3 != -1) { 
           int r = resolveParam(inst.p1).toInt();
           int g = resolveParam(inst.p2).toInt();
           int b = resolveParam(inst.p3).toInt();
           themeColorText = videodisplay.RGB(r, g, b);
        } else {
           themeColorText = (uint16_t)resolveParam(inst.p1).toInt();
        }
        break;
      }
      case OP_SQUARE: {
        String p = expandVars(getPooledString(inst.p1));
        int x,y,s; if(sscanf(p.c_str(),"%d %d %d",&x,&y,&s)>=3) {
          videodisplay.fillRect(x,y,s,s,themeColorAccent);
        }
        break;
      }
      case OP_RECT:
      case OP_DRAWRECT: {
        int x = resolveParam(inst.p1).toInt();
        int y = resolveParam(inst.p2).toInt();
        int w = resolveParam(inst.p3).toInt();
        int h = resolveParam(inst.p4).toInt();
        uint16_t c = (inst.p5 != -1) ? (uint16_t)resolveParam(inst.p5).toInt() : themeColorAccent;
        if (holdingVgaLock || xSemaphoreTake(systemMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            if(inst.op==OP_RECT) videodisplay.fillRect(x,y,w,h,c); else videodisplay.drawRect(x,y,w,h,c);
            if (!holdingVgaLock) xSemaphoreGive(systemMutex);
        }
        break;
      }
      case OP_PIXEL: {
        int x = resolveParam(inst.p1).toInt();
        int y = resolveParam(inst.p2).toInt();
        uint16_t c = (inst.p3 != -1) ? (uint16_t)resolveParam(inst.p3).toInt() : themeColorAccent;
        if (holdingVgaLock || xSemaphoreTake(systemMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            videodisplay.drawPixel(x, y, c);
            if (!holdingVgaLock) xSemaphoreGive(systemMutex);
        }
        break;
      }
      case OP_CIRCLE:
      case OP_FILLCIRCLE: {
        int x = resolveParam(inst.p1).toInt();
        int y = resolveParam(inst.p2).toInt();
        int r = resolveParam(inst.p3).toInt();
        uint16_t c = (inst.p4 != -1) ? (uint16_t)resolveParam(inst.p4).toInt() : themeColorAccent;
        if (holdingVgaLock || xSemaphoreTake(systemMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            if(inst.op==OP_CIRCLE) videodisplay.drawCircle(x,y,r,c); else videodisplay.fillCircle(x,y,r,c);
            if (!holdingVgaLock) xSemaphoreGive(systemMutex);
        }
        break;
      }
      case OP_LINE: {
        int x1 = resolveParam(inst.p1).toInt();
        int y1 = resolveParam(inst.p2).toInt();
        int x2 = resolveParam(inst.p3).toInt();
        int y2 = resolveParam(inst.p4).toInt();
        uint16_t c = (inst.p5 != -1) ? (uint16_t)resolveParam(inst.p5).toInt() : themeColorAccent;
        if (holdingVgaLock || xSemaphoreTake(systemMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            videodisplay.drawLine(x1,y1,x2,y2,c); 
            if (!holdingVgaLock) xSemaphoreGive(systemMutex);
        }
        break;
      }
      case OP_DRAWBMP: {
        renderBMP(evaluateValueToSV(expandVars(getPooledString(inst.p1)), 0).toString().c_str(), 
                  evaluateValueToSV(expandVars(getPooledString(inst.p2)), 0).toInt(),
                  evaluateValueToSV(expandVars(getPooledString(inst.p3)), 0).toInt(), holdingVgaLock);
        break;
      }
      case OP_PROGRESSBAR: {
        int x = evaluateValueToSV(expandVars(getPooledString(inst.p1)), 0).toInt();
        int y = evaluateValueToSV(expandVars(getPooledString(inst.p2)), 0).toInt();
        int w = evaluateValueToSV(expandVars(getPooledString(inst.p3)), 0).toInt();
        int pct = evaluateValueToSV(expandVars(getPooledString(inst.p4)), 0).toInt();
        videodisplay.drawRect(x,y,w,10,themeColorText);
        videodisplay.fillRect(x+2,y+2,(w-4)*pct/100,6,themeColorAccent);
        break;
      }

      // --- 3. Variables & Maths ---
      case OP_SET: {
        // SET artik tamamen PTAG-aware. p2 bir sayi, register veya pool offset olabilir.
        scriptVariables[inst.p1] = resolveParam(inst.p2);
        break;
      }
      case OP_MATH: {
        int vid = inst.p1;
        // Eski MATH artik Turbo PTAG_INT ile cok daha hizli
        ScriptValue res = resolveParam(inst.p2);
        scriptVariables[vid] = res;
        break;
      }
      case OP_ADD:
      case OP_SUB:
      case OP_MUL:
      case OP_DIV:
      case OP_MOD: {
        int vid = inst.p1;
        ScriptValue& tgt = scriptVariables[vid];
        ScriptValue val = resolveParam(inst.p2);
        float a = tgt.toFloat(), b = val.toFloat();
        float r = 0;
        if(inst.op==OP_ADD) r=a+b; else if(inst.op==OP_SUB) r=a-b; else if(inst.op==OP_MUL) r=a*b; 
        else if(inst.op==OP_DIV) r=(b!=0?a/b:0); else if(inst.op==OP_MOD) r=fmod(a,b);
        ScriptValue sv; if(tgt.type==VAL_FLOAT||val.type==VAL_FLOAT) sv.setFloat(r); else sv.setInt((int)r);
        scriptVariables[vid] = sv;
        break;
      }
      case OP_INC: { scriptVariables[inst.p1].setInt(scriptVariables[inst.p1].toInt() + 1); break; }
      case OP_DEC: { scriptVariables[inst.p1].setInt(scriptVariables[inst.p1].toInt() - 1); break; }
      case OP_RANDOM: {
        int minv = resolveParam(inst.p1).toInt();
        int maxv = resolveParam(inst.p2).toInt();
        int res = (maxv > minv) ? (minv + (rand() % (maxv - minv + 1))) : minv;
        scriptVariables[getVariableId("math_result", true)].setInt(res);
        break;
      }
      case OP_SEED: srand(resolveParam(inst.p1).toInt()); break;
      case OP_SQRT: scriptVariables[getVariableId("math_result", true)].setFloat(sqrt(resolveParam(inst.p1).toFloat())); break;
      case OP_POW: {
        float a = evaluateValueToSV(getPooledString(inst.p1), 0).toFloat();
        float b = evaluateValueToSV(getPooledString(inst.p2), 0).toFloat();
        setScriptVariableByName("math_result", String(pow(a,b)));
        break;
      }
      case OP_SIN:  setScriptVariableByName("math_result", String(sin(evaluateValueToSV(getPooledString(inst.p1), 0).toFloat()))); break;
      case OP_COS:  setScriptVariableByName("math_result", String(cos(evaluateValueToSV(getPooledString(inst.p1), 0).toFloat()))); break;
      case OP_TAN:  setScriptVariableByName("math_result", String(tan(evaluateValueToSV(getPooledString(inst.p1), 0).toFloat()))); break;
      case OP_ABS:  setScriptVariableByName("math_result", String(fabs(evaluateValueToSV(getPooledString(inst.p1), 0).toFloat()))); break;
      case OP_ATAN2: {
        float y = evaluateValueToSV(getPooledString(inst.p1), 0).toFloat();
        float x = evaluateValueToSV(getPooledString(inst.p2), 0).toFloat();
        setScriptVariableByName("math_result", String(atan2(y,x)));
        break;
      }
      case OP_VEC2_ADD:
      case OP_VEC2_SUB: {
        String p = expandVars(getPooledString(inst.p1)); char res[24], v1[24], v2[24];
        if(sscanf(p.c_str(),"%s %s %s", res, v1, v2)>=3) {
          float x1 = getScriptVariableByName(String(v1)+"_x").toFloat();
          float y1 = getScriptVariableByName(String(v1)+"_y").toFloat();
          float x2 = getScriptVariableByName(String(v2)+"_x").toFloat();
          float y2 = getScriptVariableByName(String(v2)+"_y").toFloat();
          if(inst.op==OP_VEC2_ADD) {
            setScriptVariableByName(String(res)+"_x", String(x1+x2));
            setScriptVariableByName(String(res)+"_y", String(y1+y2));
          } else {
            setScriptVariableByName(String(res)+"_x", String(x1-x2));
            setScriptVariableByName(String(res)+"_y", String(y1-y2));
          }
        }
        break;
      }
      case OP_VEC2_MUL: {
        String p = expandVars(getPooledString(inst.p1)); char res[24], v[24]; float s;
        if(sscanf(p.c_str(),"%s %s %f", res, v, &s)>=3) {
           setScriptVariable(String(res)+"_x", String(getScriptVariable(String(v)+"_x").toFloat()*s));
           setScriptVariable(String(res)+"_y", String(getScriptVariable(String(v)+"_y").toFloat()*s));
        }
        break;
      }
      case OP_VEC2_DOT: {
        String p = expandVars(getPooledString(inst.p1)); char res[24], v1[24], v2[24];
        if(sscanf(p.c_str(),"%s %s %s", res, v1, v2)>=3) {
           float x1=getScriptVariable(String(v1)+"_x").toFloat(), y1=getScriptVariable(String(v1)+"_y").toFloat();
           float x2=getScriptVariable(String(v1)+"_x").toFloat(), y2=getScriptVariable(String(v1)+"_y").toFloat();
           setScriptVariable(res, String(x1*x2 + y1*y2));
        }
        break;
      }

      case OP_TRY: {
         // Simply proceed inside Try. If throw occurs, it'll jump to CATCH.
         break;
      }
      case OP_CATCH: {
        // If we fall into CATCH normally (without throwing/error), skip to endtry
        int depth=1;
        while(scriptCurrentLine < scriptLineCount-1 && depth>0) {
          scriptCurrentLine++; 
          uint8_t nOp = scriptProgram[scriptCurrentLine].op;
          if(nOp==OP_TRY) depth++; 
          else if(nOp==OP_ENDTRY) depth--;
        }
        break;
      }
      case OP_ENDTRY: break;
      case OP_THROW: {
         logToFile("[TURBO-ERROR] " + expandVars(getPooledString(inst.p1)));
         // Find nearest CATCH
         int depth=1;
         while(scriptCurrentLine < scriptLineCount-1 && depth>0) {
            scriptCurrentLine++; uint8_t nOp = scriptProgram[scriptCurrentLine].op;
            if(nOp==OP_CATCH && depth==1) break;
         }
         break;
      }
      case OP_PROFILE: {
         static unsigned long pTime = 0;
         if(pTime == 0) pTime = micros();
         else {
           logToFile("[PROFILE] " + expandVars(getPooledString(inst.p1)) + ": " + String(micros()-pTime) + "us");
           pTime = 0;
         }
         break;
      }

      // case OP_RANDOM (Duplicate removed)
      case OP_SFX: {
         // Concurrent SFX (Simplified: just play but don't stop current BGM if possible)
         audioPlay(expandVars(getPooledString(inst.p1)));
         break;
      }
      case OP_WSS_CONNECT: {
         logToFile("[WSS] Connecting to " + expandVars(getPooledString(inst.p1)));
         setScriptVariableByName("wss_status", "connected"); // Stub
         break;
      }
      case OP_WSS_SEND: {
         logToFile("[WSS] Sending: " + expandVars(getPooledString(inst.p1)));
         break;
      }
      // --- Remaining math (ABS/ROUND/MIN/MAX handled above in optimized form, remove duplicate cases) ---

      // --- 4. String Ops ---
      case OP_CONCAT: {
        String p = getPooledString(inst.p1); int sp = p.indexOf(' ');
        if(sp != -1) {
          String vn = p.substring(0, sp);
          String appended = evaluateValue(expandVars(p.substring(sp+1)));
          ScriptValue cur = getScriptVariableByName(vn);
          setScriptVariableByName(vn, cur.toString() + appended);
        }
        break;
      }
      case OP_STRLEN: {
        String vname = getPooledString(inst.p1);
        String s = getScriptVariableByName(vname).toString();
        setScriptVariableByName("strlen_result", String(s.length()));
        break;
      }
      case OP_SUBSTRING: {
        String p = expandVars(getPooledString(inst.p1)); char vn[31]; int b, l;
        if(sscanf(p.c_str(),"%30s %d %d", vn, &b, &l) >= 3) {
          String s = getScriptVariableByName(vn).toString();
          setScriptVariableByName("substring_result", s.substring(b, b+l));
        }
        break;
      }
      case OP_TOINT: {
        String v = getPooledString(inst.p1); v.trim();
        String val = getScriptVariableByName(v).toString();
        setScriptVariableByName(v, String((int)evaluateValue(val).toFloat()));
        break;
      }
      case OP_TOUPPER: {
        String v = getPooledString(inst.p1);
        String s = getScriptVariableByName(v).toString(); s.toUpperCase();
        setScriptVariableByName(v, s); break;
      }
      case OP_TOLOWER: {
        String v = getPooledString(inst.p1);
        String s = getScriptVariableByName(v).toString(); s.toLowerCase();
        setScriptVariableByName(v, s); break;
      }

      // --- 5. Flow Control ---
      case OP_GOTO: 
        if(inst.p1 >= 0) { scriptCurrentLine = inst.p1; continue; }
        else { Serial.println("[VM-ERR] Goto undefined label"); break; }
      case OP_GOSUB:
        if (inst.p1 >= 0 && gosubTop < MAX_GOSUB_DEPTH - 1) { gosubStack[++gosubTop] = scriptCurrentLine + 1; scriptCurrentLine = inst.p1; continue; }
        else { Serial.println("[VM-ERR] Gosub undefined label or stack full"); break; }
      case OP_RETURN: if (gosubTop >= 0) { scriptCurrentLine = gosubStack[gosubTop--]; continue; } break;
      case OP_WAIT: {
        int ms = resolveParam(inst.p1).toInt();
        if (holdingVgaLock) { xSemaphoreGive(systemMutex); holdingVgaLock = false; }
        if (ms <= 0) { vTaskDelay(1); break; }
        vTaskDelay(pdMS_TO_TICKS(ms));
        break;
      }
      case OP_YIELD: vTaskDelay(1); break;
      case OP_IF:
      case OP_ELSEIF: {
        bool res = evalCondition(getPooledString(inst.p1));
        if(!res) {
          if (inst.p2 > 0) {
            // Block IF: Jump to ELSE/ENDIF
            scriptCurrentLine = inst.p2 - 1; 
          } else if (inst.p2 == -2) {
            // Single-line IF: Condition false, nothing to do, just proceed
          }
        } else {
          if (inst.p2 == -2) {
            // Single-line IF: Condition true, execute action (p3)
            String action = getPooledString(inst.p3);
            executeCommandInline(action, lineNumber);
          }
        }
        break;
      }
      case OP_ELSE: {
        if (inst.p2 > 0) scriptCurrentLine = inst.p2 - 1; // Jump to ENDIF
        break;
      }
      case OP_ENDIF: break;

      case OP_WHILE: {
        bool res = evalCondition(getPooledString(inst.p1));
        if (res) {
           if (loopStackTop < 0 || loopStack[loopStackTop].startLine != scriptCurrentLine) {
              if (loopStackTop < MAX_LOOP_DEPTH - 1) {
                 LoopInfo li; li.startLine = scriptCurrentLine; li.endLine = inst.p2;
                 li.counterVarId = -1; li.counterStart = 0; li.counterEnd = 0; li.counterStep = 0;
                 loopStack[++loopStackTop] = li;
              }
           }
        } else {
           if (loopStackTop >= 0 && loopStack[loopStackTop].startLine == scriptCurrentLine) loopStackTop--;
           if (inst.p2 > 0) scriptCurrentLine = inst.p2;
        }
        break;
      }
      case OP_WEND: if(loopStackTop>=0) { scriptCurrentLine = loopStack[loopStackTop].startLine - 1; } break;

      case OP_FOR: {
        if (loopStackTop >= 0 && loopStack[loopStackTop].startLine == scriptCurrentLine) break; 
        int vid = inst.p1;
        float start = resolveParam(inst.p2).toFloat();
        float end   = resolveParam(inst.p3).toFloat();
        float step  = resolveParam(inst.p4).toFloat();
        int jumpToNext = inst.p5;
        scriptVariables[vid].setFloat(start);
        if(loopStackTop < MAX_LOOP_DEPTH-1) {
          loopStack[++loopStackTop] = { scriptCurrentLine, jumpToNext, vid, start, end, step };
        }
        if((step > 0 && start > end) || (step < 0 && start < end)) { 
          if (jumpToNext > 0) scriptCurrentLine = jumpToNext;
          if (loopStackTop >= 0) loopStackTop--;
        }
        break;
      }
      case OP_NEXT:
        if(loopStackTop>=0 && loopStack[loopStackTop].counterVarId != -1) {
          LoopInfo& li = loopStack[loopStackTop];
          ScriptValue v = scriptVariables[li.counterVarId];
          float cur = v.toFloat() + li.counterStep;
          if (fabs(cur - roundf(cur)) < 0.001f) v.setInt((int)roundf(cur));
          else v.setFloat(cur);
          scriptVariables[li.counterVarId] = v;
          if((li.counterStep > 0 && cur <= li.counterEnd) || (li.counterStep < 0 && cur >= li.counterEnd)) {
            scriptCurrentLine = li.startLine; 
            continue;
          } else {
            loopStackTop--;
          }
        }
        break;


      // --- 6. Hardware & IO ---
      case OP_GETKEY: {
        int vid = inst.p1;
        extern unsigned long scriptStartTime;
        if (millis() - scriptStartTime < 400) {
           ScriptValue sv; sv.setString(""); scriptVariables[vid] = sv;
           scriptKeyAvailable = false;
           scriptKeyBuffer = "";
        } else {
           if (scriptKeyAvailable) {
              setScriptValue(vid, scriptKeyBuffer);
              scriptKeyAvailable = false;
              scriptKeyBuffer = "";
           } else {
              ScriptValue sv; sv.setString(""); scriptVariables[vid] = sv;
           }
        }
        break;
      }
      case OP_ISKEYDOWN: {
        int hid = resolveParam(inst.p1).toInt();
        int vid = inst.p2; // Target variable
        ScriptValue sv;
        if (hid >= 0 && hid < 256) sv.setFloat(heldKeys[hid] ? 1 : 0);
        else sv.setFloat(0);
        scriptVariables[vid] = sv;
        break;
      }
      case OP_LASTKEY: {
        int vid = inst.p1;
        extern unsigned long scriptStartTime;
        if (millis() - scriptStartTime < 400) {
          scriptVariables[vid].setFloat(0);
          lastKeyDownHID = 0;
        } else {
          ScriptValue sv; sv.setFloat((float)lastKeyDownHID);
          scriptVariables[vid] = sv;
          lastKeyDownHID = 0; // Clear after reading to prevent repeating trigger
        }
        break;
      }
      case OP_GETKEYEVENT: {
        int type_vid = inst.p1;
        int hid_vid  = inst.p2;
        extern unsigned long scriptStartTime;
        if (millis() - scriptStartTime < 400) {
          scriptVariables[type_vid].setFloat(0);
          scriptVariables[hid_vid].setFloat(0);
          keyEventAvailable = false;
        } else {
          if (keyEventAvailable) {
            // Basit varsayım: en son olay gelsin.
            // Karmaşık olsa kuyruk gerekirdi ama polling için yeterli.
            scriptVariables[type_vid].setFloat(heldKeys[lastKeyDownHID] ? 1 : 2); // 1=Down, 2=Up
            scriptVariables[hid_vid].setFloat((float)(heldKeys[lastKeyDownHID] ? lastKeyDownHID : lastKeyUpHID));
            keyEventAvailable = false;
          } else {
            scriptVariables[type_vid].setFloat(0); // 0 = olay yok
            scriptVariables[hid_vid].setFloat(0);
          }
        }
        break;
      }

      case OP_GETGAMEPAD: {
        int player = resolveParam(inst.p1).toInt();
        String btn = resolveParam(inst.p2).toString();
        int vid = inst.p3;
        int pIdx = player - 1;
        bool state = false;
        if (pIdx >= 0 && pIdx < 2) {
          int btnIdx = -1;
          if      (btn == "UP")       btnIdx = 0;
          else if (btn == "DOWN")     btnIdx = 1;
          else if (btn == "LEFT")     btnIdx = 2;
          else if (btn == "RIGHT")    btnIdx = 3;
          else if (btn == "X" || btn == "A") btnIdx = 4;
          else if (btn == "TRIANGLE" || btn == "Y") btnIdx = 5;
          else if (btn == "CIRCLE" || btn == "B")   btnIdx = 6;
          else if (btn == "SQUARE" || btn == "X_BOX")   btnIdx = 7;
          else if (btn == "L1")       btnIdx = 8;
          else if (btn == "R1")       btnIdx = 9;
          else if (btn == "L2")       btnIdx = 10;
          else if (btn == "R2")       btnIdx = 11;
          else if (btn == "SELECT")   btnIdx = 12;
          else if (btn == "START")    btnIdx = 13;
          if (btnIdx >= 0 && btnIdx < 14) {
            state = gamepadButtons[pIdx][btnIdx];
          }
        }
        ScriptValue sv; sv.setFloat(state ? 1.0f : 0.0f);
        scriptVariables[vid] = sv;
        break;
      }

      case OP_GETGAMEPADANALOG: {
        int player = resolveParam(inst.p1).toInt();
        String axis = resolveParam(inst.p2).toString();
        int vid = inst.p3;
        int pIdx = player - 1;
        int16_t val = 0;
        if (pIdx >= 0 && pIdx < 2) {
          int axIdx = -1;
          if      (axis == "LX") axIdx = 0;
          else if (axis == "LY") axIdx = 1;
          else if (axis == "RX") axIdx = 2;
          else if (axis == "RY") axIdx = 3;
          if (axIdx >= 0 && axIdx < 4) {
            val = gamepadAnalog[pIdx][axIdx];
          }
        }
        ScriptValue sv; sv.setFloat((float)val);
        scriptVariables[vid] = sv;
        break;
      }

      case OP_TONE: {
        String p=expandVars(getPooledString(inst.p1)); int fr=440, du=100;
        sscanf(p.c_str(), "%d %d", &fr, &du);
        // Non-blocking: LEDC PWM tabanlı async bip
        audioBeep(fr, du);
        break;
      }
      case OP_GETMOUSE: {
        if(inst.p1) setScriptValue(getVariableId(getPooledString(inst.p1), true), String(mouseX));
        if(inst.p2) setScriptValue(getVariableId(getPooledString(inst.p2), true), String(mouseY));
        if(inst.p3) setScriptValue(getVariableId(getPooledString(inst.p3), true), leftButton ? "1" : "0");
        if(inst.p4) setScriptValue(getVariableId(getPooledString(inst.p4), true), rightButton ? "1" : "0");
        if(inst.p5) setScriptValue(getVariableId(getPooledString(inst.p5), true), middleButton ? "1" : "0");
        break;
      }

      case OP_GETSCREEN: {
        if(inst.p1) setScriptValue(getVariableId(getPooledString(inst.p1), true), String(SCREEN_WIDTH));
        if(inst.p2) setScriptValue(getVariableId(getPooledString(inst.p2), true), String(SCREEN_HEIGHT));
        if(vmDebugActive) Serial.printf("[VM-GFX] Screen Info: %dx%d\n", SCREEN_WIDTH, SCREEN_HEIGHT);
        break;
      }
      // --- 7. WiFi & Network ---
      case OP_WIFIBEGIN: {
        String p=expandVars(getPooledString(inst.p1)); char s[32], pw[32];
        if(sscanf(p.c_str(),"%s %s", s, pw)>=2) WiFi.begin(s, pw);
        break;
      }
      case OP_WIFIDISCONNECT: WiFi.disconnect(); break;
      case OP_WIFISTATUS: {
        int st = WiFi.status(); setScriptVariableByName("wifistatus", (st==WL_CONNECTED)?"connected":"disconnected");
        break;
      }
      case OP_GETIP:  setScriptVariableByName("getip_result", WiFi.localIP().toString()); break;
      case OP_GETMAC: setScriptVariableByName("getmac_result", WiFi.macAddress()); break;
      case OP_GETGW:  setScriptVariableByName("getgw_result", WiFi.gatewayIP().toString()); break;
      case OP_PING: {
        String host = expandVars(getPooledString(inst.p1));
        bool ok = manualPing(host.c_str()); 
        setScriptVariableByName("ping_result", ok ? "1" : "0");
        break;
      }
      case OP_HTTPGET: {
        if (WiFi.status() != WL_CONNECTED) {
          setScriptVariableByName("httpget_status", "-1");
          setScriptVariableByName("http_result", "Error: No WiFi");
          break;
        }
        HTTPClient h; h.begin(expandVars(getPooledString(inst.p1))); int c=h.GET();
        if(c==200) setScriptVariableByName("http_result", h.getString());
        setScriptVariableByName("httpget_status", String(c)); h.end();
        break;
      }
      case OP_HTTPPOST: {
        String p=getPooledString(inst.p1); int sp=p.indexOf(' ');
        if(sp!=-1) {
          HTTPClient h; 
          h.begin(expandVars(p.substring(0,sp)));
          h.addHeader("Content-Type", "application/json");
          
          // Auto-attach api_key if defined in EyuScript variables
          ScriptValue apiKeyVal = getScriptVariableByName("api_key");
          if (apiKeyVal.type != VAL_NULL && apiKeyVal.toString().length() > 0) {
              h.addHeader("Authorization", apiKeyVal.toString());
          }

          int c=h.POST(expandVars(p.substring(sp+1)));
          if(c==200) setScriptVariableByName("http_result", h.getString());
          setScriptVariableByName("httppost_status", String(c)); h.end();
        }
        break;
      }
      case OP_JSONPARSE: {
        String data = getScriptVariableByName("http_result").toString();
        DeserializationError err = deserializeJson(lastJson, data);
        setScriptVariableByName("json_status", err ? "error" : "success");
        break;
      }
      case OP_JSONGET: {
        String p=expandVars(getPooledString(inst.p1)); int sp=p.indexOf(' ');
        if(sp!=-1) {
          String path = p.substring(0,sp), var = p.substring(sp+1);
          setScriptVariableByName(var, lastJson[path].as<String>());
        }
        break;
      }

      // --- 8. File System ---
      case OP_FILEREAD: {
        String path = expandVars(getPooledString(inst.p1));
        if(xSemaphoreTake(sdMutex, pdMS_TO_TICKS(5000))==pdTRUE) {
          File f = SD.open(path);
          if(f) { setScriptVariableByName("fileread_result", f.readString()); f.close(); }
          xSemaphoreGive(sdMutex);
        }
        break;
      }
      case OP_FILEWRITE:
      case OP_FILEAPPEND: {
        String p=getPooledString(inst.p1); int sp=p.indexOf(' ');
        if(sp!=-1) {
          String path=expandVars(p.substring(0,sp)), data=expandVars(p.substring(sp+1));
          if(xSemaphoreTake(sdMutex, pdMS_TO_TICKS(5000))==pdTRUE) {
            File f = SD.open(path, (inst.op==OP_FILEWRITE)?FILE_WRITE:FILE_APPEND);
            if(f) { f.print(data); f.close(); } xSemaphoreGive(sdMutex);
          }
        }
        break;
      }
      case OP_FILESIZE: {
        String path = expandVars(getPooledString(inst.p1));
        if(xSemaphoreTake(sdMutex, pdMS_TO_TICKS(5000))==pdTRUE) {
          File f = SD.open(path);
          setScriptVariableByName("filesize_result", f ? String(f.size()) : "-1");
          if(f) f.close(); xSemaphoreGive(sdMutex);
        }
        break;
      }
      case OP_FILEFIND: {
        String pattern = expandVars(getPooledString(inst.p1));
        String result = "";
        if(xSemaphoreTake(sdMutex, pdMS_TO_TICKS(5000))==pdTRUE) {
          File root = SD.open("/");
          File file = root.openNextFile();
          while(file) {
            if(String(file.name()).indexOf(pattern)!=-1) { result += String(file.name()) + "\n"; }
            file = root.openNextFile();
          }
          setScriptVariableByName("filefind_result", result);
          xSemaphoreGive(sdMutex);
        }
        break;
      }

      // --- 9. GPIO ---
      case OP_PINMODE: {
        String p = expandVars(getPooledString(inst.p1)); int pin; char mode[16];
        if(sscanf(p.c_str(),"%d %15s", &pin, mode)>=2) {
          int m = INPUT; if(strcmp(mode,"OUTPUT")==0) m=OUTPUT; else if(strcmp(mode,"INPUT_PULLUP")==0) m=INPUT_PULLUP;
          pinMode(pin, m);
        }
        break;
      }
      case OP_DIGITALWRITE: {
        String p = expandVars(getPooledString(inst.p1)); int pin; char val[8];
        if(sscanf(p.c_str(),"%d %7s", &pin, val)>=2) {
          int v = (strcmp(val,"HIGH")==0 || strcmp(val,"1")==0) ? HIGH : LOW;
          digitalWrite(pin, v);
        }
        break;
      }
      case OP_DIGITALREAD: {
        int pin = evaluateValue(expandVars(getPooledString(inst.p1))).toInt();
        setScriptVariableByName("lastread", String(digitalRead(pin)));
        break;
      }
      case OP_ANALOGREAD: {
        int pin = evaluateValue(expandVars(getPooledString(inst.p1))).toInt();
        setScriptVariableByName("lastread", String(analogRead(pin)));
        break;
      }

      case OP_ONGPIO: {
        String p = expandVars(getPooledString(inst.p1)); int pin; char lbl[32];
        if(sscanf(p.c_str(), "%d %31s", &pin, lbl)>=2) {
          int line = findScriptLabel(lbl);
          if (line != -1 && gpioIntCount < 4) {
            registeredInterrupts[gpioIntCount] = {pin, line, false};
            pinMode(pin, INPUT_PULLUP);
            attachInterruptArg(pin, gpio_isr_handler, (void*)gpioIntCount, FALLING);
            logToFile("[VM] HW Interrupt Attached: Pin " + String(pin) + " -> " + lbl);
            gpioIntCount++;
          }
        }
        break;
      }

      // --- 10. Arrays ---
      case OP_ARRAY: {
        String p = expandVars(getPooledString(inst.p1));
        char name[24]; int size=0, width=0;
        if (scriptArrayCount < MAX_SCRIPT_ARRAYS) {
          int n = sscanf(p.c_str(), "%s %d %d", name, &size, &width);
          strncpy(scriptArrays[scriptArrayCount].name, name, 23);
          scriptArrays[scriptArrayCount].size = (size > 0 && size <= MAX_ARRAY_SIZE) ? size : 0;
          scriptArrays[scriptArrayCount].width = width;
          scriptArrayCount++;
        }
        break;
      }
      case OP_ASET: {
        // p1=array_name, p2=index_expr, p3=value_expr
        const char* name = getPooledString(inst.p1);
        int idx = resolveParam(inst.p2).toInt();
        ScriptArray* a = findArrayCached(name);
        if (a && idx >= 0 && idx < MAX_ARRAY_SIZE) {
          // Sayısal literal (PTAG_INT) desteği ile çökme engellendi 🛡️
          a->values[idx] = (inst.p3 != -1 && inst.p3 != 0) ? resolveParam(inst.p3) : ScriptValue();
          if (idx >= a->size) a->size = idx + 1;
        }
        break;
      }
      case OP_AGET: {
        const char* p1Str = getPooledString(inst.p1);
        const char* p2Str = getPooledString(inst.p2);
        
        if (inst.p3 == -1 || inst.p3 == 0) {
            // 2 Parameters: aget array index -> math_result
            int idx = resolveParam(inst.p2).toInt();
            ScriptArray* a = findArrayCached(p1Str);
            if (a && idx >= 0 && idx < a->size) {
                scriptVariables[getVariableId("math_result", true)] = a->values[idx];
            }
        } else {
            // 3 Parameters: aget var array index
            int idx = resolveParam(inst.p3).toInt();
            ScriptArray* a = findArrayCached(p2Str);
            if (a && idx >= 0 && idx < a->size) {
                // 🔥 Hantal string çevirileri iptal! Doğrudan objeyi ata (Sıfır RAM Sızıntısı)
                int targetVarId = getVariableId(p1Str, true);
                if (targetVarId != -1) {
                    scriptVariables[targetVarId] = a->values[idx];
                }
            }
        }
        break;
      }
      case OP_ASET2D: {
        String p = expandVars(getPooledString(inst.p1));
        char name[24]; char valExpr[64]; int x, y;
        if(sscanf(p.c_str(), "%s %d %d %[^\n]", name, &x, &y, valExpr)>=4) {
          ScriptArray* a = findArray(name);
          if(a && a->width > 0) {
            int idx = y * a->width + x;
            if(idx >= 0 && idx < MAX_ARRAY_SIZE) {
              a->values[idx] = evaluateValueToSV(valExpr, 0);
              if(idx >= a->size) a->size = idx + 1;
            }
          }
        }
        break;
      }
      case OP_AGET2D: {
        String p = expandVars(getPooledString(inst.p1));
        char name[24], varName[24]; int x, y;
        if(sscanf(p.c_str(), "%s %d %d %s", name, &x, &y, varName)>=4) {
          ScriptArray* a = findArray(name);
          if(a && a->width > 0) {
            int idx = y * a->width + x;
            if(idx >= 0 && idx < a->size) setScriptVariableByName(varName, a->values[idx].toString());
            else setScriptVariableByName(varName, "0");
          }
        }
        break;
      }
      case OP_FLOOR: {
        String p = expandVars(getPooledString(inst.p1)); int spIdx = p.indexOf(' ');
        if(spIdx != -1) {
          String varN = p.substring(0, spIdx);
          float val = evaluateValue(expandVars(p.substring(spIdx+1))).toFloat();
          setScriptVariableByName(varN, String((int)floor(val)));
        } else {
          float val = getScriptVariableByName(p).toFloat();
          setScriptVariableByName(p, String((int)floor(val)));
        }
        break;
      }
      case OP_CEIL: {
        String p = expandVars(getPooledString(inst.p1)); int spIdx = p.indexOf(' ');
        if(spIdx != -1) {
          String varN = p.substring(0, spIdx);
          float val = evaluateValue(expandVars(p.substring(spIdx+1))).toFloat();
          setScriptVariableByName(varN, String((int)ceil(val)));
        } else {
          float val = getScriptVariableByName(p).toFloat();
          setScriptVariableByName(p, String((int)ceil(val)));
        }
        break;
      }

      case OP_ALEN: {
        String p = expandVars(getPooledString(inst.p1)); int sp = p.indexOf(' ');
        if(sp != -1) {
          String name = p.substring(0, sp); name.trim();
          String var  = p.substring(sp+1);  var.trim();
          ScriptArray* a = findArray(name.c_str());
          setScriptVariableByName(var, a ? String(a->size) : "0");
        }
        break;
      }
      case OP_AUDIO: {
        String p = expandVars(getPooledString(inst.p1));
        if(p=="STOP") audioStop(); else if(p=="PAUSE") audioPause();
        else if(p.startsWith("VOLUME ")) audioSetVolume(p.substring(7).toInt());
        else if(p=="STATUS") setScriptVariableByName("audio_result", audioStatus());
        break;
      }
      case OP_PLAY: audioPlay(expandVars(getPooledString(inst.p1))); break;
      
      // --- 12. IPC ---
      case OP_IPC: {
        String p = expandVars(getPooledString(inst.p1));
        int sp = p.indexOf(' ');
        if(sp != -1) {
          String key = p.substring(0, sp), val = p.substring(sp+1);
          setScriptVariableByName(key, val);
        }
        break;
      }

      // --- 13. UI Helpers ---
      case OP_BUTTON: {
        String p = expandVars(getPooledString(inst.p1)); int x,y,w,h; char label[32];
        if(sscanf(p.c_str(),"%d %d %d %d %s", &x, &y, &w, &h, label)>=5) {
          xSemaphoreTake(systemMutex, portMAX_DELAY);
          videodisplay.fillRect(x,y,w,h,themeColorAccent);
          videodisplay.setTextColor(themeColorBG); videodisplay.setCursor(x+5,y+5); videodisplay.print(label);
          xSemaphoreGive(systemMutex);
          if(leftButton && mouseX>=x && mouseX<=x+w && mouseY>=y && mouseY<=y+h) {
             setScriptVariableByName("button_click", "1"); setScriptVariableByName("button_name", String(label));
          } else setScriptVariableByName("button_click", "0");
        }
        break;
      }

      case OP_SETTEXT: {
        String p = getPooledString(inst.p1);
        int sz=1, fnt=0; 
        if(sscanf(p.c_str(), "%d %d", &sz, &fnt)>=1) {
           videodisplay.setTextSize(sz);
        }
        break;
      }
      
      case OP_LOADSPRITE: {
        int id = evaluateValueToSV(getPooledString(inst.p1), 0).toInt();
        String path = evaluateValueToSV(getPooledString(inst.p2), 0).toString();
        if (id >= 0 && id < MAX_SPRITES && sdCardPresent) {
          freeSprite(id);
          File f = SD.open(path);
          if (f) {
            f.seek(18); int32_t w, h; f.read((uint8_t*)&w, 4); f.read((uint8_t*)&h, 4);
            f.seek(10); int32_t offset; f.read((uint8_t*)&offset, 4); f.seek(offset);
            int absW = abs(w), absH = abs(h);
            uint8_t* data = (uint8_t*)heap_caps_malloc(absW * absH, MALLOC_CAP_SPIRAM);
            if (data) {
              size_t rowSize = (absW * 3 + 3) & ~3;
              uint8_t* scanline = (uint8_t*)malloc(rowSize);
              if (scanline) {
                for (int j = 0; j < absH; j++) {
                  f.read(scanline, rowSize);
                  int py = (h > 0) ? (absH - 1 - j) : j;
                  for (int i = 0; i < absW; i++) {
                    uint8_t b=scanline[i*3], g=scanline[i*3+1], r=scanline[i*3+2];
                    data[py * absW + i] = videodisplay.base.rgb(r, g, b);
                  }
                }
                free(scanline);
                globalSprites[id] = { data, (uint16_t)absW, (uint16_t)absH, true };
                logToFile("[VM] Sprite " + String(id) + " loaded: " + String(absW) + "x" + String(absH));
              } else free(data);
            }
            f.close();
          }
        }
        break;
      }
      case OP_DRAWSPRITE: {
        int id = evaluateValueToSV(getPooledString(inst.p1), 0).toInt();
        int x  = evaluateValueToSV(getPooledString(inst.p2), 0).toInt();
        int y  = evaluateValueToSV(getPooledString(inst.p3), 0).toInt();
        if (vmDebugActive) Serial.printf("[VM] DRAWSPRITE id=%d at %d,%d\n", id, x, y);
        if (id >= 0 && id < MAX_SPRITES && globalSprites[id].loaded) {
          Sprite& s = globalSprites[id];
          uint8_t* fb = (vgaInitialized && videodisplay.base.backBuffer)
                          ? (uint8_t*)videodisplay.base.backBuffer : nullptr;
          for (int j = 0; j < s.h; j++) {
            int py = y + j;
            if (py < 0 || py >= SCREEN_HEIGHT) continue;
            for (int i = 0; i < s.w; i++) {
              int px = x + i;
              if (px < 0 || px >= SCREEN_WIDTH) continue;
              uint8_t color = s.data[j * s.w + i];
              if (color != 0x00) {
                // Güvenli blit + GFX fallback
                if (fb) fb[py * SCREEN_WIDTH + px] = color;
                else videodisplay.drawPixel(px, py, (uint16_t)color); // RGB332 fallback
              }
            }
            if (j % 32 == 0) esp_task_wdt_reset();
          }
        }
        break;
      }
      
      case OP_NOP: break; // Normal: bos satir, yorum veya etiket

      // ── TURBO Register Opcode Handlers ──────────────────────────────
      case OP_RMOV: {
        int rd = inst.p1 & 0x3FF; // Unpack register index
        if (rd < 0 || rd >= VM_REGISTER_COUNT) {
          vmGSOD("RMOV: Gecersiz dest register R" + String(rd), lineNumber); break;
        }
        if (inst.p3 == -1) {
          // rload path
          ScriptValue val = resolveParam(inst.p2);
          vmReg(rd) = val;
        } else {
          // rmov path
          int rs = inst.p2 & 0x3FF; 
          if (rs < 0 || rs >= VM_REGISTER_COUNT) {
            vmGSOD("RMOV: Gecersiz src register R" + String(rs), lineNumber); break;
          }
          vmReg(rd) = vmReg(rs);
        }
        break;
      }
      case OP_RADD:
      case OP_RSUB:
      case OP_RMUL:
      case OP_RDIV: {
        int rd = inst.p1 & 0x3FF; 
        int ra = inst.p2 & 0x3FF;
        int rb = inst.p3 & 0x3FF;
        
        if (rd < 0 || rd >= VM_REGISTER_COUNT ||
            ra < 0 || ra >= VM_REGISTER_COUNT ||
            rb < 0 || rb >= VM_REGISTER_COUNT) {
          vmGSOD("Register erisimi sinir disi", lineNumber);
          break;
        }
        float a = vmReg(ra).toFloat();
        float b = vmReg(rb).toFloat();
        if (inst.op == OP_RDIV && b == 0.0f) {
          vmGSOD("RDIV: Sifira bolme! R" + String(rb) + "=0", lineNumber);
          break;
        }
        float result = 0;
        switch(inst.op) {
          case OP_RADD: result = a + b; break;
          case OP_RSUB: result = a - b; break;
          case OP_RMUL: result = a * b; break;
          case OP_RDIV: result = a / b; break;
          default: break;
        }
        bool useFloat = (vmReg(ra).type == VAL_FLOAT || vmReg(rb).type == VAL_FLOAT
                         || inst.op == OP_RDIV);
        if (useFloat) {
          vmReg(rd).setFloat(result);
        } else {
          vmReg(rd).setInt((int32_t)result);
        }
        break;
      }

      // ── TURBO Grafik Register Handler'ları ─────────────────────────
      // evaluateValueToSV YOK — sadece register okuma, nano-saniye hızında
      case OP_RPIXEL: {
        // p1=X_reg, p2=Y_reg, p3=Color_reg (-1=themeAccent)
        int x = vmReg(inst.p1 & 0x3FF).toInt();
        int y = vmReg(inst.p2 & 0x3FF).toInt();

        uint16_t c = themeColorAccent;
        if (inst.p3 != -1 && inst.p3 != 0) {
          c = (uint16_t)vmReg(inst.p3 & 0x3FF).toInt();
        }

        // Güvenli GFX Çizimi (Korumalı) 🛡️
        if (holdingVgaLock || xSemaphoreTake(systemMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            videodisplay.drawPixel(x, y, c);
            if (!holdingVgaLock) xSemaphoreGive(systemMutex);
        }
        break;
      }
      case OP_RLINE: {
        int x1r = inst.p1 & 0x3FF, y1r = inst.p2 & 0x3FF;
        int x2r = inst.p3 & 0x3FF, y2r = inst.p4 & 0x3FF;
        if (x1r>=VM_REGISTER_COUNT||y1r>=VM_REGISTER_COUNT||x2r>=VM_REGISTER_COUNT||y2r>=VM_REGISTER_COUNT) {
          vmGSOD("RLINE: Gecersiz register", lineNumber); break;
        }
        int x1 = vmReg(x1r).toInt();
        int y1 = vmReg(y1r).toInt();
        int x2 = vmReg(x2r).toInt();
        int y2 = vmReg(y2r).toInt();
        uint16_t c = themeColorAccent;
        if (inst.p5 != -1) {
           int cr = inst.p5 & 0x3FF;
           if (cr < VM_REGISTER_COUNT) c = (uint16_t)vmReg(cr).toInt();
        }
        videodisplay.drawLine(x1, y1, x2, y2, c);
        break;
      }
      case OP_RSETCOLOR: {
        int rr = inst.p1 & 0x3FF, gr = inst.p2 & 0x3FF, br = inst.p3 & 0x3FF;
        if (rr>=VM_REGISTER_COUNT||gr>=VM_REGISTER_COUNT||br>=VM_REGISTER_COUNT) {
          vmGSOD("RSETCOLOR: Gecersiz register", lineNumber); break;
        }
        int r = vmReg(rr).toInt();
        int g = vmReg(gr).toInt();
        int b = vmReg(br).toInt();
        themeColorAccent = videodisplay.RGB(r, g, b);
        break;
      }
      case OP_RGET_IND: 
      case OP_RSET_IND: {
        int rd = inst.p1 & 0x3FF; 
        int rs = inst.p2 & 0x3FF;
        ScriptValue& regVal = vmReg(rs);
        int idx;
        if (regVal.type == VAL_STRING) {
            if (isActuallyNum(regVal.sVal)) {
                idx = (int)atof(regVal.sVal.c_str());
            } else {
                idx = -1;
            }
        } else {
            idx = (int)roundf(regVal.toFloat());
        }
        if (idx < 0 || idx >= VM_REGISTER_COUNT) {
            Serial.printf("!!! 1124 ENGELLENDİ !!! Kaynak: R%d, Hatalı Index: %d, Op: %s\n", 
                          rs, idx, (inst.op == OP_RGET_IND ? "RGET" : "RSET"));
            vmGSOD("Register Index Tasmasi: " + String(idx) + " (Limit 1024)", lineNumber);
            break;
        }
        if (inst.op == OP_RGET_IND) {
            vmReg(rd) = vmReg(idx); 
        } else {
            vmReg(idx) = vmReg(rd); 
        }
        break;
      }

      default: 
        if (inst.op == OP_UNKNOWN) {
          showSystemError("Script Calisma Hatasi", "Gecersiz komut. Satir: " + String(scriptCurrentLine+1) + "\nOpCode: " + String(inst.op));
        }
        break;
    }
    scriptCurrentLine++;
    }
  if (holdingVgaLock) { xSemaphoreGive(systemMutex); holdingVgaLock = false; }
  Serial.printf("[VM-EXIT] runEyuScriptTask exited loop. scriptCurrentLine: %d, scriptLineCount: %d, runningScript: %d\n", 
                scriptCurrentLine, scriptLineCount, runningScript);
  releaseScriptArrays(); // Clean up PSRAM arrays
  // scriptVariables + scriptVarNames String heap'ini geri ver (leak onleme)
  for (int i = 0; i < scriptVarCount; i++) {
    scriptVariables[i] = ScriptValue();
    scriptVarNames[i]  = String();
  }
  scriptVarCount = 0;
  runningScript = false; desktopMessage = "Script finished";
  turnOffStatusLED(); vgaCleanupScanline();
  staticBackgroundDrawn = false; desktopNeedsRedraw = true;
  scriptTaskHandle = NULL; vTaskDelete(NULL);
}

void stopEyuScript() {
  if (runningScript) {
    runningScript = false;
    // Script kendi kapanana kadar bekle (temiz shutdown yolu)
    int timeout = 0;
    while (scriptTaskHandle != NULL && timeout < 150) {  // max 1.5s bekle
      vTaskDelay(pdMS_TO_TICKS(10));
      timeout++;
    }
    // Task kendisi kapatmadiysa zorla sonlandir + bellek temizle
    if (scriptTaskHandle) {
      Serial.printf("[VM] Zorla kill (timeout %dms). Bellek temizleniyor...\n", timeout*10);
      vTaskDelete(scriptTaskHandle);
      scriptTaskHandle = NULL;
      // ── Zorla kill sonrasi String destructor'larini uyandir ──
      // PSRAM String sızıntılarını (leak) bu şekilde engelliyoruz ✅
      releaseScriptArrays();
      
      for (int i = 0; i < VM_PSRAM_COUNT; i++) vmPsramRegs[i] = ScriptValue();
      for (int i = 0; i < MAX_SCRIPT_VARS; i++) {
        scriptVariables[i] = ScriptValue();
        scriptVarNames[i]  = String();
      }
      // VGA mutex'i zorla kill durumunda serbest birak
      if (holdingVgaLock && systemMutex) {
        xSemaphoreGive(systemMutex);
        holdingVgaLock = false;
      }
      vgaCleanupScanline();
      desktopNeedsRedraw = true;
    }
  }
  logToFile("[VM] Script Stopped cleanly");
}

void runEyuScript(String filePath) {
  stopEyuScript(); 
  vTaskDelay(pdMS_TO_TICKS(50)); // Reclaim heap

  // Math Pool allocation in internal DRAM (SRAM) to prevent PSRAM String copy deadlocks
  if (!vmValuePool) vmValuePool = (ScriptValue*)heap_caps_malloc(VM_MATH_RECURSION * VM_MATH_STACK_SIZE * sizeof(ScriptValue), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  if (!vmOpPool)    vmOpPool    = (char*)heap_caps_malloc(VM_MATH_RECURSION * VM_MATH_STACK_SIZE, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  
  if (vmValuePool) {
     for(int i=0; i<VM_MATH_RECURSION*VM_MATH_STACK_SIZE; i++) new(&vmValuePool[i]) ScriptValue();
  }

  // Komut Havuzunu Kesin Olarak Temizle
  if (scriptProgram) {
    for (int i = 0; i < MAX_SCRIPT_LINES; i++) {
        scriptProgram[i].op = OP_UNKNOWN;
        scriptProgram[i].p1 = -1; scriptProgram[i].p2 = -1; scriptProgram[i].p3 = -1;
        scriptProgram[i].p4 = -1; scriptProgram[i].p5 = -1;
    }
  }

  uint32_t freeH = ESP.getFreeHeap();
  uint32_t maxBlock = ESP.getMaxAllocHeap();
  uint32_t freePsram = ESP.getFreePsram();
  
  globalScriptPath = filePath;
  runningScript = true;

  // Strateji: Math Pool'u PSRAM'e taşıdığımız için artık fonks. stack'i çok küçük.
  // 6KB DRAM stack (6144B) iç DRAM'deki 7.6KB'lık bloğa rahatça sığar.
  // PSRAM stack'e (FreeRTOS port hataları nedeniyle) gerek kalmadı. ✅
  
  size_t stackSize = 12288;
  BaseType_t res = xTaskCreatePinnedToCore(
      runEyuScriptTask, "EyuTurbo", stackSize, NULL, 5, &scriptTaskHandle, 1
    );

  if (res == pdPASS) {
    Serial.printf("[VM] EyuTurbo baslatildi. iDRAM Stack: %u B | PSRAM Pool Aktif ✅\n", stackSize);
  } else {
    runningScript = false;
    Serial.printf("[VM-ERR] Task hatasi: %d\n", res);
    addEsdosOutput("Hata: Script baslatilamadi!");
  }
}
