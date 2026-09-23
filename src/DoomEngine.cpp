#include "DoomEngine.h"
#include "Globals.h"
#include <esp_task_wdt.h>

extern "C" {
    // Declarations for PrBoom symbols
    int doom_main(int argc, const char * const *argv);
    typedef enum { ev_keydown, ev_keyup, ev_mouse, ev_joystick } evtype_t;
    typedef struct { evtype_t type; int data1; int data2; int data3; } event_t;
    void D_PostEvent(const event_t* ev);
    
    // Shared WAD path variable defined in i_system.c
    extern char doomWadFilePath[256];
}

static TaskHandle_t doomTaskHandle = nullptr;
static bool prevHeldKeys[256] = {false};

// C-linkage display bridge called from i_video.c:I_FinishUpdate
extern "C" void doom_display_frame(const uint8_t* scr, const uint16_t* palette) {
    if (!showDoom || !eyeDoom.isRunning) return;
    
    extern volatile int sdReadCount;
    static uint32_t lastFPSPrint = 0;
    static int frameCount = 0;
    frameCount++;
    if (millis() - lastFPSPrint > 1000) {
        float fps = frameCount * 1000.0f / (millis() - lastFPSPrint);
        Serial.printf("[DOOM-DEBUG] Doom Engine FPS: %.2f (SD Reads: %d)\n", fps, sdReadCount);
        sdReadCount = 0;
        frameCount = 0;
        lastFPSPrint = millis();
    }
    
    static const uint16_t* lastPalettePtr = nullptr;
    static uint8_t nativePalette[256];
    static uint16_t lastPaletteContent[256];
    
    bool paletteChanged = (palette != lastPalettePtr);
    if (!paletteChanged) {
        for (int i = 0; i < 256; i++) {
            if (palette[i] != lastPaletteContent[i]) {
                paletteChanged = true;
                break;
            }
        }
    }
    
    if (paletteChanged) {
        lastPalettePtr = palette;
        Serial.println("[DOOM-PAL] Palette Changed! Converted colors:");
        for (int i = 0; i < 256; i++) {
            lastPaletteContent[i] = palette[i];
            uint16_t color = palette[i];
            
            uint8_t r_5 = (color >> 11) & 0x1F; // 0-31
            uint8_t g_6 = (color >> 5) & 0x3F;  // 0-63
            uint8_t b_5 = color & 0x1F;         // 0-31
            
            // Map to binary ON/OFF states based on threshold (ideal for 3-bit/8-color display)
            uint8_t r = (r_5 >= 14) ? 255 : 0;
            uint8_t g = (g_6 >= 28) ? 255 : 0;
            uint8_t b = (b_5 >= 14) ? 255 : 0;
            
            nativePalette[i] = videodisplay.base.rgb(r, g, b);
            if (i < 8) {
                Serial.printf("  idx %d: 0x%04X -> R:%d G:%d B:%d -> native 0x%02X\n", i, color, r, g, b, nativePalette[i]);
            }
        }
    }
    
    if (doomFrameReady) return;
    
    static int scaleMapX[400];
    static int scaleMapY[300];
    static bool scaleMapsInitialized = false;
    if (!scaleMapsInitialized) {
        for (int x = 0; x < 400; x++) {
            scaleMapX[x] = (x * 320) / 400;
            if (scaleMapX[x] >= 320) scaleMapX[x] = 319;
        }
        for (int y = 0; y < 300; y++) {
            scaleMapY[y] = (y * 240) / 300;
            if (scaleMapY[y] >= 240) scaleMapY[y] = 239;
        }
        scaleMapsInitialized = true;
    }
    
    if (eyeDoom.fullscreen) {
        for (int y = 0; y < 300; y++) {
            const uint8_t *src = scr + scaleMapY[y] * 320;
            uint8_t *dst = doomFrameBuffer + y * 400;
            for (int x = 0; x < 400; x++) {
                dst[x] = nativePalette[src[scaleMapX[x]]];
            }
        }
    } else {
        memset(doomFrameBuffer, 0, 400 * 300);
        for (int y = 0; y < 240; y++) {
            uint8_t *dst = doomFrameBuffer + (y + 30) * 400 + 40;
            const uint8_t *src = scr + y * 320;
            for (int x = 0; x < 320; x++) {
                dst[x] = nativePalette[src[x]];
            }
        }
    }
    
    doomFrameReady = true;
    doomLoadingActive = false;
}

