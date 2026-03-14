#pragma once
// SPI ILI9341 via Adafruit_ILI9341
// Included by esp32-faux86.ino inside #elif defined(DISPLAY_SPI)
// SPI2 default pins on ESP32-S3: SCK=12, MISO=13, MOSI=11
#include "Adafruit_ILI9341.h"
#define TFT_DC       7
#define TFT_CS       10
#define TFT_RST      6
#define TFT_BL       15
#define TFT_ROTATION  1
#define TFT_SPEED_HZ (60 * 1000 * 1000ul)
SPIClass spi2(FSPI);  // shared by ILI9341 + XPT2046
SPIClass spi3(HSPI);  // SD card
Adafruit_ILI9341 *gfx = new Adafruit_ILI9341(&spi2, TFT_DC, TFT_CS, TFT_RST);
