/*
        Faux86: A portable, open-source 8086 PC emulator.
        Copyright (C)2018 James Howard
        Based on Fake86
        Copyright (C)2010-2013 Mike Chambers

        Contributions and Updates (c)2023 Curtis aka ArnoldUK

        This program is free software; you can redistribute it and/or
        modify it under the terms of the GNU General Public License
        as published by the Free Software Foundation; either version 2
        of the License, or (at your option) any later version.

        This program is distributed in the hope that it will be useful,
        but WITHOUT ANY WARRANTY; without even the implied warranty of
        MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
        GNU General Public License for more details.

        You should have received a copy of the GNU General Public License
        along with this program; if not, write to the Free Software
        Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
   02110-1301, USA.
*/

#include "ArduinoInterface.h"
#include <VM.h>

#include "esp32-hal.h"

using namespace Faux86;

bool sdlconsole_ctrl = 0;
bool sdlconsole_alt = 0;

#ifndef DISABLE_ARDUINO_TFT
ArduinoHostSystemInterface::ArduinoHostSystemInterface(Arduino_TFT *gfx)
    : _arduino_gfx(gfx) {
  log_d("ArduinoHostSystemInterface::ArduinoHostSystemInterface()");
}
#endif

ArduinoHostSystemInterface::ArduinoHostSystemInterface(Adafruit_SPITFT *gfx)
    : _adafruit_gfx(gfx) {}

void ArduinoHostSystemInterface::init(VM *inVM) {
  log_d("ArduinoHostSystemInterface::init(VM *inVM)");
#ifndef DISABLE_ARDUINO_TFT
  frameBufferInterface.setGfx(_arduino_gfx);
#endif
  frameBufferInterface.setGfx(_adafruit_gfx);
}

void ArduinoHostSystemInterface::resize(uint32_t desiredWidth,
                                        uint32_t desiredHeight) {
  // log_d("ArduinoHostSystemInterface::resize(%d, %d)", desiredWidth,
  // desiredHeight);
}

void ArduinoAudioInterface::init(VM &vm) {
  log_d("ArduinoAudioInterface::init(VM &vm)");
}

void ArduinoAudioInterface::shutdown() {
  log_d("ArduinoAudioInterface::shutdown()");
}

void ArduinoAudioInterface::fillAudioBuffer(void *udata, uint8_t *stream,
                                            int len) {
  log_d("ArduinoAudioInterface::fillAudioBuffer(void *udata, uint8_t *stream, "
        "%d)",
        len);
}

ArduinoHostSystemInterface::~ArduinoHostSystemInterface() {
  log_d("ArduinoHostSystemInterface::~ArduinoHostSystemInterface()");
}

DiskInterface *ArduinoHostSystemInterface::openFile(const char *filename) {
  log_d("ArduinoHostSystemInterface::openFile(%s)", filename);
  return new StdioDiskInterface(filename);
}

#ifndef DISABLE_ARDUINO_TFT
void ArduinoFrameBufferInterface::setGfx(Arduino_TFT *gfx) {
  log_d("ArduinoFrameBufferInterface::setGfx(Arduino_TFT *gfx)");
  _arduino_gfx = gfx;
}
#endif

void ArduinoFrameBufferInterface::setGfx(Adafruit_SPITFT *gfx) {
  _adafruit_gfx = gfx;
}

RenderSurface *ArduinoFrameBufferInterface::getSurface() {
  log_d("ArduinoFrameBufferInterface::getSurface()");
  return &renderSurface;
}

void ArduinoFrameBufferInterface::setPalette(Palette *palette) {
  log_d("ArduinoFrameBufferInterface::setPalette(Palette *palette)");
}

void ArduinoFrameBufferInterface::initSemaphore() {
  _blitSemaphore = xSemaphoreCreateBinary();
}

