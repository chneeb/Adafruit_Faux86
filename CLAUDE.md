# Adafruit Faux86 — Claude Code Notes

## Project Overview
Arduino library wrapping [Faux86-remake](https://github.com/moononournation/Faux86-remake), an 8086 PC emulator targeting ESP32-S3. The main example runs DOS games (e.g. Prince of Persia) on an ILI9341 TFT with a CardKB I2C keyboard.

## Key Files
- `src/` — Arduino library wrapper (ArduinoInterface, StdioDiskInterface, Keymap, embedded ROM headers)
- `examples/esp32-faux86/esp32-faux86.ino` — main sketch; all VM config lives here
- `examples/esp32-faux86/cardkb.h` — CardKB I2C keyboard polling and XT scan code translation
- `examples/esp32-faux86/usbhid_espusbhost.h` — EspUsbHost subclass; overrides `onKeyboard()` to translate HID reports to XT scan codes
- `examples/esp32-faux86/touch.h` — touchscreen input (XPT2046, CS=GPIO4, polled mode, active — translates to serial mouse events)
- `disks/` — disk images (.img) for the VM; upload one to FFat as `hd0_12m_games.img`

## Architecture
- The Faux86 VM runs in a dedicated FreeRTOS task (`vm86_task`) pinned to **Core 0**, priority 10
- **`render` task** (Core 1, priority 1) — calls `writePixels()` to blit to the TFT; spawned by `hostInterface.beginRender()` in `setup()` before `vm86_task`
- `loop()` (Core 1, priority 1) polls the CardKB and XPT2046 touch; same priority as `render` task so they time-share Core 1 without input lag
- `vm86` is a global `Faux86::VM*` pointer; `cardkb.h` uses it directly, so it must be included after `vm86` is declared
- `handleKeyDown/Up(uint16_t xt)` only use the **low byte** of the XT scan code — the `0xe0` extended prefix is silently discarded. Arrow keys arrive as numpad scan codes (0x4b/0x4d/0x48/0x50), which DOS games treat identically.

## CardKB Key Codes (v1.1 firmware)
All four arrow codes are **different from the datasheet** on real hardware — only Left matches:
| Physical key | CardKB byte | XT scan (low byte sent) |
|---|---|---|
| Left  | 0xB4 | 0x4b |
| Up    | 0xB5 | 0x48 |
| Down  | 0xB6 | 0x50 |
| Right | 0xB7 | 0x4d |

## Arduino IDE Board Settings
| Setting | Value |
|---|---|
| Board | ESP32S3 Dev Module |
| USB CDC On Boot | Enable |
| Flash Size | 16MB |
| Partition Scheme | 16M Flash (2M APP/12.5MB FATFS) |
| PSRAM | OPI PSRAM |
| Arduino Runs On | Core 1 (default) |
| Events Run On | Core 0 (default) |

**Note:** Changing partition scheme erases FFat — re-upload the disk image afterwards.

## VM Configuration (esp32-faux86.ino)
| Setting | Value | Notes |
|---|---|---|
| `cpuSpeed` | 0 | 0 = unlimited; already maxed out |
| `slowSystem` | true | Currently set true in sketch |
| `enableAudio` | false | Enabling costs significant CPU |
| `usePCSpeaker` | false | Only set true when enableAudio = true |
| `frameDelay` | 60 ms | See frameDelay notes below |
| `framebuffer.width/height` | 720×480 | Only affects secondary render surface, NOT the VGA blit path |

## frameDelay / Watchdog Notes
`frameDelay` controls how often Core 0 downscales the VGA framebuffer and signals Core 1 to blit.
Core 0 cost per frame: ~1ms downscale + 1ms `vTaskDelay(1)` (explicit yield to keep IDLE0 alive).
Core 1 cost per frame: `writePixels()` to the ILI9341 parallel bus (~10–15ms).

The `vTaskDelay(1)` at the end of `blit()` is essential — without it, IDLE0 never runs and the task watchdog fires. Previously `writePixels()` on Core 0 provided implicit yields via the parallel bus driver; moving it to Core 1 removed those.

Tested values with Core 1 rendering:
| frameDelay | Result |
|---|---|
| 60 ms (~16.7 FPS) | Works — current setting |
| 80 ms (~12.5 FPS) | Works |
| 100 ms | Works |
| 150 ms | Works |
| 50 ms | Watchdog crash (too fast before vTaskDelay fix was added) |

## blit() Implementation (src/ArduinoInterface.cpp)
The blit function downscales the VGA framebuffer 2×2→1 on Core 0, then signals Core 1 to send it to the TFT.
Key details:
- Uses **hardcoded `VGA_FRAMEBUFFER_WIDTH=800`** (from `Faux86-remake/src/Video.h`) as the stride — `vmConfig.framebuffer.width/height` does NOT affect this
- **Ping-pong buffers**: two `malloc()` buffers (internal SRAM, ~32KB each for 320×200). Core 0 writes to `_frameBuf[_writeIdx]`, Core 1 reads from `_frameBuf[_pendingIdx]`. Indices flip on each blit so neither core touches the same buffer simultaneously (safe because `frameDelay` ≥ 60ms >> writePixels ~15ms).
- **Nearest-neighbour scaling**: takes the top-left pixel of each 2×2 block instead of averaging all four.
- **Binary semaphore**: Core 0 gives after downscale; Core 1 takes before `writePixels()`. Extra gives are dropped (frames skipped), never queued.
- **`vTaskDelay(1)` at end of blit()**: explicit yield to keep IDLE0 alive on Core 0.

## render Task (Core 1)
- Spawned by `ArduinoHostSystemInterface::beginRender()`, called from `setup()` before `vm86_task`
- Pinned to Core 1, **priority 1** (same as Arduino loop task) — prevents render from preempting CardKB polling and causing input lag
- Blocks on `_blitSemaphore` when idle; wakes only when Core 0 signals a new frame

## Performance
Core 0: 100% dedicated to 8086 emulation except ~2ms per `frameDelay` cycle (downscale + yield).
Core 1: `writePixels()` (~10–15ms) interleaved with CardKB polling via FreeRTOS round-robin.
Display FPS = 1000 / frameDelay. At 60ms → ~16.7 FPS, well-suited to DOS-era games.

Other performance knobs:
- `cpuSpeed=0` — already unlimited
- ESP32-S3 LX7 at 240MHz — already maximum supported clock; overclocking poorly documented

## Hardware (esp32-faux86 example)
- Board: ESP32-S3 Dev Module with OPI PSRAM
- Display: ILI9341 via SPI (SPI2: SCK=12, MISO=13, MOSI=11, CS=10, DC=7), 320×240, rotation 1
- Keyboard: **INPUT_USB_HID_ESPUSBHOST** (default) — EspUsbHost on native OTG port (GPIO19/20); or **INPUT_USB_HID_TINYUSB** (legacy TinyUSB); or **INPUT_CARDKB** — M5Stack CardKB v1.1 on I2C (SDA=8, SCL=9, addr=0x5F)
- Touch: XPT2046 on SPI2 bus (shared with ILI9341), CS=4, polled mode (INT=255)
- SD card: SPI3 (SCK=40, MISO=41, MOSI=42, CS=39)
- Storage: SD card preferred (`/sd/hd0.img`); falls back to FFat (`/ffat/hd0_12m_games.img`)

## SPI Bus Assignments
| Bus | SCK | MISO | MOSI | Devices |
|---|---|---|---|---|
| SPI2 | 12 | 13 | 11 | ILI9341 (CS=10, DC=7) + XPT2046 (CS=4) |
| SPI3 | 40 | 41 | 42 | SD card (CS=39) |

Both `spi2` and `spi3` are `SPIClass` globals in the sketch. `touch.h` uses `TOUCH_XPT2046_SPI_CLASS` (defined as `spi2`) so it shares the SPI2 bus with the TFT.

`TOUCH_XPT2046_INT = 255` (polled mode). Touch events are translated to serial mouse in `loop()`.

## Input Method Selection
Set at the top of `esp32-faux86.ino`:
```cpp
#define INPUT_USB_HID_ESPUSBHOST  // EspUsbHost — native OTG, N-key rollover (default)
//#define INPUT_USB_HID_TINYUSB   // TinyUSB    — native OTG, N-key rollover (legacy)
//#define INPUT_CARDKB            // M5Stack CardKB — single key, no rollover
```

| Define | USB Mode | USB CDC On Boot | Serial monitor port |
|---|---|---|---|
| `INPUT_USB_HID_ESPUSBHOST` | Hardware CDC and JTAG (TBD — see note) | Disable | UART/COM (CH340/CP2102) |
| `INPUT_USB_HID_TINYUSB` | USB-OTG (TinyUSB) | Disable | UART/COM (CH340/CP2102) |
| `INPUT_CARDKB` | either | Enable | USB CDC or UART |

**Note on USB Mode for EspUsbHost:** not yet confirmed. EspUsbHost uses `usb_host_install()` (ESP-IDF OTG host driver) which may conflict with "Hardware CDC and JTAG" taking ownership of GPIO19/20. Still needs testing — try "Hardware CDC and JTAG" first, then "USB-OTG (TinyUSB)" if it doesn't work.

**Serial monitor:** with USB CDC On Boot = Disable, Serial goes to UART0. Use the UART COM port (CH340/CP2102 chip), not the native USB port. If the board has only one USB connector (native OTG only, no onboard UART chip), an external USB-UART adapter is needed.

**USB hub not supported:** The ESP-IDF USB host stack does not support USB hubs. The keyboard must be plugged **directly** into the ESP32-S3 OTG port — no hub in between.

## Uploading Disk Images
**SD card (preferred):** Copy disk image to the root of the SD card as `hd0.img`.

**FFat fallback:** Use the [arduino-esp32fs-plugin](https://github.com/lorol/arduino-esp32fs-plugin):
1. Place a `.img` file in `examples/esp32-faux86/data/` named `hd0_12m_games.img`
2. IDE → Tools → ESP32 Sketch Data Upload → FatFS

The sketch tries `/sd/hd0.img` first; if not found, falls back to `/ffat/hd0_12m_games.img`.

## Build Notes
- Arduino IDE 1.8.x does **not** cache compiled objects — every build is a full rebuild (slow)
- Upgrading to **Arduino IDE 2.x** gives proper incremental compilation
- `Config.h` (`DEBUG_CONFIG`, `DEBUG_VM` defined) adds logging overhead; comment these out once things are working to speed up both compile and runtime

## Input Limitations
- **CardKB reports only one key at a time** — hardware limitation, single byte per I2C poll. No multi-key rollover.
- Shift+key combos work because the CardKB firmware handles them internally and returns a shifted ASCII code (our code injects a synthetic Shift event around the key).
- Simultaneous keys (e.g. arrow + letter) are not possible. This affects games like Prince of Persia where Shift+arrow is needed for careful stepping and jumping.
- Faux86-remake has **no joystick/gameport emulation** (port 0x201 not implemented).

## Arduino ESP32 SDK Notes
- Arduino ESP32 **3.3.7** bundles **ESP-IDF 5.5.2** (release/v5.5)
- ESP32-S3 SDK ships with **NimBLE only** — `CONFIG_BLUEDROID_ENABLED` is not set
- Bluedroid headers (`esp_bt_main.h`, `esp_gap_bt_api.h`, `esp_gap_ble_api.h`) are absent from the S3 SDK
- ESP32-S3 has **no Classic Bluetooth hardware** — BLE only

## Alternative Keyboard Options (Research Notes)
### EspUsbHost (tanakamasayuki/EspUsbHost) — integrated, pending hardware test
- Uses ESP-IDF USB host driver (`usb_host_install()`) directly, bypassing TinyUSB
- Implemented as `INPUT_USB_HID_ESPUSBHOST` in `usbhid_espusbhost.h`
- **USB hub not supported** — ESP-IDF USB host stack has no hub support; keyboard must connect directly
- Physical direct-connection currently blocked by adapter size — pending solution (right-angle adapter, BLE, etc.)
- Debug prints added to `onKeyboard()` and `onGone()` — leave in until confirmed working

### bt-keyboard (turgu1/bt-keyboard) — parked
- Targets **regular ESP32 only** (dual-mode Classic BT + BLE hardware); not designed for ESP32-S3
- Uses Bluedroid stack (`CONFIG_BTDM_CTRL_MODE_BTDM=y`, `CONFIG_BT_CLASSIC_ENABLED=y`) — unavailable on S3
- ESP32-S3 has BLE-only hardware; no Classic BT radio at all
- IDF version (4.4+) is NOT the blocker
- To get BLE HID keyboards on S3: rewrite bt layer for NimBLE HID host, or use a different library

## PlatformIO Build (working)
Files: `examples/esp32-faux86/platformio.ini` + `partitions_16MB_fatfs.csv`
Sketch and headers live in `examples/esp32-faux86/src/` (PlatformIO convention).

### Planned: define-flag switching (not yet implemented)
Goal: switch between parallel ILI9341 + CardKB and SPI ILI9341 + EspUsbHost by flipping two `#define` lines at the top of the sketch — same pattern as the existing `INPUT_*` defines. Single PlatformIO environment, no env switching needed.

**Approach:**
1. **`esp32-faux86.ino`** — add a `DISPLAY_*` define block at the top alongside `INPUT_*`:
   ```cpp
   #define DISPLAY_PARALLEL  // parallel ILI9341 via Arduino_GFX
   //#define DISPLAY_SPI     // SPI ILI9341 via Adafruit_ILI9341
   ```
   Replace the hardcoded display/SD pin block with `#ifdef DISPLAY_PARALLEL` / `#ifdef DISPLAY_SPI` guards. Guard SD wiring and `setup()` init (spi2.begin, gfx->begin speed arg) accordingly.

2. **`src/ArduinoInterface.h`** — replace the hardcoded `#define DISABLE_ARDUINO_TFT` with:
   ```cpp
   #ifdef DISPLAY_SPI
   #define DISABLE_ARDUINO_TFT
   #endif
   ```
   This lets the parallel path use `Arduino_TFT *gfx` and the SPI path use `Adafruit_SPITFT *gfx` without touching the header to switch.

3. **`platformio.ini`** — keep single env; list all libs together (`Adafruit_ILI9341`, `Arduino_GFX`, `EspUsbHost`). USB mode still needs manual update to match `INPUT_*` (see table below) — or accept that CardKB users set USB mode once and leave it.

**Key detail — `DISABLE_ARDUINO_TFT`:** this define exists because `Arduino_DataBus.h` in Arduino_GFX causes a compile error on arduino-esp32 3.x (`LIST_HEAD` / `i80_device_list`). Driving it from `DISPLAY_SPI` means the error is suppressed for SPI builds while the parallel path keeps Arduino_TFT enabled. Verify the installed Arduino_GFX version has fixed this before implementing — if not, the parallel build will still fail.

### Key lessons learned during migration
- **Use pioarduino platform, not official espressif32.** The official `espressif32` platform (even 6.13.0) ships arduino-esp32 2.x (IDF 4.4.7). This causes silent early boot crashes on hardware using OPI PSRAM + IDF 5.x APIs. Use `platform = https://github.com/pioarduino/platform-espressif32/releases/download/stable/platform-espressif32.zip` which tracks arduino-esp32 3.x (IDF 5.x), matching Arduino IDE 3.3.7.
- **USB mode: use `build_unflags` + `build_flags`.** The board JSON defines `ARDUINO_USB_MODE=1` (Hardware CDC). Override with `build_unflags = -DARDUINO_USB_MODE=1` then `build_flags = -DARDUINO_USB_MODE=0`. Using `-U/-D` inline or `board_build.arduino.usb_mode` both fail (redefinition warnings or silently ignored).
- **Adafruit_Faux86 library: use `symlink://../..` in lib_deps.** The library is the repo itself; `symlink://` references it in-place so PlatformIO compiles its `src/` correctly.
- **faux86-remake headers: explicit `-I` needed.** PlatformIO doesn't propagate include paths between libraries. Add `-I${PROJECT_LIBDEPS_DIR}/${PIOENV}/faux86-remake/src` to `build_flags` so Adafruit_Faux86's sources can find `DriveManager.h` etc.
- **`StdioDiskInterface.h` include fix.** Changed `#include "../src/DriveManager.h"` → `#include "DriveManager.h"` (the `../src/` relative path was an Arduino IDE artifact that broke under PlatformIO).
- **Adafruit BusIO must be listed explicitly.** PlatformIO doesn't resolve transitive dependencies automatically; `Adafruit_GFX` pulls in `Adafruit_I2CDevice.h` from BusIO.
- **PSRAM verification.** PSRAM shows as `.ext_ram.bss` sections in the link map. If absent, check `IDF_VER` in verbose build output — IDF 4.4.x means wrong platform/framework.

### Data upload
Replace arduino-esp32fs-plugin with:
```bash
pio run --target uploadfs   # uploads data/ to FFat
```

### USB mode per input method
Change these two values in `build_unflags` / `build_flags` to match `INPUT_*`:
| Input method | `ARDUINO_USB_MODE` | `ARDUINO_USB_CDC_ON_BOOT` |
|---|---|---|
| `INPUT_USB_HID_ESPUSBHOST` (default) | 0 | 0 |
| `INPUT_USB_HID_TINYUSB` | 0 | 0 |
| `INPUT_CARDKB` | 1 | 1 |

## Upstream Dependencies
- [Faux86-remake](https://github.com/moononournation/Faux86-remake) — the actual 8086 emulator core (not in Library Manager; install from git)
- Adafruit GFX Library
- Adafruit ILI9341
- GFX Library for Arduino (Arduino_GFX)
- TouchLib / XPT2046_Touchscreen (not in Library Manager; install from git)
- SD (built into ESP32 Arduino core)
- [EspUsbHost](https://github.com/tanakamasayuki/EspUsbHost) — ESP-IDF USB host wrapper for INPUT_USB_HID_ESPUSBHOST (not in Library Manager; install from git)