static void doomThreadTask(void* pvParameters) {
    // Arguments to run shareware Doom
    const char* argv[] = { 
        "doom", 
        "-iwad", doomWadFilePath, 
        "-width", "320", 
        "-height", "240",
        "-nosound" // Sound not supported in phase 1
    };
    int argc = sizeof(argv)/sizeof(argv[0]);
    
    Serial.printf("[DOOM] Starting PrBoom loop with WAD: %s...\n", doomWadFilePath);
    
    esp_task_wdt_delete(NULL);
    
    // Run the main game loop
    doom_main(argc, argv);
    
    Serial.println("[DOOM] Game loop exited.");
    eyeDoom.isRunning = false;
    doomTaskHandle = nullptr;
    vTaskDelete(NULL);
}

DoomEngine::DoomEngine() {
    isRunning = false;
}

bool DoomEngine::init(String wadPath) {
    Serial.println("[DOOM-DEBUG] DoomEngine::init started");
    if (isRunning) {
        Serial.println("[DOOM-DEBUG] Already running, returning true");
        return true;
    }
    
    // Convert SD card path notation to VFS notation if necessary
    // E.g. "/doom1.wad" -> "/sd/doom1.wad"
    if (wadPath == "") wadPath = "/sd/doom1.wad";
    
    if (wadPath.startsWith("/")) {
        if (!wadPath.startsWith("/sd/") && !wadPath.startsWith("/sdcard/")) {
            wadPath = "/sd" + wadPath;
        }
    } else {
        wadPath = "/sd/" + wadPath;
    }
    
    strncpy(doomWadFilePath, wadPath.c_str(), sizeof(doomWadFilePath)-1);
    doomWadFilePath[sizeof(doomWadFilePath)-1] = '\0';
    currentWadPath = wadPath;
    
    if (!doomFrameBuffer) {
        Serial.println("[DOOM-DEBUG] Allocating frame buffer...");
        doomFrameBuffer = (uint8_t*)heap_caps_malloc(400 * 300, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!doomFrameBuffer) {
            Serial.println("[DOOM-ERR] PSRAM framebuffer allocation failed!");
            return false;
        }
    }
    
    doomLoadingActive = true;
    doomLoadingProgress = 0;
    strcpy(doomLoadingText, "Starting Doom...");
    
    // Create Doom runner thread on Core 0 (keeping Core 1 for VGA and OS tasks)
    isRunning = true;
    Serial.println("[DOOM-DEBUG] Creating EyuDoom task on Core 0...");
    BaseType_t res = xTaskCreatePinnedToCore(
        doomThreadTask, 
        "EyuDoom", 
        32768, // Larger stack for PrBoom
        NULL, 
        1, // Lower priority to avoid starving IDLE tasks
        &doomTaskHandle, 
        0 // Core 0
    );
    
    if (res != pdPASS) {
        Serial.println("[DOOM-ERR] Failed to create Doom task!");
        isRunning = false;
        doomLoadingActive = false;
        return false;
    }
    
    Serial.println("[DOOM-DEBUG] DoomEngine::init finished");
    return true;
}

void DoomEngine::update(float deltaTime) {
    // Key event posting has been safely moved to Core 0 (I_StartFrame inside i_video.c)
    // to prevent multi-core race conditions and memory corruption crashes.
}

void DoomEngine::render(int x, int y, int w, int h) {
    // The screen updates are handled asynchronously by I_FinishUpdate calling doom_display_frame
}

void DoomEngine::toggleFullscreen() {
    fullscreen = !fullscreen;
    Serial.printf("[DOOM] Fullscreen: %s\n", fullscreen ? "ON" : "OFF");
}

