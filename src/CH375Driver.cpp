#include "CH375Driver.h"
#include <HardwareSerial.h>
#include "Globals.h"

CH375::CH375FSClass CH375FS;

namespace CH375 {
    // Send a command to Leonardo and wait for the response starting with "UR,"
    static String sendAndGetRep(const String& cmd, unsigned long timeout = 5000) {
        if (!CH375FS.isConnected() && !cmd.startsWith("U,INIT")) return "";
        
        usbResponseReady = false;
        Serial1.println(cmd);
        
        unsigned long start = millis();
        while (millis() - start < timeout) {
            if (usbResponseReady) {
                usbResponseReady = false;
                return lastUsbResponse;
            }
            delay(1);
        }
        return ""; // Timeout
    }

    // Hex Conversion Helpers
    static String toHex(const uint8_t* buf, int len) {
        String hex = "";
        for (int i = 0; i < len; i++) {
            if (buf[i] < 16) hex += "0";
            hex += String(buf[i], HEX);
        }
        return hex;
    }

    static uint8_t fromHexChar(char c) {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return 0;
    }

    static void fromHex(const String& hex, uint8_t* buf) {
        int len = hex.length() / 2;
        for (int i = 0; i < len; i++) {
            buf[i] = (fromHexChar(hex[i*2]) << 4) | fromHexChar(hex[i*2+1]);
        }
    }

    // CH375FileImpl Implementation
    CH375FileImpl::CH375FileImpl() : _pos(0), _size(0), _isDir(false), _isOpen(false), _enumStarted(false) {}
    
    CH375FileImpl::CH375FileImpl(const String& path, bool isDir, uint32_t size) 
        : _path(path), _pos(0), _size(size), _isDir(isDir), _isOpen(true), _enumStarted(false) {}

    int CH375FileImpl::read() {
        uint8_t b;
        if (read(&b, 1) == 1) return b;
        return -1;
    }

    int CH375FileImpl::read(uint8_t* buf, size_t size) {
        if (!_isOpen || _isDir) return 0;
        
        String rep = sendAndGetRep("U,READ," + String(size));
        if (rep.startsWith("UR,READ,")) {
            // Format: UR,READ,actual_len,hex_data
            int firstComma = rep.indexOf(',');
            int secondComma = rep.indexOf(',', firstComma + 1);
            int thirdComma = rep.indexOf(',', secondComma + 1);
            
            int actualLen = rep.substring(secondComma + 1, thirdComma).toInt();
            String hexData = rep.substring(thirdComma + 1);
            
            if (actualLen > 0) {
                fromHex(hexData, buf);
                _pos += actualLen;
                return actualLen;
            }
        }
        return 0;
    }

    size_t CH375FileImpl::write(uint8_t val) {
        return write(&val, 1);
    }

    size_t CH375FileImpl::write(const uint8_t* buf, size_t size) {
        if (!_isOpen || _isDir) return 0;
        
        String hexData = toHex(buf, size);
        String rep = sendAndGetRep("U,WRITE," + hexData);
        if (rep.startsWith("UR,WRITE,")) {
            int lastComma = rep.lastIndexOf(',');
            int written = rep.substring(lastComma + 1).toInt();
            _pos += written;
            if (_pos > _size) _size = _pos;
            return written;
        }
        return 0;
    }

    int CH375FileImpl::available() {
        if (!_isOpen || _isDir) return 0;
        return (_size > _pos) ? (_size - _pos) : 0;
    }

    int CH375FileImpl::peek() {
        if (!_isOpen || _isDir || _pos >= _size) return -1;
        uint8_t b;
        uint32_t saved = _pos;
        int bytesRead = read(&b, 1);
        seek(saved);
        return (bytesRead == 1) ? b : -1;
    }

    bool CH375FileImpl::seek(uint32_t pos) {
        if (!_isOpen) return false;
        
        String rep = sendAndGetRep("U,SEEK," + String(pos));
        if (rep.startsWith("UR,SEEK,")) {
            int lastComma = rep.lastIndexOf(',');
            bool ok = rep.substring(lastComma + 1).toInt() == 1;
            if (ok) {
                _pos = pos;
                return true;
            }
        }
        return false;
    }

    void CH375FileImpl::close() {
        if (_isOpen) {
            sendAndGetRep("U,CLOSE");
            _isOpen = false;
        }
    }

    bool CH375FileImpl::openNextFile(FileInfo& info) {
        if (!_isOpen || !_isDir) return false;
        
        String rep = sendAndGetRep("U,NEXT," + _path);
        if (rep.startsWith("UR,NEXT,ERR")) return false;
        
        if (rep.startsWith("UR,NEXT,")) {
            // Format: UR,NEXT,name,isDir,size
            int comma1 = rep.indexOf(',');
            int comma2 = rep.indexOf(',', comma1 + 1);
            int comma3 = rep.indexOf(',', comma2 + 1);
            int comma4 = rep.indexOf(',', comma3 + 1);
            
            String name = rep.substring(comma2 + 1, comma3);
            bool isDir = rep.substring(comma3 + 1, comma4).toInt() == 1;
            uint32_t size = rep.substring(comma4 + 1).toInt();
            
            strncpy(info.name, name.c_str(), sizeof(info.name) - 1);
            info.name[sizeof(info.name) - 1] = '\0';
            info.isDir = isDir;
            info.size = size;
            return true;
        }
        return false;
    }

