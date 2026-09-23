// ============================================================
// AudioManager.cpp — EyudiOS S3 Audio Engine
// Standard I2S modu (PDM kaldirildi)
// Harici I2S DAC gerektirir: MAX98357A veya PCM5102
//   BCLK  → GPIO 17
//   LRC   → GPIO 16  (yeni)
//   DOUT  → GPIO 18
// ============================================================
#include "Globals.h"
#include <Audio.h>
#include "driver/i2s.h"

// Pin tanimlamalari
#define AUDIO_I2S_BCLK  17
#define AUDIO_I2S_LRC   16
#define AUDIO_I2S_DOUT  18

// ── Global Durum ───────────────────────────────────────────
bool audioReady   = false;
bool audioPlaying = false;
int  audioVolume  = 60; 
String audioCurrentFile = "";

static Audio* audioObj = nullptr;
static TaskHandle_t audioTaskHandle = nullptr;
static SemaphoreHandle_t audioMutex = nullptr;

// ── Audio Task ─────────────────────────────────────
static void audioLoopTask(void*) {
  while (true) {
    if (audioReady && audioPlaying && audioObj && audioMutex) {
      if (xSemaphoreTake(audioMutex, 0) == pdTRUE) {
        audioObj->loop();
        xSemaphoreGive(audioMutex);
      }
    }
    vTaskDelay(pdMS_TO_TICKS(2)); 
  }
}

// ── Callback'ler ───────────────────────────────────────────
void audio_info(const char *info) { Serial.printf("[AUDIO] %s\n", info); }
void audio_eof_mp3(const char *info){ audioPlaying = false; audioCurrentFile = ""; }

void audio_showstation(const char *info) {
  String msg = "[AUDIO] icy-name: " + String(info);
  Serial.println(msg);
  addEsdosOutput(msg);
  setScriptVariableByName("radio_station", String(info));
}

void audio_showstreamtitle(const char *info) {
  String msg = "[AUDIO] StreamTitle: " + String(info);
  Serial.println(msg);
  addEsdosOutput(msg);
  setScriptVariableByName("radio_title", String(info));
}

// ── Dışa Açık API ──────────────────────────────────────────
void initAudio() {
  if (audioReady && audioObj) return;
  if (!audioMutex) audioMutex = xSemaphoreCreateMutex();

  if (xSemaphoreTake(audioMutex, pdMS_TO_TICKS(2000)) == pdTRUE) {
    if (!audioObj) {
      uint32_t freeSram = heap_caps_get_free_size(MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL);
      Serial.printf("[AUDIO] Standard I2S Init. SRAM: %d B\n", freeSram);
      if (freeSram < 16000) {
        Serial.println("[AUDIO] HATA: Yetersiz SRAM!");
        xSemaphoreGive(audioMutex);
        vmGSOD("Yetersiz RAM (Serbest: " + String(freeSram) + " B). I2S baslatilamiyor.", lineNumber);
        return;
      }

      // Once temizle
      i2s_driver_uninstall(I2S_NUM_0);
      i2s_driver_uninstall(I2S_NUM_1);

      // Standard I2S modu (PDM degil) — I2S_NUM_1 kullan
      audioObj = new Audio(false, 3, I2S_NUM_1);

      if (audioObj) {
        // BCLK, LRC (WS), DOUT siralamasiyla
        audioObj->setPinout(AUDIO_I2S_BCLK, AUDIO_I2S_LRC, AUDIO_I2S_DOUT);
        audioObj->setVolume(audioVolume * 21 / 100);

        if (!audioTaskHandle) {
          xTaskCreatePinnedToCore(audioLoopTask, "audio_loop", 8192,
                                  nullptr, 3, &audioTaskHandle, 1);
        }
        audioReady = true;
        Serial.println("[AUDIO] Standard I2S OK (BCLK=17, LRC=16, DOUT=18).");
      } else {
        Serial.println("[AUDIO] HATA: Audio nesnesi olusturulamadi!");
      }
    }
    xSemaphoreGive(audioMutex);
  }
}

