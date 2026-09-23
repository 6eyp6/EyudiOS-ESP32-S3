// ==============================================================================
// COMMENT / UNCOMMENT TO FIT INTO LEONARDO'S LIMITED FLASH MEMORY (28KB Max)
// ==============================================================================
#define SUPPORT_PS4   // PS4 Wired Controllers
#define SUPPORT_PS2   // PS2 Controllers (via direct pins)
//#define SUPPORT_PS3  // Uncomment if you need PS3 Wired Controllers
//#define SUPPORT_XBOX // Uncomment if you need Xbox Wired Controllers
// ==============================================================================

#ifdef SUPPORT_PS4
#include <PS4USB.h>
#endif
#ifdef SUPPORT_PS3
#include <PS3USB.h>
#endif
#ifdef SUPPORT_XBOX
#include <XBOXUSB.h>
#endif
#include <usbhub.h>

#ifdef SUPPORT_PS2
#include <PS2X_lib.h>
#endif

#ifdef dobogusinclude
#include <spi4teensy3.h>
#endif
#include <SPI.h>

USB Usb;
USBHub Hub(&Usb);

// Player controller instances gated by compile switches
#ifdef SUPPORT_PS4
PS4USB PS4_1(&Usb);
PS4USB PS4_2(&Usb);
#endif
#ifdef SUPPORT_PS3
PS3USB PS3_1(&Usb);
PS3USB PS3_2(&Usb);
#endif
#ifdef SUPPORT_XBOX
XBOXUSB Xbox_1(&Usb);
XBOXUSB Xbox_2(&Usb);
#endif

#ifdef SUPPORT_PS2
PS2X ps2;
bool ps2Connected = false;
int ps2Error = -1;

// Auto-Calibration variables for PS2
int16_t centerLX = 127, centerLY = 127, centerRX = 127, centerRY = 127;
bool calibrated = false;
int calibrationSamples = 0;
long calibrationSumLX = 0, calibrationSumLY = 0, calibrationSumRX = 0, calibrationSumRY = 0;

// Filter history
float smoothLX = 127.0, smoothLY = 127.0, smoothRX = 127.0, smoothRY = 127.0;
byte lastReadLX = 127, lastReadLY = 127, lastReadRX = 127, lastReadRY = 127;
#endif

bool espVerified = false;
unsigned long lastDriverStatusTime = 0;

struct ButtonState {
  const char* name;
  bool lastState;
};

#define NUM_BUTTONS 14
ButtonState buttons[2][NUM_BUTTONS] = {
  {
    {"UP", false}, {"DOWN", false}, {"LEFT", false}, {"RIGHT", false},
    {"X", false}, {"TRIANGLE", false}, {"CIRCLE", false}, {"SQUARE", false},
    {"L1", false}, {"R1", false}, {"L2", false}, {"R2", false},
    {"SELECT", false}, {"START", false}
  },
  {
    {"UP", false}, {"DOWN", false}, {"LEFT", false}, {"RIGHT", false},
    {"X", false}, {"TRIANGLE", false}, {"CIRCLE", false}, {"SQUARE", false},
    {"L1", false}, {"R1", false}, {"L2", false}, {"R2", false},
    {"SELECT", false}, {"START", false}
  }
};

int16_t lastLX[2] = {0}, lastLY[2] = {0}, lastRX[2] = {0}, lastRY[2] = {0};
unsigned long lastAnalogTime[2] = {0};
const int DEADZONE = 18; // Keep at 18 since software overrides hardware glitches

void setup() {
  Serial.begin(115200);
  Serial1.begin(115200);

  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LOW);

  if (Usb.Init() == -1) {
    Serial1.println("SYS,USB_ERR");
  } else {
    Serial.println("USB Init OK");
  }

  #ifdef SUPPORT_PS2
  // Configure PS2 Gamepad (CLK=13, CMD=11, SEL=10, DAT=12)
  ps2Error = ps2.config_gamepad(13, 11, 10, 12, false, false);
  if (ps2Error == 0) {
    ps2Connected = true;
    Serial.println("PS2 Controller configured successfully");
  } else {
    Serial.print("PS2 Controller init failed with error code: ");
    Serial.println(ps2Error);
  }
  #endif
}

