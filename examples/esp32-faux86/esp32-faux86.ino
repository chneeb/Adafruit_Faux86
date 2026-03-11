/*******************************************************************************
 * Arduino ESP32 Faux86
 *
 * Original example can be found here:
 * https://github.com/moononournation/T-Deck/blob/main/esp32-faux86/esp32-faux86.ino
 *
 * Required libraries:
 * - Faux86-remake: https://github.com/moononournation/Faux86-remake.git
 *   Note: on Linux, you may need to make change as
 *https://github.com/ArnoldUK/Faux86-remake/pull/5
 * - Adafruit GFX: https://github.com/adafruit/Adafruit-GFX-Library and your
 *specific TFT library controller e.g. Adafruit_ILI9341
 * - Arduino_GFX: https://github.com/moononournation/Arduino_GFX.git
 * - For uploading files to FFat:
 *https://github.com/lorol/arduino-esp32fs-plugin
 *
 * Arduino IDE Settings
 * Board:            "ESP32S3 Dev Module"
 * USB CDC On Boot:  "Enable"
 * Partition Scheme: "16M Flash(2M APP/12.5MB FATFS)"
 *
 * uploaded one of img file in disks/ to ESP32-S3 using
 *[arduino-esp32fs-plugin](https://github.com/lorol/arduino-esp32fs-plugin)
 * - Install arduino-esp32fs-plugin
 * - Create a folder named "data" in the sketch folder
 * - Copy one of the img file in disks/ to "data" folder
 * - From IDE menu select: "Tools" -> "ESP32 Sketch Data Upload" then select
 *"FatFS" then click "OK"
 ******************************************************************************/

#include "SPI.h"

#include "Adafruit_Faux86.h"

#define TFT_RST 5
#define TFT_BL -1
#define TFT_ROTATION 1

#include <Arduino_GFX_Library.h>
Arduino_DataBus *bus = new Arduino_ESP32PAR8(7 /* DC */, 6 /* CS */, 1 /* WR */, 2 /* RD */,
                                              21, 46, 18, 17, 19, 20, 3, 14 /* D0-D7 */);
Arduino_TFT *gfx = new Arduino_ILI9341(bus, TFT_RST);

// CardKB I2C pins — adjust to match your wiring
#define CARDKB_SDA 8
#define CARDKB_SCL 9

#include <FFat.h>
#include <SD.h>
#include <WiFi.h>

#define SD_CS   10
#define SD_SCK  12
#define SD_MISO 13
#define SD_MOSI 11

Faux86::VM *vm86;
Faux86::ArduinoHostSystemInterface hostInterface(gfx);
Faux86::Config vmConfig(&hostInterface);

uint16_t *vga_framebuffer;

// cardkb.h uses vm86, so include after the declaration above
#include "cardkb.h"

