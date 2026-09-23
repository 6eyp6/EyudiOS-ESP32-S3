#ifndef EYU_FS_H
#define EYU_FS_H

#include <Arduino.h>
#include <FS.h>
#include <SD.h>
#include "CH375Driver.h"

class EyuFile : public Stream {
private:
    fs::File _sdFile;
    CH375::CH375FileImpl _usbFile;
    bool _isUsb;

public:
    EyuFile();
    EyuFile(fs::File sdFile);
    EyuFile(CH375::CH375FileImpl usbFile);

    operator bool() const { return _isUsb ? (bool)_usbFile : (bool)_sdFile; }

    // Stream / Print overrides
    int read() override;
    int read(uint8_t* buf, size_t size);
    size_t write(uint8_t val) override;
    size_t write(const uint8_t* buf, size_t size) override;
    int available() override;
    int peek() override;
    void flush() override;

    // File API
    bool seek(uint32_t pos, SeekMode mode = SeekSet);
    uint32_t position() const;
    uint32_t size() const;
    void close();
    bool isDirectory();
    EyuFile openNextFile(const char* mode = "r");
    void rewindDirectory();
    String name() const;
};

class EyuFSClass {
public:
    bool begin(int csPin, SPIClass &spi, uint32_t frequency = 4000000);
    EyuFile open(const String& path, const char* mode = "r");
    EyuFile open(const char* path, const char* mode = "r");
    bool exists(const String& path);
    bool remove(const String& path);
    bool rename(const String& src, const String& dst);
    bool mkdir(const String& path);
    bool rmdir(const String& path);
    uint64_t totalBytes();
    uint64_t usedBytes();
};

extern EyuFSClass EyuFS;

#endif
