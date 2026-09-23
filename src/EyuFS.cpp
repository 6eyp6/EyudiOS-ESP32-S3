#include "EyuFS.h"

// Define the global instance
EyuFSClass EyuFS;

// EyuFile Implementation
EyuFile::EyuFile() : _isUsb(false) {}

EyuFile::EyuFile(fs::File sdFile) : _sdFile(sdFile), _isUsb(false) {}

EyuFile::EyuFile(CH375::CH375FileImpl usbFile) : _usbFile(usbFile), _isUsb(true) {}

int EyuFile::read() {
    if (_isUsb) return _usbFile.read();
    return _sdFile.read();
}

int EyuFile::read(uint8_t* buf, size_t size) {
    if (_isUsb) return _usbFile.read(buf, size);
    return _sdFile.read(buf, size);
}

size_t EyuFile::write(uint8_t val) {
    if (_isUsb) return _usbFile.write(val);
    return _sdFile.write(val);
}

size_t EyuFile::write(const uint8_t* buf, size_t size) {
    if (_isUsb) return _usbFile.write(buf, size);
    return _sdFile.write(buf, size);
}

int EyuFile::available() {
    if (_isUsb) return _usbFile.available();
    return _sdFile.available();
}

int EyuFile::peek() {
    if (_isUsb) return _usbFile.peek();
    return _sdFile.peek();
}

void EyuFile::flush() {
    if (!_isUsb) _sdFile.flush();
}

bool EyuFile::seek(uint32_t pos, SeekMode mode) {
    if (_isUsb) {
        uint32_t target = pos;
        if (mode == SeekEnd) {
            target = _usbFile.size() - pos;
        } else if (mode == SeekCur) {
            target = _usbFile.position() + pos;
        }
        return _usbFile.seek(target);
    }
    return _sdFile.seek(pos, mode);
}

uint32_t EyuFile::position() const {
    if (_isUsb) return _usbFile.position();
    return _sdFile.position();
}

uint32_t EyuFile::size() const {
    if (_isUsb) return _usbFile.size();
    return _sdFile.size();
}

void EyuFile::close() {
    if (_isUsb) {
        _usbFile.close();
    } else {
        _sdFile.close();
    }
}

bool EyuFile::isDirectory() {
    if (_isUsb) return _usbFile.isDirectory();
    return _sdFile.isDirectory();
}

EyuFile EyuFile::openNextFile(const char* mode) {
    if (_isUsb) {
        CH375::FileInfo info;
        if (_usbFile.openNextFile(info)) {
            String fullPath = _usbFile.path();
            if (!fullPath.endsWith("/")) fullPath += "/";
            fullPath += String(info.name);
            CH375::CH375FileImpl entry(fullPath, info.isDir, info.size);
            return EyuFile(entry);
        }
        return EyuFile();
    }
    return EyuFile(_sdFile.openNextFile(mode));
}

void EyuFile::rewindDirectory() {
    if (_isUsb) {
        _usbFile.rewindDirectory();
    } else {
        _sdFile.rewindDirectory();
    }
}

String EyuFile::name() const {
    if (_isUsb) return _usbFile.path();
    return _sdFile.name();
}


// EyuFSClass Implementation
extern bool sdCardPresent;
extern bool usbPresent;

bool EyuFSClass::begin(int csPin, SPIClass &spi, uint32_t frequency) {
    if (SD.begin(csPin, spi, frequency)) {
        sdCardPresent = true;
        return true;
    }
    sdCardPresent = false;
    return false;
}

EyuFile EyuFSClass::open(const String& path, const char* mode) {
    if (sdCardPresent) {
        return EyuFile(SD.open(path, mode));
    }
    if (usbPresent) {
        bool forWrite = (strchr(mode, 'w') != nullptr || strchr(mode, 'a') != nullptr);
        CH375::OpenMode om = CH375::READ_ONLY;
        if (forWrite) {
            om = CH375::WRITE_ONLY;
        }
        CH375::CH375FileImpl file = CH375FS.open(path, om);
        return EyuFile(file);
    }
    return EyuFile();
}

EyuFile EyuFSClass::open(const char* path, const char* mode) {
    return open(String(path), mode);
}

bool EyuFSClass::exists(const String& path) {
    if (sdCardPresent) return SD.exists(path);
    if (usbPresent) return CH375FS.exists(path);
    return false;
}

bool EyuFSClass::remove(const String& path) {
    if (sdCardPresent) return SD.remove(path);
    if (usbPresent) return CH375FS.remove(path);
    return false;
}

bool EyuFSClass::rename(const String& src, const String& dst) {
    if (sdCardPresent) return SD.rename(src, dst);
    if (usbPresent) return CH375FS.rename(src, dst);
    return false;
}

bool EyuFSClass::mkdir(const String& path) {
    if (sdCardPresent) return SD.mkdir(path);
    if (usbPresent) return CH375FS.mkdir(path);
    return false;
}

bool EyuFSClass::rmdir(const String& path) {
    if (sdCardPresent) return SD.rmdir(path);
    if (usbPresent) return CH375FS.rmdir(path);
    return false;
}

uint64_t EyuFSClass::totalBytes() {
    if (sdCardPresent) return SD.totalBytes();
    if (usbPresent) return CH375FS.totalBytes();
    return 0;
}

uint64_t EyuFSClass::usedBytes() {
    if (sdCardPresent) return SD.usedBytes();
    if (usbPresent) return CH375FS.usedBytes();
    return 0;
}