void vm86_task(void *param) {
  (void)param;

  if (!FFat.begin(false)) {
    Serial.println("WARNING: FFat mount failed");
  }

  SPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
  bool sd_ok = SD.begin(SD_CS);
  if (!sd_ok) {
    Serial.println("WARNING: SD card mount failed");
  }

  /* CPU settings */
  vmConfig.singleThreaded = true; // only WIN32 support multithreading
  vmConfig.slowSystem = true; // no audio, so no need to reserve time for it
  vmConfig.cpuSpeed = 0; // no limit

  /* Video settings */
  vmConfig.frameDelay = 60; // 50ms — Core 1 handles display; Core 0 just downscales
  vmConfig.framebuffer.width = 720;
  vmConfig.framebuffer.height = 480;

  /* Audio settings */
  vmConfig.enableAudio = false;
  vmConfig.useDisneySoundSource = false;
  vmConfig.useSoundBlaster = false;
  vmConfig.useAdlib = false;
  vmConfig.usePCSpeaker = false; // no audio output, skip speaker emulation
  vmConfig.audio.sampleRate = 22050; // 32000 //44100 //48000;
  vmConfig.audio.latency = 200;

  /* set BIOS ROM */
  // vmConfig.biosFile = hostInterface.openFile("/ffat/pcxtbios.bin");
  vmConfig.biosFile = new Faux86::EmbeddedDisk(pcxtbios_bin, pcxtbios_bin_len);

  /* set Basic ROM */
  // vmConfig.romBasicFile = hostInterface.openFile("/ffat/rombasic.bin");
  vmConfig.romBasicFile =
      new Faux86::EmbeddedDisk(rombasic_bin, rombasic_bin_len);

  /* set Video ROM */
  // vmConfig.videoRomFile = hostInterface.openFile("/ffat/videorom.bin");
  vmConfig.videoRomFile =
      new Faux86::EmbeddedDisk(videorom_bin, videorom_bin_len);

  /* set ASCII FONT Data */
  // vmConfig.asciiFile = hostInterface.openFile("/ffat/asciivga.dat");
  vmConfig.asciiFile = new Faux86::EmbeddedDisk(asciivga_dat, asciivga_dat_len);

  /* floppy drive image */
  // vmConfig.diskDriveA = hostInterface.openFile("/ffat/fd0.img");

  // harddisk drive image: try SD card first, fall back to FFat
  // vmConfig.diskDriveC = hostInterface.openFile("/ffat/hd0_12m_win30.img");
  if (sd_ok && SD.exists("/hd0.img")) {
    Serial.println("Loading disk image from SD card");
    vmConfig.diskDriveC = hostInterface.openFile("/sd/hd0.img");
  } else {
    Serial.println("Loading disk image from FFat");
    vmConfig.diskDriveC = hostInterface.openFile("/ffat/hd0_12m_games.img");
  }

  /* set boot drive */
  vmConfig.setBootDrive("hd0");

  vga_framebuffer = (uint16_t *)calloc(
      VGA_FRAMEBUFFER_WIDTH * VGA_FRAMEBUFFER_HEIGHT, sizeof(uint16_t));
  if (!vga_framebuffer) {
    Serial.println("Failed to allocate vga_framebuffer");
  }

  vm86 = new Faux86::VM(vmConfig);
  if (vm86->init()) {
    hostInterface.init(vm86);
  }

  while (1) {
    vm86->simulate();
    // hostInterface.tick();

    // simulated() call yield() inside but since this is highest priority task
    // we should call vTaskDelay(1) to allow other task to run
    vTaskDelay(1);
  }
}

/*******************************************************************************
 * Please config the touch panel in touch.h
 ******************************************************************************/
#include "touch.h"

#define TRACK_SPEED 2
bool trackball_interrupted = false;
int16_t trackball_up_count = 1;
int16_t trackball_down_count = 1;
int16_t trackball_left_count = 1;
int16_t trackball_right_count = 1;
int16_t trackball_click_count = 0;
bool mouse_downed = false;

void IRAM_ATTR ISR_up() {
  trackball_interrupted = true;
  trackball_up_count <<= TRACK_SPEED;
}

void IRAM_ATTR ISR_down() {
  trackball_interrupted = true;
  trackball_down_count <<= TRACK_SPEED;
}

void IRAM_ATTR ISR_left() {
  trackball_interrupted = true;
  trackball_left_count <<= TRACK_SPEED;
}

void IRAM_ATTR ISR_right() {
  trackball_interrupted = true;
  trackball_right_count <<= TRACK_SPEED;
}

void IRAM_ATTR ISR_click() {
  trackball_interrupted = true;
  ++trackball_click_count;
}

//--------------------------------------------------------------------+
//
//--------------------------------------------------------------------+
void setup() {
  WiFi.mode(WIFI_OFF);

  Serial.begin(115200);
  // Serial.setDebugOutput(true);
  // while(!Serial) delay(10);
  Serial.println("esp32-faux86");

  Serial.println("Init display");
  gfx->begin();
  gfx->setRotation(TFT_ROTATION);
  gfx->fillScreen(0x000000); // black

#if defined(TFT_BL) && (TFT_BL != -1)
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);
#endif

