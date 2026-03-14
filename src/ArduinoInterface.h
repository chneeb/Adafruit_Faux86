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
#pragma once

// DISPLAY_SPI builds inject -DDISABLE_ARDUINO_TFT via platformio.ini build_flags
// to avoid a compile error in Arduino_DataBus.h on arduino-esp32 3.x (LIST_HEAD).
// DISPLAY_PARALLEL builds need Arduino_TFT enabled, so no define here.
#ifdef DISPLAY_SPI
#define DISABLE_ARDUINO_TFT
#endif

#ifndef DISABLE_ARDUINO_TFT
#include <Arduino_TFT.h>
#endif

#include "StdioDiskInterface.h"
#include <Adafruit_SPITFT.h>
#include <VM.h>
#include <freertos/semphr.h>
#include <sys/time.h>

namespace Faux86 {
class ArduinoFrameBufferInterface : public FrameBufferInterface {
public:
#ifndef DISABLE_ARDUINO_TFT
  virtual void setGfx(Arduino_TFT *gfx);
#endif
  virtual void setGfx(Adafruit_SPITFT *gfx);
  virtual RenderSurface *getSurface() override;
  virtual void setPalette(Palette *palette) override;
  virtual void blit(uint16_t *pixels, int w, int h, int stride) override;

  // Called once before starting tasks; render loop runs on Core 1
  void initSemaphore();
  void renderLoop();

private:
  RenderSurface renderSurface;

#ifndef DISABLE_ARDUINO_TFT
  Arduino_TFT *_arduino_gfx = nullptr;
#endif
  Adafruit_SPITFT *_adafruit_gfx = nullptr;

  // Ping-pong buffers: Core 0 writes _frameBuf[_writeIdx],
  // Core 1 reads _frameBuf[_pendingIdx]
  uint16_t *_frameBuf[2] = {nullptr, nullptr};
  int16_t _frameBufW = 0;
  int16_t _frameBufH = 0;
  volatile uint8_t _writeIdx = 0;
  volatile uint8_t _pendingIdx = 0;
  volatile int16_t _pendingW = 0;
  volatile int16_t _pendingH = 0;
  SemaphoreHandle_t _blitSemaphore = nullptr;
};

class ArduinoTimerInterface : public TimerInterface {
public:
  virtual uint64_t getHostFreq() override;
  virtual uint64_t getTicks() override;
};

class ArduinoAudioInterface : public AudioInterface {
public:
  virtual void init(VM &vm) override;
  virtual void shutdown() override;

private:
  static void fillAudioBuffer(void *udata, uint8_t *stream, int len);
};

class ArduinoHostSystemInterface : public HostSystemInterface {
public:
#ifndef DISABLE_ARDUINO_TFT
  ArduinoHostSystemInterface(Arduino_TFT *gfx);
#endif
  ArduinoHostSystemInterface(Adafruit_SPITFT *gfx);
  virtual ~ArduinoHostSystemInterface();
  virtual void init(VM *inVM) override;
  virtual void resize(uint32_t desiredWidth, uint32_t desiredHeight) override;

  virtual AudioInterface &getAudio() override { return audioInterface; }
  virtual FrameBufferInterface &getFrameBuffer() override {
    return frameBufferInterface;
  }
  virtual TimerInterface &getTimer() override { return timerInterface; }
  virtual DiskInterface *openFile(const char *filename) override;

  void tick();

  // Creates the semaphore and spawns the render task on Core 1.
  // Call from setup() before starting vm86_task.
  void beginRender();

  // Forwarding shim called by the render task trampoline
  void renderLoop();

private:
  ArduinoAudioInterface audioInterface;
  ArduinoFrameBufferInterface frameBufferInterface;
  ArduinoTimerInterface timerInterface;

#ifndef DISABLE_ARDUINO_TFT
  Arduino_TFT *_arduino_gfx;
#endif
  Adafruit_SPITFT *_adafruit_gfx;
};
}; // namespace Faux86
