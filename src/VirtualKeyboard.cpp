#include "Globals.h"

void showVirtualKeyboardWithCallback(String title, String initialText, VirtualKeyboardCallback callback) {
  vkbTitle = title; keyboardInput = initialText; keyboardCursor = initialText.length(); vkbCallback = callback;
  showVirtualKeyboard = true; vkbActive = true; staticBackgroundDrawn = false; keyboardNeedsRedraw = true;
  keyboardKeyRows[0] = "1234567890"; keyboardKeyRows[1] = "qwertyuiop"; keyboardKeyRows[2] = "asdfghjkl"; keyboardKeyRows[3] = "zxcvbnm";
  for (int i = 0; i < 4; i++) keyboardRowLengths[i] = keyboardKeyRows[i].length();
}

void closeVirtualKeyboard() { showVirtualKeyboard = false; vkbActive = false; vkbCallback = NULL; staticBackgroundDrawn = false; desktopNeedsRedraw = true; }

void handleVirtualKeyboardClick() {} // Already handled in main loop or Desktop.cpp?

void handleVirtualKeyboardPress() {
  if (keyboardSelectedRow < 4) {
    char key = keyboardKeyRows[keyboardSelectedRow][keyboardSelectedCol];
    if (keyboardShift) key = toupper(key); keyboardInput += key;
  } else {
    switch (keyboardSelectedCol) {
      case 0: keyboardShift = !keyboardShift; break;
      case 1: if (keyboardInput.length() > 0) keyboardInput.remove(keyboardInput.length() - 1); break;
      case 2: keyboardInput += " "; break;
      case 3: if (vkbCallback) vkbCallback(keyboardInput); closeVirtualKeyboard(); break;
      case 4: closeVirtualKeyboard(); break;
    }
  }
  keyboardNeedsRedraw = true;
}

void handleVirtualKeyboardInput(ParsedKey key) {
  if (key.printableChar != 0) {
    keyboardInput += key.printableChar;
    keyboardNeedsRedraw = true;
  } else if (key.isBackspace) {
    if (keyboardInput.length() > 0) keyboardInput.remove(keyboardInput.length() - 1);
    keyboardNeedsRedraw = true;
  } else if (key.normalizedKey == "a" || (key.isArrow && key.arrowDirection == 2)) { 
    keyboardSelectedCol = (keyboardSelectedCol - 1 + keyboardRowLengths[keyboardSelectedRow]) % keyboardRowLengths[keyboardSelectedRow]; keyboardNeedsRedraw = true; 
  }
  else if (key.normalizedKey == "d" || (key.isArrow && key.arrowDirection == 3)) { 
    keyboardSelectedCol = (keyboardSelectedCol + 1) % keyboardRowLengths[keyboardSelectedRow]; keyboardNeedsRedraw = true; 
  }
  else if (key.normalizedKey == "w" || (key.isArrow && key.arrowDirection == 0)) { 
    keyboardSelectedRow = (keyboardSelectedRow - 1 + 5) % 5; keyboardSelectedCol = min(keyboardSelectedCol, keyboardRowLengths[keyboardSelectedRow] - 1); keyboardNeedsRedraw = true; 
  }
  else if (key.normalizedKey == "s" || (key.isArrow && key.arrowDirection == 1)) { 
    keyboardSelectedRow = (keyboardSelectedRow + 1) % 5; keyboardSelectedCol = min(keyboardSelectedCol, keyboardRowLengths[keyboardSelectedRow] - 1); keyboardNeedsRedraw = true; 
  }
  else if (key.isEnter) { handleVirtualKeyboardPress(); }
  else if (key.isEscape) { closeVirtualKeyboard(); }
}
