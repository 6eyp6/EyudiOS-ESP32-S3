#include "WadManager.h"
#include "Globals.h"

WadManager::WadManager() {
    numLumps = 0;
    infoTableOffset = 0;
    totalCacheUsed = 0;
}

WadManager::~WadManager() {
    closeWad();
}

bool WadManager::openWad(String path) {
    if (numLumps > 0) return true; // Already open
    
    // SD kart kilidi al
    if (xSemaphoreTake(sdMutex, pdMS_TO_TICKS(5000)) != pdTRUE) return false;
    
    wadFile = SD.open(path);
    if (!wadFile) {
        xSemaphoreGive(sdMutex);
        return false;
    }

    // Header parse: 4 byte magic, 4 byte numLumps, 4 byte infoTableOffset
    char magic[5] = {0};
    wadFile.read((uint8_t*)magic, 4);
    
    if (strcmp(magic, "IWAD") != 0 && strcmp(magic, "PWAD") != 0) {
        wadFile.close();
        xSemaphoreGive(sdMutex);
        return false;
    }

    wadFile.read((uint8_t*)&numLumps, 4);
    wadFile.read((uint8_t*)&infoTableOffset, 4);

    // Directory oku
    wadFile.seek(infoTableOffset);
    lumps.reserve(numLumps);

    for (uint32_t i = 0; i < numLumps; i++) {
        WadLump l;
        wadFile.read((uint8_t*)&l.fileOffset, 4);
        wadFile.read((uint8_t*)&l.size, 4);
        wadFile.read((uint8_t*)l.name, 8);
        l.name[8] = '\0';
        l.psramCache = nullptr;
        lumps.push_back(l);
    }

    xSemaphoreGive(sdMutex);
    return true;
}

void WadManager::closeWad() {
    // Tüm cache i temizle
    for (auto& l : lumps) {
        if (l.psramCache) {
            heap_caps_free(l.psramCache);
            l.psramCache = nullptr;
        }
    }
    lumps.clear();
    
    if (wadFile) {
        xSemaphoreTake(sdMutex, pdMS_TO_TICKS(1000));
        wadFile.close();
        xSemaphoreGive(sdMutex);
    }
    
    numLumps = 0;
    totalCacheUsed = 0;
}

int WadManager::findLump(const char* name) {
    char searchName[9];
    strncpy(searchName, name, 8);
    searchName[8] = '\0';
    for (int i = 0; i < (int)lumps.size(); i++) {
        if (strcasecmp(lumps[i].name, searchName) == 0) return i;
    }
    return -1;
}

void* WadManager::getLumpData(int index) {
    if (index < 0 || index >= (int)lumps.size()) return nullptr;

    // Cache kontrol et
    if (lumps[index].psramCache) return lumps[index].psramCache;

    // Cache'de yok, SD'den PSRAM'e çek
    uint32_t size = lumps[index].size;
    if (size == 0) return nullptr;

    // Cache limiti kontrolü (çok dolduysa temizle - Basit mantık)
    if (totalCacheUsed + size > maxCacheLimit) {
        // En eski veya rasgele birilerini temizle (Gerçek LRU yerine şimdilik boşaltma yapalım)
        for (auto& l : lumps) {
            if (l.psramCache) {
                heap_caps_free(l.psramCache);
                l.psramCache = nullptr;
            }
        }
        totalCacheUsed = 0;
    }

    void* buf = heap_caps_malloc(size, MALLOC_CAP_SPIRAM);
    if (!buf) return nullptr;

    if (xSemaphoreTake(sdMutex, pdMS_TO_TICKS(2000)) == pdTRUE) {
        wadFile.seek(lumps[index].fileOffset);
        wadFile.read((uint8_t*)buf, size);
        xSemaphoreGive(sdMutex);
        
        lumps[index].psramCache = buf;
        totalCacheUsed += size;
        return buf;
    }

    heap_caps_free(buf);
    return nullptr;
}

uint32_t WadManager::getLumpSize(int index) {
    if (index < 0 || index >= (int)lumps.size()) return 0;
    return lumps[index].size;
}

String WadManager::getWadInfo() {
    if (numLumps == 0) return "WAD Yuklu Degil.";
    return "WAD: " + String(numLumps) + " lumps. Cache: " + String(totalCacheUsed/1024) + " KB";
}

WadManager wadMgr;