void loop() {
  Usb.Task();

  // Handshake phase
  if (!espVerified) {
    unsigned long now = millis();
    if (now - lastDriverStatusTime >= 500) {
      lastDriverStatusTime = now;
      Serial1.println("SYS,DRV:CONTROLLER");
    }

    if (Serial1.available()) {
      String resp = Serial1.readStringUntil('\n');
      resp.trim();
      if (resp == "SYS,ACK") {
        espVerified = true;
        digitalWrite(LED_BUILTIN, HIGH);
        Serial.println("ESP32 handshake successful!");
      }
    }
    delay(1);
    return;
  }

  // Player 1 bindings
  bool p1Active = false;
  #ifdef SUPPORT_PS4
  if (PS4_1.connected()) {
    handlePS4(1, PS4_1);
    p1Active = true;
  }
  #endif
  #ifdef SUPPORT_PS3
  if (!p1Active && PS3_1.PS3Connected) {
    handlePS3(1, PS3_1);
    p1Active = true;
  }
  #endif
  #ifdef SUPPORT_XBOX
  if (!p1Active && Xbox_1.XboxConnected) {
    handleXbox(1, Xbox_1);
    p1Active = true;
  }
  #endif
  #ifdef SUPPORT_PS2
  if (!p1Active && ps2Connected) {
    handlePS2(1);
    p1Active = true;
  }
  #endif

  if (!p1Active) {
    resetPlayer(1);
  }

  // Player 2 bindings
  bool p2Active = false;
  #ifdef SUPPORT_PS4
  if (PS4_2.connected()) {
    handlePS4(2, PS4_2);
    p2Active = true;
  }
  #endif
  #ifdef SUPPORT_PS3
  if (!p2Active && PS3_2.PS3Connected) {
    handlePS3(2, PS3_2);
    p2Active = true;
  }
  #endif
  #ifdef SUPPORT_XBOX
  if (!p2Active && Xbox_2.XboxConnected) {
    handleXbox(2, Xbox_2);
    p2Active = true;
  }
  #endif

  if (!p2Active) {
    resetPlayer(2);
  }

  delay(1);
}

void sendButton(int player, const char* name, bool pressed) {
  Serial1.print("GP,");
  Serial1.print(player);
  Serial1.print(",");
  Serial1.print(name);
  Serial1.print(",");
  Serial1.println(pressed ? "1" : "0");
}

void checkButton(int player, int index, bool currentState) {
  int pIdx = player - 1;
  if (currentState != buttons[pIdx][index].lastState) {
    buttons[pIdx][index].lastState = currentState;
    sendButton(player, buttons[pIdx][index].name, currentState);
  }
}

void checkAnalog(int player, int16_t lx, int16_t ly, int16_t rx, int16_t ry) {
  int pIdx = player - 1;
  if (abs(lx) < DEADZONE) lx = 0;
  if (abs(ly) < DEADZONE) ly = 0;
  if (abs(rx) < DEADZONE) rx = 0;
  if (abs(ry) < DEADZONE) ry = 0;

  unsigned long now = millis();
  if (now - lastAnalogTime[pIdx] >= 16) {
    if (lx != lastLX[pIdx] || ly != lastLY[pIdx] || rx != lastRX[pIdx] || ry != lastRY[pIdx]) {
      lastLX[pIdx] = lx;
      lastLY[pIdx] = ly;
      lastRX[pIdx] = rx;
      lastRY[pIdx] = ry;
      lastAnalogTime[pIdx] = now;

      Serial1.print("GPA,");
      Serial1.print(player);
      Serial1.print(",");
      Serial1.print(lx);
      Serial1.print(",");
      Serial1.print(ly);
      Serial1.print(",");
      Serial1.print(rx);
      Serial1.print(",");
      Serial1.println(ry);
    }
  }
}

void resetPlayer(int player) {
  int pIdx = player - 1;
  for (int i = 0; i < NUM_BUTTONS; i++) {
    if (buttons[pIdx][i].lastState) {
      buttons[pIdx][i].lastState = false;
      sendButton(player, buttons[pIdx][i].name, false);
    }
  }
}