#if 0
  Wire.begin(TDECK_I2C_SDA, TDECK_I2C_SCL, TDECK_I2C_FREQ);

  Serial.println("Init touchscreen");
  touch_init(gfx->width(), gfx->height(), gfx->getRotation());

  // Init trackball
  pinMode(TDECK_TRACKBALL_UP, INPUT_PULLUP);
  attachInterrupt(TDECK_TRACKBALL_UP, ISR_up, FALLING);
  pinMode(TDECK_TRACKBALL_DOWN, INPUT_PULLUP);
  attachInterrupt(TDECK_TRACKBALL_DOWN, ISR_down, FALLING);
  pinMode(TDECK_TRACKBALL_LEFT, INPUT_PULLUP);
  attachInterrupt(TDECK_TRACKBALL_LEFT, ISR_left, FALLING);
  pinMode(TDECK_TRACKBALL_RIGHT, INPUT_PULLUP);
  attachInterrupt(TDECK_TRACKBALL_RIGHT, ISR_right, FALLING);
  pinMode(TDECK_TRACKBALL_CLICK, INPUT_PULLUP);
  attachInterrupt(TDECK_TRACKBALL_CLICK, ISR_click, FALLING);
#endif

  Serial.println("Init CardKB");
  Wire.begin(CARDKB_SDA, CARDKB_SCL);

  Serial.println("Init touchscreen");
  touch_init(gfx->width(), gfx->height(), gfx->getRotation());

  // Start the render task on Core 1 (priority 5) before the VM task
  hostInterface.beginRender();

  // Create a thread with high priority to run VM 86
  // For S3" since we don't use wifi in this example. We can it on core0
  xTaskCreateUniversal(vm86_task, "vm86", 8192, NULL, 10, NULL, 0);
}

void loop() {
  if (vm86) {
    cardkb_poll();
  }

  /* touch → serial mouse */
  static int16_t prev_tx = -1, prev_ty = -1;
  static bool touch_was_down = false;
  if (vm86) {
    if (touch_has_signal() && touch_touched()) {
      if (!touch_was_down) {
        vm86->mouse.handleButtonDown(Faux86::SerialMouse::ButtonType::Left);
        touch_was_down = true;
        prev_tx = touch_last_x;
        prev_ty = touch_last_y;
      } else {
        int16_t dx = touch_last_x - prev_tx;
        int16_t dy = touch_last_y - prev_ty;
        if (dx || dy) {
          vm86->mouse.handleMove(dx, dy);
          prev_tx = touch_last_x;
          prev_ty = touch_last_y;
        }
      }
    } else if (touch_was_down) {
      vm86->mouse.handleButtonUp(Faux86::SerialMouse::ButtonType::Left);
      touch_was_down = false;
      prev_tx = -1;
      prev_ty = -1;
    }
  }

#if 0
  /* handle trackball input */
  if (trackball_interrupted) {
    if (trackball_click_count > 0) {
      Serial.println("vm86->mouse.handleButtonDown(Faux86::SerialMouse::ButtonType::Left);");
      vm86->mouse.handleButtonDown(Faux86::SerialMouse::ButtonType::Left);
      mouse_downed = true;
    }
    int16_t x_delta = trackball_right_count - trackball_left_count;
    int16_t y_delta = trackball_down_count - trackball_up_count;
    if ((x_delta != 0) || (y_delta != 0)) {
      Serial.printf("x_delta: %d, y_delta: %d\n", x_delta, y_delta);
      vm86->mouse.handleMove(x_delta, y_delta);
    }
    trackball_interrupted = false;
    trackball_up_count = 1;
    trackball_down_count = 1;
    trackball_left_count = 1;
    trackball_right_count = 1;
    trackball_click_count = 0;
  } else if (mouse_downed) {
    if (digitalRead(TDECK_TRACKBALL_CLICK) == HIGH) // released
    {
      Serial.println("vm86->mouse.handleButtonUp(Faux86::SerialMouse::ButtonType::Left);");
      vm86->mouse.handleButtonUp(Faux86::SerialMouse::ButtonType::Left);
      mouse_downed = false;
    }
  }
#endif
}