    void CH375FileImpl::rewindDirectory() {
        sendAndGetRep("U,REWIND");
    }


    // CH375FSClass Implementation
    CH375FSClass::CH375FSClass() : _initialized(false), _connected(false), _currentDir("/"), _rxPin(-1), _txPin(-1) {}

    bool CH375FSClass::begin(int rxPin, int txPin) {
        _rxPin = rxPin;
        _txPin = txPin;
        
        Serial1.begin(115200, SERIAL_8N1, rxPin, txPin);
        delay(100);
        
        _initialized = true;
        return true;
    }

    bool CH375FSClass::mount() {
        if (!_initialized) return false;
        
        // Command Leonardo to initialize CH375 and mount
        String rep = sendAndGetRep("U,INIT", 6000);
        if (rep.startsWith("UR,INIT,")) {
            int lastComma = rep.lastIndexOf(',');
            _connected = (rep.substring(lastComma + 1).toInt() == 1);
            return _connected;
        }
        return false;
    }

    CH375FileImpl CH375FSClass::open(const String& path, OpenMode mode) {
        String cleanPath = path;
        if (!cleanPath.startsWith("/")) {
            cleanPath = _currentDir + (_currentDir.endsWith("/") ? "" : "/") + cleanPath;
        }
        
        String rep = sendAndGetRep("U,OPEN," + cleanPath + "," + String(mode));
        if (rep.startsWith("UR,OPEN,")) {
            int comma1 = rep.indexOf(',');
            int comma2 = rep.indexOf(',', comma1 + 1);
            int comma3 = rep.indexOf(',', comma2 + 1);
            
            bool ok = rep.substring(comma2 + 1, comma3).toInt() == 1;
            uint32_t size = rep.substring(comma3 + 1).toInt();
            
            if (ok) {
                bool isDir = path.endsWith("/");
                return CH375FileImpl(cleanPath, isDir, size);
            }
        }
        return CH375FileImpl();
    }

    bool CH375FSClass::exists(const String& path) {
        String cleanPath = path;
        if (!cleanPath.startsWith("/")) {
            cleanPath = _currentDir + (_currentDir.endsWith("/") ? "" : "/") + cleanPath;
        }
        String rep = sendAndGetRep("U,EXISTS," + cleanPath);
        if (rep.startsWith("UR,EXISTS,")) {
            int lastComma = rep.lastIndexOf(',');
            return (rep.substring(lastComma + 1).toInt() == 1);
        }
        return false;
    }

    bool CH375FSClass::remove(const String& path) {
        String cleanPath = path;
        if (!cleanPath.startsWith("/")) {
            cleanPath = _currentDir + (_currentDir.endsWith("/") ? "" : "/") + cleanPath;
        }
        String rep = sendAndGetRep("U,REMOVE," + cleanPath);
        if (rep.startsWith("UR,REMOVE,")) {
            int lastComma = rep.lastIndexOf(',');
            return (rep.substring(lastComma + 1).toInt() == 1);
        }
        return false;
    }

    bool CH375FSClass::rename(const String& oldName, const String& newName) {
        if (copy(oldName, newName)) {
            return remove(oldName);
        }
        return false;
    }

    bool CH375FSClass::copy(const String& src, const String& dst) {
        CH375FileImpl s = open(src, READ_ONLY);
        CH375FileImpl d = open(dst, WRITE_ONLY);
        if (s && d) {
            uint8_t buf[64];
            while (s.available()) {
                int r = s.read(buf, sizeof(buf));
                if (r <= 0) break;
                d.write(buf, r);
            }
            s.close();
            d.close();
            return true;
        }
        if (s) s.close();
        if (d) d.close();
        return false;
    }

    bool CH375FSClass::move(const String& src, const String& dst) {
        if (copy(src, dst)) {
            return remove(src);
        }
        return false;
    }

    bool CH375FSClass::mkdir(const String& path) {
        String cleanPath = path;
        if (!cleanPath.startsWith("/")) {
            cleanPath = _currentDir + (_currentDir.endsWith("/") ? "" : "/") + cleanPath;
        }
        String rep = sendAndGetRep("U,MKDIR," + cleanPath);
        if (rep.startsWith("UR,MKDIR,")) {
            int lastComma = rep.lastIndexOf(',');
            return (rep.substring(lastComma + 1).toInt() == 1);
        }
        return false;
    }

    bool CH375FSClass::rmdir(const String& path) {
        return remove(path);
    }

    bool CH375FSClass::changeDirectory(const String& path) {
        String cleanPath = path;
        if (!cleanPath.startsWith("/")) {
            cleanPath = _currentDir + (_currentDir.endsWith("/") ? "" : "/") + cleanPath;
        }
        if (exists(cleanPath)) {
            _currentDir = cleanPath;
            return true;
        }
        return false;
    }

    uint64_t CH375FSClass::totalBytes() {
        return 8ULL * 1024 * 1024 * 1024;
    }

    uint64_t CH375FSClass::usedBytes() {
        return 2ULL * 1024 * 1024 * 1024;
    }
}