#ifdef SUPPORT_PS4
void handlePS4(int player, PS4USB &controller) {
  checkButton(player, 0, controller.getButtonPress(UP));
  checkButton(player, 1, controller.getButtonPress(DOWN));
  checkButton(player, 2, controller.getButtonPress(LEFT));
  checkButton(player, 3, controller.getButtonPress(RIGHT));
  
  checkButton(player, 4, controller.getButtonPress(CROSS));
  checkButton(player, 5, controller.getButtonPress(TRIANGLE));
  checkButton(player, 6, controller.getButtonPress(CIRCLE));
  checkButton(player, 7, controller.getButtonPress(SQUARE));
  
  checkButton(player, 8, controller.getButtonPress(L1));
  checkButton(player, 9, controller.getButtonPress(R1));
  checkButton(player, 10, controller.getButtonPress(L2));
  checkButton(player, 11, controller.getButtonPress(R2));
  
  checkButton(player, 12, controller.getButtonPress(SHARE));
  checkButton(player, 13, controller.getButtonPress(OPTIONS));

  int16_t lx = (int16_t)controller.getAnalogHat(LeftHatX) - 127;
  int16_t ly = (int16_t)controller.getAnalogHat(LeftHatY) - 127;
  int16_t rx = (int16_t)controller.getAnalogHat(RightHatX) - 127;
  int16_t ry = (int16_t)controller.getAnalogHat(RightHatY) - 127;
  checkAnalog(player, lx, ly, rx, ry);
}
#endif

#ifdef SUPPORT_PS3
void handlePS3(int player, PS3USB &controller) {
  checkButton(player, 0, controller.getButtonPress(UP));
  checkButton(player, 1, controller.getButtonPress(DOWN));
  checkButton(player, 2, controller.getButtonPress(LEFT));
  checkButton(player, 3, controller.getButtonPress(RIGHT));
  
  checkButton(player, 4, controller.getButtonPress(CROSS));
  checkButton(player, 5, controller.getButtonPress(TRIANGLE));
  checkButton(player, 6, controller.getButtonPress(CIRCLE));
  checkButton(player, 7, controller.getButtonPress(SQUARE));
  
  checkButton(player, 8, controller.getButtonPress(L1));
  checkButton(player, 9, controller.getButtonPress(R1));
  checkButton(player, 10, controller.getButtonPress(L2));
  checkButton(player, 11, controller.getButtonPress(R2));
  
  checkButton(player, 12, controller.getButtonPress(SELECT));
  checkButton(player, 13, controller.getButtonPress(START));

  int16_t lx = (int16_t)controller.getAnalogHat(LeftHatX) - 127;
  int16_t ly = (int16_t)controller.getAnalogHat(LeftHatY) - 127;
  int16_t rx = (int16_t)controller.getAnalogHat(RightHatX) - 127;
  int16_t ry = (int16_t)controller.getAnalogHat(RightHatY) - 127;
  checkAnalog(player, lx, ly, rx, ry);
}
#endif

#ifdef SUPPORT_XBOX
void handleXbox(int player, XBOXUSB &controller) {
  checkButton(player, 0, controller.getButtonPress(UP));
  checkButton(player, 1, controller.getButtonPress(DOWN));
  checkButton(player, 2, controller.getButtonPress(LEFT));
  checkButton(player, 3, controller.getButtonPress(RIGHT));
  
  checkButton(player, 4, controller.getButtonPress(A));
  checkButton(player, 5, controller.getButtonPress(Y));
  checkButton(player, 6, controller.getButtonPress(B));
  checkButton(player, 7, controller.getButtonPress(X));
  
  checkButton(player, 8, controller.getButtonPress(L1));
  checkButton(player, 9, controller.getButtonPress(R1));
  checkButton(player, 10, controller.getButtonPress(L2) > 50);
  checkButton(player, 11, controller.getButtonPress(R2) > 50);
  
  checkButton(player, 12, controller.getButtonPress(BACK));
  checkButton(player, 13, controller.getButtonPress(START));

  int16_t lx = controller.getAnalogHat(LeftHatX) / 256;
  int16_t ly = -controller.getAnalogHat(LeftHatY) / 256;
  int16_t rx = controller.getAnalogHat(RightHatX) / 256;
  int16_t ry = -controller.getAnalogHat(RightHatY) / 256;
  checkAnalog(player, lx, ly, rx, ry);
}
#endif

