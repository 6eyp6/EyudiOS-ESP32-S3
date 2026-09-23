#ifndef APP_API_H
#define APP_API_H

#include <Arduino.h>

// ============================================================
// EyudiOS ESP32-S3 Edition v2.0 — Unified Application API
// ============================================================

struct AppDescriptor {
    const char* id;          // Unique application identifier (e.g. "FileManager")
    const char* title;       // Display title in Window titlebar / Desktop
    const char* symbol;      // 2-character icon display symbol (e.g. "FL")
    uint16_t color;          // Icon theme color (RGB565 format)
    bool enabled;            // Enabled status
    void (*launchFunc)();    // Application launcher callback function
};

// Plugin Hook Interfaces (Weak symbols allow seamless external application registration)
#ifdef __cplusplus
extern "C" {
#endif

void registerExternalApps();
bool launchExternalApp(const String &appName);

#ifdef __cplusplus
}
#endif

#endif // APP_API_H
