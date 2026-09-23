#ifndef CH375_DRIVER_H
#define CH375_DRIVER_H

#include <Arduino.h>

namespace CH375 {
    // CH375 Command Definitions
    const uint8_t CMD_SET_BAUDRATE    = 0x02;
    const uint8_t CMD_RESET_ALL       = 0x05;
    const uint8_t CMD_SET_USB_MODE    = 0x15;
    const uint8_t CMD_SET_FILE_NAME   = 0x2F;
    const uint8_t CMD_DISK_INIT       = 0x51;
    const uint8_t CMD_DISK_MOUNT      = 0x31;
    const uint8_t CMD_FILE_OPEN       = 0x32;
    const uint8_t CMD_FILE_ENUM_GO    = 0x33;
    const uint8_t CMD_FILE_CREATE     = 0x34;
    const uint8_t CMD_FILE_ERASE      = 0x35;
    const uint8_t CMD_FILE_CLOSE      = 0x36;
    const uint8_t CMD_BYTE_LOCATE     = 0x39;
    const uint8_t CMD_BYTE_READ       = 0x3A;
    const uint8_t CMD_BYTE_RD_GO      = 0x3B;
    const uint8_t CMD_BYTE_WRITE      = 0x3C;
    const uint8_t CMD_BYTE_WR_GO      = 0x3D;
    const uint8_t CMD_GET_STATUS      = 0x22;
    const uint8_t CMD_RD_USB_DATA     = 0x28;
    const uint8_t CMD_WR_USB_DATA     = 0x2C;
    
    // Additional useful commands
    const uint8_t CMD_DISK_QUERY      = 0x3F;
    const uint8_t CMD_DISK_READY      = 0x30;
    const uint8_t CMD_DIR_INFO_READ   = 0x37;
    const uint8_t CMD_DIR_INFO_SAVE   = 0x38;
    const uint8_t CMD_SEC_LOCATE      = 0x4A;
    const uint8_t CMD_SEC_READ        = 0x4B;
    const uint8_t CMD_SEC_WRITE       = 0x4C;

    // USB Modes
    const uint8_t USB_MODE_INVALID    = 0x00;
    const uint8_t USB_MODE_HOST       = 0x06;
    const uint8_t USB_MODE_HOST_RESET = 0x07;
    
    // Status / Interrupt codes
    const uint8_t ANS_RET_SUCCESS     = 0x51;
    const uint8_t USB_INT_SUCCESS     = 0x14;
    const uint8_t USB_INT_CONNECT     = 0x15;
    const uint8_t USB_INT_DISCONNECT  = 0x16;
    const uint8_t USB_INT_DISK_READ   = 0x1D;
    const uint8_t USB_INT_DISK_WRITE  = 0x1E;
    const uint8_t USB_INT_USB_READY   = 0x18;
    const uint8_t USB_INT_BUF_OVER    = 0x17;
    
    const uint8_t ERR_MISS_DIR        = 0x41;
    const uint8_t ERR_MISS_FILE       = 0x42;
    const uint8_t ERR_FOUND_NAME      = 0x43;
    
    // File attributes
    const uint8_t ATTR_DIRECTORY      = 0x10;
    const uint8_t ATTR_VOLUME_ID      = 0x08;
    const uint8_t ATTR_LONG_NAME      = 0x0F;

    enum OpenMode {
        READ_ONLY = 0,
        WRITE_ONLY = 1,
        READ_WRITE = 2
    };

    struct FileInfo {
        char name[256]; // Extended name buffer for LFN / standard strings
        bool isDir;
        uint32_t size;
    };

    // Forward declaration of classes
    class CH375FileImpl;
    class CH375FSClass;

    class CH375FileImpl {
    private:
        String _path;
        uint32_t _pos;
        uint32_t _size;
        bool _isDir;
        bool _isOpen;
        bool _enumStarted;
        friend class CH375FSClass;

    public:
        CH375FileImpl();
        CH375FileImpl(const String& path, bool isDir, uint32_t size);
        operator bool() const { return _isOpen; }
        String path() const { return _path; }
        
        int read();
        int read(uint8_t* buf, size_t size);
        size_t write(uint8_t val);
        size_t write(const uint8_t* buf, size_t size);
        int available();
        int peek();
        bool seek(uint32_t pos);
        uint32_t position() const { return _pos; }
        uint32_t size() const { return _size; }
        void close();
        bool isDirectory() const { return _isDir; }
        bool openNextFile(FileInfo& info);
        void rewindDirectory();
    };

    class CH375FSClass {
    private:
        bool _initialized;
        bool _connected;
        String _currentDir;
        int _rxPin;
        int _txPin;

        bool openPath(const String& path, OpenMode mode);

    public:
        CH375FSClass();
        bool begin(int rxPin, int txPin);
        bool isConnected() const { return _connected; }
        bool mount();
        
        CH375FileImpl open(const String& path, OpenMode mode = READ_ONLY);
        bool exists(const String& path);
        bool remove(const String& path);
        bool rename(const String& oldName, const String& newName);
        bool copy(const String& src, const String& dst);
        bool move(const String& src, const String& dst);
        bool mkdir(const String& path);
        bool rmdir(const String& path);
        
        bool changeDirectory(const String& path);
        String currentDirectory() const { return _currentDir; }
        
        uint64_t totalBytes();
        uint64_t usedBytes();
    };
}

extern CH375::CH375FSClass CH375FS;

#endif
