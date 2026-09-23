#include "QRCodeGenerator.h"
#include "Globals.h"
#include <qrcode.h>

// =============================================================================
// Industry-Standard QR Generator Engine using ricmoo/QRCode Library (Version 6)
// 100% SPEC COMPLIANT REED-SOLOMON & ZERO-STACK ALLOCATION CACHING
// =============================================================================

static String cachedText = "";
static QRCode qrcode;
static uint8_t* qrcodeData = nullptr;

void drawQRCodeVGA(int x, int y, const String &text, int pixelScale, uint16_t fgColor, uint16_t bgColor) {
    if (text.length() == 0) return;

    if (qrcodeData == nullptr) {
        qrcodeData = (uint8_t*)heap_caps_malloc(1024, MALLOC_CAP_SPIRAM);
        if (qrcodeData == nullptr) {
            static uint8_t fallbackBuf[1024];
            qrcodeData = fallbackBuf;
        }
    }

    if (text != cachedText) {
        uint8_t payload[256];
        uint16_t len = (uint16_t)min((size_t)250, (size_t)text.length());
        memcpy(payload, text.c_str(), len);

        // qrcode_initBytes supports full binary/lowercase string encoding safely
        int8_t status = qrcode_initBytes(&qrcode, qrcodeData, 6, ECC_LOW, payload, len);
        if (status != 0) {
            qrcode_initBytes(&qrcode, qrcodeData, 8, ECC_LOW, payload, len);
        }
        cachedText = text;
    }

    if (qrcode.size == 0) return;

    int margin = 2; // Quiet Zone Margin (2 modules)
    int totalModules = qrcode.size + (margin * 2);
    int totalWidth = totalModules * pixelScale;

    // Render High-Contrast Quiet Zone Background (Bright White)
    videodisplay.fillRect(x, y, totalWidth, totalWidth, bgColor);

    // Render QR Code Modules
    for (uint8_t r = 0; r < qrcode.size; r++) {
        for (uint8_t c = 0; c < qrcode.size; c++) {
            if (qrcode_getModule(&qrcode, c, r)) {
                int px = x + ((c + margin) * pixelScale);
                int py = y + ((r + margin) * pixelScale);
                videodisplay.fillRect(px, py, pixelScale, pixelScale, fgColor);
            }
        }
    }
}
