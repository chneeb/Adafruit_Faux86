#pragma once
// Parallel ILI9341 via Arduino_GFX
// Included by esp32-faux86.ino inside #ifdef DISPLAY_PARALLEL
#include <Arduino_GFX_Library.h>
#define TFT_RST      5
#define TFT_BL      -1
#define TFT_ROTATION  1
Arduino_DataBus *bus = new Arduino_ESP32PAR8(7 /* DC */, 6 /* CS */, 1 /* WR */, 2 /* RD */,
                                              21, 46, 18, 17, 19, 20, 3, 14 /* D0-D7 */);
Arduino_TFT *gfx = new Arduino_ILI9341(bus, TFT_RST);