void ArduinoFrameBufferInterface::blit(uint16_t *pixels, int w, int h,
                                       int stride) {
  // log_d("ArduinoFrameBufferInterface::blit(uint32_t *pixels, %d, %d, %d)", w,
  // h, stride);

  if (!_blitSemaphore) return;

#ifdef DEBUG_TIMING
  static uint8_t blit_fps = 0;
  static unsigned long next_10secound = 0;
  ++blit_fps;
  if (millis() > next_10secound) {
    log_d("blit_fps: %.1f", blit_fps / 10.0);
    blit_fps = 0;
    next_10secound = ((millis() / 1000) + 10) * 1000;
  }
  return;
#endif

  int16_t hOut = h / 2;
  int16_t wOut = w / 2;

  // Reallocate both ping-pong buffers if video mode resolution changes
  if (_frameBufW != wOut || _frameBufH != hOut) {
    if (_frameBuf[0]) { free(_frameBuf[0]); _frameBuf[0] = nullptr; }
    if (_frameBuf[1]) { free(_frameBuf[1]); _frameBuf[1] = nullptr; }
    _frameBuf[0] = (uint16_t *)malloc(wOut * hOut * sizeof(uint16_t));
    _frameBuf[1] = (uint16_t *)malloc(wOut * hOut * sizeof(uint16_t));
    _frameBufW = wOut;
    _frameBufH = hOut;
    _writeIdx = 0;
  }
  if (!_frameBuf[0] || !_frameBuf[1]) return;

  // Nearest-neighbour 2x downscale into the write buffer (Core 0 only)
  uint8_t wi = _writeIdx;
  uint16_t *src = pixels;
  uint32_t rowSkip = VGA_FRAMEBUFFER_WIDTH + (VGA_FRAMEBUFFER_WIDTH - w);
  uint16_t *dst = _frameBuf[wi];

  for (int16_t y = 0; y < hOut; y++) {
    for (int16_t i = 0; i < wOut; i++) {
      *dst++ = *src;
      src += 2;
    }
    src += rowSkip;
  }

  // Publish the completed frame and flip the write index
  _pendingW = wOut;
  _pendingH = hOut;
  _pendingIdx = wi;
  _writeIdx = wi ^ 1;

  // Signal Core 1 (binary semaphore: extra gives are silently dropped)
  xSemaphoreGive(_blitSemaphore);

  // Yield to IDLE0: previously writePixels() on Core 0 had implicit yields
  // inside the parallel bus transfer; now blit is pure memory work so we must
  // yield explicitly to prevent the task watchdog from firing.
  vTaskDelay(1);
}

// Runs on Core 1: waits for a completed frame and sends it to the display.
void ArduinoFrameBufferInterface::renderLoop() {
  while (1) {
    if (xSemaphoreTake(_blitSemaphore, portMAX_DELAY) != pdTRUE) continue;

    uint8_t idx = _pendingIdx;
    int16_t w   = _pendingW;
    int16_t h   = _pendingH;
    uint16_t *buf = _frameBuf[idx];
    if (!buf || w == 0 || h == 0) continue;

    if (_adafruit_gfx) {
      _adafruit_gfx->startWrite();
      _adafruit_gfx->setAddrWindow(0, 0, w, h);
      _adafruit_gfx->writePixels(buf, w * h);
      _adafruit_gfx->endWrite();
    }
#ifndef DISABLE_ARDUINO_TFT
    else if (_arduino_gfx) {
      _arduino_gfx->startWrite();
      _arduino_gfx->writeAddrWindow(0, 0, w, h);
      _arduino_gfx->writePixels(buf, w * h);
      _arduino_gfx->endWrite();
    }
#endif
  }
}

static void render_task_fn(void *param) {
  static_cast<ArduinoHostSystemInterface *>(param)->renderLoop();
}

void ArduinoHostSystemInterface::renderLoop() {
  frameBufferInterface.renderLoop();
}

void ArduinoHostSystemInterface::beginRender() {
  frameBufferInterface.initSemaphore();
  xTaskCreateUniversal(render_task_fn, "render", 4096, this, 1, nullptr, 1);
}

uint64_t ArduinoTimerInterface::getHostFreq() {
  // log_v("ArduinoTimerInterface::getHostFreq()");
  return CONFIG_FREERTOS_HZ;
}

uint64_t ArduinoTimerInterface::getTicks() {
  // log_d("ArduinoTimerInterface::getTicks(): %d", xTaskGetTickCount());
  return xTaskGetTickCount();
}

void ArduinoHostSystemInterface::tick() {
  // log_d("ArduinoHostSystemInterface::tick");
}

void Faux86::log(Faux86::LogChannel channel, const char *message, ...) {
  if (ARDUHAL_LOG_LEVEL > channel) {
    va_list args;
    va_start(args, message);
    log_printf(message, args);
    log_printf("\n");
    va_end(args);
  }
}