bool audioPlay(String path) {
  bool isWebStream = path.startsWith("http://") || path.startsWith("https://");
  if (!isWebStream && !sdCardPresent) return false;
  if (!audioReady || !audioObj) initAudio();
  if (!audioObj || !audioMutex) return false;

  if (!isWebStream) {
    if (!path.startsWith("/")) path = "/" + path;
    if (path.endsWith(".txt")) {
      return playRadioFromConfig(path);
    }
    if (!SD.exists(path)) return false;
  }

  bool ok = false;
  if (audioTaskHandle) vTaskSuspend(audioTaskHandle);
  
  if (xSemaphoreTake(audioMutex, pdMS_TO_TICKS(3000)) == pdTRUE) {
    bool gotSdMutex = false;
    if (!isWebStream) {
      if (sdMutex && xSemaphoreTake(sdMutex, pdMS_TO_TICKS(3000)) == pdTRUE) {
        gotSdMutex = true;
      }
    }
    
    // In web stream mode, we don't strictly need sdMutex if we aren't reading from local storage
    if (isWebStream || gotSdMutex) {
      audioPlaying = false;
      audioObj->stopSong(); // Temizle
      
      Serial.printf("[AUDIO] Play: %s\n", path.c_str());
      if (isWebStream) {
        ok = audioObj->connecttohost(path.c_str());
      } else {
        ok = audioObj->connecttoFS(RealSD, path.c_str());
      }
      
      if (ok) {
        audioPlaying = true;
        audioCurrentFile = path;
      }
      if (gotSdMutex && sdMutex) {
        xSemaphoreGive(sdMutex);
      }
    }
    xSemaphoreGive(audioMutex);
  }
  
  if (audioTaskHandle) vTaskResume(audioTaskHandle);
  return ok;
}

bool playRadioFromConfig(String txtFilePath) {
  if (!sdCardPresent && !usbPresent) return false;
  if (!txtFilePath.startsWith("/")) txtFilePath = "/" + txtFilePath;
  if (!SD.exists(txtFilePath)) {
    Serial.println("[AUDIO] Radio config file not found!");
    return false;
  }
  File f = SD.open(txtFilePath, "r");
  if (!f) return false;
  
  String url = "";
  while (f.available()) {
    char c = f.read();
    if (c == '\n' || c == '\r') {
      if (url.length() > 0) break;
    } else {
      url += c;
    }
  }
  f.close();
  url.trim();
  
  if (url.length() > 0 && (url.startsWith("http://") || url.startsWith("https://"))) {
    Serial.printf("[AUDIO] Loaded URL from %s: %s\n", txtFilePath.c_str(), url.c_str());
    return audioPlay(url);
  }
  Serial.println("[AUDIO] Invalid URL in radio config file.");
  return false;
}

// --- Kontrol Fonksiyonları ---
void audioStop() {
  if (audioReady && audioObj && audioMutex && xSemaphoreTake(audioMutex, pdMS_TO_TICKS(500)) == pdTRUE) {
    audioPlaying = false; audioObj->stopSong(); xSemaphoreGive(audioMutex);
  }
}

void audioSetVolume(int vol) {
  audioVolume = constrain(vol, 0, 100);
  if (audioReady && audioObj && audioMutex && xSemaphoreTake(audioMutex, pdMS_TO_TICKS(500)) == pdTRUE) {
    audioObj->setVolume(audioVolume * 21 / 100); xSemaphoreGive(audioMutex);
  }
}

void audioPause() {
  if (audioReady && audioObj && audioMutex && xSemaphoreTake(audioMutex, pdMS_TO_TICKS(500)) == pdTRUE) {
    audioObj->pauseResume(); xSemaphoreGive(audioMutex);
  }
}

// ── LEDC PWM Beep (Non-blocking) ──────────────────────────
#define BEEP_LEDC_CH    7    // LEDC kanal (0-7 arası, diğer PWM ile çakışmaz)
#define BEEP_LEDC_RES   8    // 8-bit çözünürlük

static TaskHandle_t beepTaskHandle = nullptr;

struct BeepParams { int hz; int ms; };
static void beepTask(void* pv) {
  BeepParams* p = (BeepParams*)pv;
  ledcSetup(BEEP_LEDC_CH, p->hz, BEEP_LEDC_RES);
  ledcAttachPin(buzzerPin, BEEP_LEDC_CH);
  ledcWrite(BEEP_LEDC_CH, 128); // 50% duty cycle
  vTaskDelay(pdMS_TO_TICKS(p->ms));
  ledcWrite(BEEP_LEDC_CH, 0);   // Sessiz
  ledcDetachPin(buzzerPin);
  delete p;
  beepTaskHandle = nullptr;
  vTaskDelete(nullptr);
}

void audioBeep(int hz, int ms) {
  if (buzzerPin < 0) return;  // Buzör pin tanımlı değilse atla
  if (beepTaskHandle != nullptr) {
    vTaskDelete(beepTaskHandle);
    ledcWrite(BEEP_LEDC_CH, 0);
    ledcDetachPin(buzzerPin);
    beepTaskHandle = nullptr;
  }
  BeepParams* p = new BeepParams{hz, ms};
  xTaskCreatePinnedToCore(beepTask, "beep", 2048, p, 2, &beepTaskHandle, 1);
}

String audioStatus() {
  if (!audioReady) return "Off";
  if (!audioPlaying) return "Idle";
  return "Playing: " + audioCurrentFile;
}
