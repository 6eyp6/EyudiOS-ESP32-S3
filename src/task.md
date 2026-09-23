# Task List: esp32-doom Integration

- `[x]` Setup build configurations in `platformio.ini`
- `[x]` Copy and integrate core `esp32-doom` source files into `src/doom/`
- `[x]` Refactor memory allocations in Doom source to use PSRAM (`ps_malloc` / `MALLOC_CAP_SPIRAM`)
- `[x]` Implement display bridge (translate Doom's 8-bit palette to VGA RGB565) in `DoomEngine.cpp`
- `[x]` Redirect WAD file reading via POSIX / FatFS SD card interface
- `[x]` Map keyboard and mouse inputs from `processUSBInput()` to Doom events
- `[x]` Verify compilation and execution without crashing
