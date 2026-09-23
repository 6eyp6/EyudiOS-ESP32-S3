#ifndef QRCODE_GENERATOR_H
#define QRCODE_GENERATOR_H

#include <Arduino.h>

/**
 * @brief Draws a QR code onto the VGA display.
 * 
 * @param x Top-left X position on screen
 * @param y Top-left Y position on screen
 * @param text Content string to encode into QR Code
 * @param pixelScale Size of each QR module pixel block in screen pixels (default: 4)
 * @param fgColor Color for QR dark modules (default: Black / 0)
 * @param bgColor Color for QR light modules/margin (default: White / 0xFFFF)
 */
void drawQRCodeVGA(int x, int y, const String &text, int pixelScale = 4, uint16_t fgColor = 0, uint16_t bgColor = 0xFFFF);

#endif // QRCODE_GENERATOR_H