void DoomEngine::stop() {
    if (!isRunning) return;
    
    Serial.println("[DOOM] Stopping game engine...");
    
    // Request game exit inside Doom engine
    // We can simulate an Escape key or tell game to exit. 
    // In PrBoom we can set gameaction = ga_nothing or force exit since we are deleting the task.
    isRunning = false;
    if (doomTaskHandle) {
        vTaskDelete(doomTaskHandle);
        doomTaskHandle = nullptr;
    }
    
    // Clear display area
    videodisplay.fillScreen(videodisplay.RGB(0,0,0));
    videodisplay.show();
    
    if (doomFrameBuffer) {
        heap_caps_free(doomFrameBuffer);
        doomFrameBuffer = nullptr;
    }
    doomFrameReady = false;
}

DoomEngine eyeDoom;

extern "C" void doom_trigger_gsod(const char* title, const char* message) {
    gsodTitle = title;
    gsodMessage = message;
    gsodPending = true;
    systemInErrorState = true;
}

#include <SD.h>
extern "C" {
    static File doomFiles[32];

    int doom_open(const char* path, int flags) {
        int fd = -1;
        for (int i = 3; i < 32; i++) {
            if (!doomFiles[i]) {
                fd = i;
                break;
            }
        }
        if (fd == -1) return -1;

        String sPath = path;
        // Strip mount points /sdcard or /sd to get the absolute path relative to SD card root
        if (sPath.startsWith("/sdcard/")) {
            sPath = "/" + sPath.substring(8);
        } else if (sPath.startsWith("/sd/")) {
            sPath = "/" + sPath.substring(4);
        } else if (!sPath.startsWith("/")) {
            sPath = "/" + sPath;
        }

        if (xSemaphoreTake(sdMutex, pdMS_TO_TICKS(5000)) != pdTRUE) return -1;
        File f = SD.open(sPath, FILE_READ);
        xSemaphoreGive(sdMutex);

        if (!f) return -1;

        doomFiles[fd] = f;
        return fd;
    }

    void doom_close(int fd) {
        if (fd < 3 || fd >= 32 || !doomFiles[fd]) return;
        if (xSemaphoreTake(sdMutex, pdMS_TO_TICKS(5000)) == pdTRUE) {
            doomFiles[fd].close();
            // Clear File object slot
            doomFiles[fd] = File();
            xSemaphoreGive(sdMutex);
        }
    }

    volatile int sdReadCount = 0;
    
    int doom_read(int fd, void* buf, int len) {
        sdReadCount++;
        if (fd < 3 || fd >= 32 || !doomFiles[fd]) return -1;
        if (xSemaphoreTake(sdMutex, pdMS_TO_TICKS(5000)) != pdTRUE) return -1;
        
        uint8_t* p = (uint8_t*)buf;
        int totalRead = 0;
        while (totalRead < len) {
            int toRead = len - totalRead;
            int bytesRead = doomFiles[fd].read(p + totalRead, toRead);
            if (bytesRead <= 0) {
                break; // EOF or error
            }
            totalRead += bytesRead;
        }
        
        xSemaphoreGive(sdMutex);
        return totalRead;
    }

    int doom_lseek(int fd, int offset, int whence) {
        if (fd < 3 || fd >= 32 || !doomFiles[fd]) return -1;
        if (xSemaphoreTake(sdMutex, pdMS_TO_TICKS(5000)) != pdTRUE) return -1;

        int target = offset;
        if (whence == SEEK_CUR) {
            target = doomFiles[fd].position() + offset;
        } else if (whence == SEEK_END) {
            target = doomFiles[fd].size() + offset;
        }

        doomFiles[fd].seek(target);
        int pos = doomFiles[fd].position();
        xSemaphoreGive(sdMutex);
        return pos;
    }

    int doom_filelength(int fd) {
        if (fd < 3 || fd >= 32 || !doomFiles[fd]) return 0;
        if (xSemaphoreTake(sdMutex, pdMS_TO_TICKS(5000)) != pdTRUE) return 0;
        int sz = doomFiles[fd].size();
        xSemaphoreGive(sdMutex);
        return sz;
    }
}