#ifdef SUPPORT_PS2
void handlePS2(int player) {
  if (!ps2.read_gamepad(false, 0)) {
    resetPlayer(player);
    return;
  }

  if (ps2.Button(PSB_START) && ps2.Button(PSB_SELECT) && ps2.Button(PSB_PAD_UP) && ps2.Button(PSB_PAD_DOWN)) {
    resetPlayer(player);
    return;
  }

  checkButton(player, 0, ps2.Button(PSB_PAD_UP));
  checkButton(player, 1, ps2.Button(PSB_PAD_DOWN));
  checkButton(player, 2, ps2.Button(PSB_PAD_LEFT));
  checkButton(player, 3, ps2.Button(PSB_PAD_RIGHT));
  
  checkButton(player, 4, ps2.Button(PSB_BLUE));
  checkButton(player, 5, ps2.Button(PSB_GREEN));
  checkButton(player, 6, ps2.Button(PSB_RED));
  checkButton(player, 7, ps2.Button(PSB_PINK));
  
  checkButton(player, 8, ps2.Button(PSB_L1));
  checkButton(player, 9, ps2.Button(PSB_R1));
  checkButton(player, 10, ps2.Button(PSB_L2));
  checkButton(player, 11, ps2.Button(PSB_R2));
  
  checkButton(player, 12, ps2.Button(PSB_SELECT));
  checkButton(player, 13, ps2.Button(PSB_START));

  // Get raw analog readings
  byte lx_raw = ps2.Analog(PSS_LX);
  byte ly_raw = ps2.Analog(PSS_LY);
  byte rx_raw = ps2.Analog(PSS_RX);
  byte ry_raw = ps2.Analog(PSS_RY);

  // Individual Hardware Fault Filter: If any single axis drops to absolute extremes (< 10 or > 245)
  // while other axes are centered, pull it back to the calibrated center.
  if (lx_raw < 10 || lx_raw > 245) lx_raw = centerLX;
  if (ly_raw < 10 || ly_raw > 245) ly_raw = centerLY;
  if (rx_raw < 10 || rx_raw > 245) rx_raw = centerRX;
  if (ry_raw < 10 || ry_raw > 245) ry_raw = centerRY;

  // Glitch Filter: Discard sudden impossible changes (> 65)
  if (abs((int16_t)lx_raw - lastReadLX) > 65) lx_raw = lastReadLX;
  if (abs((int16_t)ly_raw - lastReadLY) > 65) ly_raw = lastReadLY;
  if (abs((int16_t)rx_raw - lastReadRX) > 65) rx_raw = lastReadRX;
  if (abs((int16_t)ry_raw - lastReadRY) > 65) ry_raw = lastReadRY;

  lastReadLX = lx_raw;
  lastReadLY = ly_raw;
  lastReadRX = rx_raw;
  lastReadRY = ry_raw;

  // If ANALOG mode is off on the PS2 controller, readings stay at 0 or 255.
  // We filter out these digital-mode values to prevent phantom stick drift.
  if ((lx_raw == 0 || lx_raw == 255) && (ly_raw == 0 || ly_raw == 255)) {
    lx_raw = centerLX;
    ly_raw = centerLY;
  }
  if ((rx_raw == 0 || rx_raw == 255) && (ry_raw == 0 || ry_raw == 255)) {
    rx_raw = centerRX;
    ry_raw = centerRY;
  }

  // Software Low-Pass Filter (Exponential Moving Average) to eliminate jitter
  smoothLX = smoothLX * 0.70f + lx_raw * 0.30f;
  smoothLY = smoothLY * 0.70f + ly_raw * 0.30f;
  smoothRX = smoothRX * 0.70f + rx_raw * 0.30f;
  smoothRY = smoothRY * 0.70f + ry_raw * 0.30f;

  // Subtract calibrated centers to get clean offset values
  int16_t lx = (int16_t)smoothLX - centerLX;
  int16_t ly = (int16_t)smoothLY - centerLY;
  int16_t rx = (int16_t)smoothRX - centerRX;
  int16_t ry = (int16_t)smoothRY - centerRY;

  checkAnalog(player, lx, ly, rx, ry);
}
#endif
