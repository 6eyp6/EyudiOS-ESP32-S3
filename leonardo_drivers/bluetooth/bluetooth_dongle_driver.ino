#include <PS4BT.h>
#include <PS3BT.h>
#include <usbhub.h>

#ifdef dobogusinclude
#include <spi4teensy3.h>
#endif
#include <SPI.h>

USB Usb;
BTD Btd(&Usb);

// Multi-player Bluetooth instances
PS4BT PS4_1(&Btd);
PS4BT PS4_2(&Btd);
PS3BT PS3_1(&Btd);
PS3BT PS3_2(&Btd);

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
const int DEADZONE = 15;

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
}

void loop() {
  Usb.Task();

  // Handshake phase
  if (!espVerified) {
    unsigned long now = millis();
    if (now - lastDriverStatusTime >= 500) {
      lastDriverStatusTime = now;
      Serial1.println("SYS,DRV:BTDONGLE");
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

  // Handle Player 1
  if (PS4_1.connected()) {
    handlePS4(1, PS4_1);
  } else if (PS3_1.PS3Connected) { // Fixed: using PS3Connected
    handlePS3(1, PS3_1);
  } else {
    resetPlayer(1);
  }

  // Handle Player 2
  if (PS4_2.connected()) {
    handlePS4(2, PS4_2);
  } else if (PS3_2.PS3Connected) { // Fixed: using PS3Connected
    handlePS3(2, PS3_2);
  } else {
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

void handlePS4(int player, PS4BT &controller) {
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

void handlePS3(int player, PS3BT &controller) {
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
