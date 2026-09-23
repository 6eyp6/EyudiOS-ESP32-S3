#ifndef WAD_MANAGER_H
#define WAD_MANAGER_H

#include <Arduino.h>
#include "SystemConfig.h"
#include <vector>

struct WadLump {
    char name[9];
    uint32_t fileOffset;
    uint32_t size;
    void* psramCache; // Pointer to PSRAM if cached, else nullptr
};

class WadManager {
public:
    WadManager();
    ~WadManager();

    bool openWad(String path);
    void closeWad();

    // Lump functions
    int findLump(const char* name);
    void* getLumpData(int index); // This part implements "on-demand" loading
    void freeLump(int index);    // Optional: manually free a lump from PSRAM

    uint32_t getLumpSize(int index);
    String getWadInfo();

private:
    File wadFile;
    std::vector<WadLump> lumps;
    uint32_t numLumps;
    uint32_t infoTableOffset;
    
    // Simple cache management
    uint32_t totalCacheUsed;
    const uint32_t maxCacheLimit = 4 * 1024 * 1024; // 4 * 1024 (Bytes->KB) * 1024 (KB->MB) = 4.194.304 Bytes
};

extern WadManager wadMgr;

#endif
