#ifndef DOOM_ENGINE_H
#define DOOM_ENGINE_H

#include <Arduino.h>

class DoomEngine {
public:
    DoomEngine();
    bool init(String wadPath);
    void update(float deltaTime);
    void render(int x, int y, int w, int h);
    void handleInput(String key, bool isDown);
    void stop();
    void toggleFullscreen();
    
    volatile bool isRunning = false;
    String currentWadPath = "";
    bool fullscreen = true;
};

extern DoomEngine eyeDoom;

#endif // DOOM_ENGINE_H
