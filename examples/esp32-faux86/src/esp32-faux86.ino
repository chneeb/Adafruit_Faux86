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
 * - Adafruit GFX: https://github.com/adafruit/Adafruit-GFX-Library
 * - Adafruit ILI9341: https://github.com/adafruit/Adafruit_ILI9341
 * - XPT2046_Touchscreen: https://github.com/PaulStoffregen/XPT2046_Touchscreen.git
 * - Adafruit TinyUSB (INPUT_USB_HID_TINYUSB only): https://github.com/adafruit/Adafruit_TinyUSB_Arduino
 * - EspUsbHost (INPUT_USB_HID_ESPUSBHOST only): https://github.com/tanakamasayuki/EspUsbHost
 * - For uploading files to FFat:
 *https://github.com/lorol/arduino-esp32fs-plugin
 *
 * Arduino IDE Settings
 * Board:            "ESP32S3 Dev Module"
 * Partition Scheme: "16M Flash(2M APP/12.5MB FATFS)"
 * PSRAM:            "OPI PSRAM"
 *
 * INPUT_USB_HID_ESPUSBHOST (default USB keyboard):
 *   USB Mode:        "Hardware CDC and JTAG"
 *   USB CDC On Boot: "Disable"
 *   Connect USB keyboard to the OTG/native USB port (GPIO19/20).
 *   Use the UART/COM port for Serial monitor.
 *
 * INPUT_USB_HID_TINYUSB (legacy USB keyboard):
 *   USB Mode:        "USB-OTG (TinyUSB)"
 *   USB CDC On Boot: "Disable"
 *   Connect USB keyboard to the OTG/native USB port (GPIO19/20).
 *   Use the UART/COM port for Serial monitor.
 *
 * INPUT_CARDKB:
 *   USB CDC On Boot: "Enable"
 ******************************************************************************/

// ── Input method ─────────────────────────────────────────────────────────────
// Uncomment ONE:
#define INPUT_USB_HID_ESPUSBHOST // EspUsbHost — native OTG, N-key rollover (USB Mode: Hardware CDC and JTAG)
//#define INPUT_USB_HID_TINYUSB  // TinyUSB    — native OTG, N-key rollover (USB Mode: USB-OTG TinyUSB)
//#define INPUT_CARDKB           // M5Stack CardKB v1.1 I2C (single key, no rollover)

// ── Display: SPI ILI9341 on SPI2 ─────────────────────────────────────────────
// SPI2 default pins on ESP32-S3: SCK=12, MISO=13, MOSI=11
// Adjust TFT_DC to match your wiring (any free GPIO, e.g. GPIO 7):
#define TFT_DC       7
#define TFT_CS       10
#define TFT_RST      6
#define TFT_BL       15
#define TFT_ROTATION 1
#define TFT_SPEED_HZ (60 * 1000 * 1000ul)

// ── SD card: SPI3 ─────────────────────────────────────────────────────────────
// SPI3 has no IO_MUX defaults on ESP32-S3; pins must be explicit.
// GPIO 26-37 are reserved for OPI PSRAM — use 39-42 instead.
#define SD_CS   39
#define SD_SCK  40
#define SD_MISO 41
#define SD_MOSI 42
 
#include "SPI.h"
#include "Adafruit_Faux86.h"
#include "Adafruit_ILI9341.h"
#include <FFat.h>
#include <SD.h>
#include <WiFi.h>

#ifdef INPUT_USB_HID_TINYUSB
#include "Adafruit_TinyUSB.h"
// Native ESP32-S3 USB OTG host via TinyUSB stack.
Adafruit_USBH_Host USBHost;
#endif

#ifdef INPUT_CARDKB
#include <Wire.h>
#define CARDKB_SDA 8
#define CARDKB_SCL 9
#endif

// Reference: https://curiousscientist.tech/blog/esp32-s3-multiple-spi-buses-spi-fspi-hspi
// SPI2 bus shared by ILI9341 and XPT2046 (different CS pins):
SPIClass spi2(FSPI);
// SPI3 bus for SD card:
SPIClass spi3(HSPI);

Adafruit_ILI9341 *gfx = new Adafruit_ILI9341(&spi2, TFT_DC, TFT_CS, TFT_RST);

// Tell touch.h to share our spi2 object instead of the default SPI:
#define TOUCH_XPT2046_SPI_CLASS spi2
#include "touch.h"

Faux86::VM *vm86;
Faux86::ArduinoHostSystemInterface hostInterface(gfx);
Faux86::Config vmConfig(&hostInterface);

uint16_t *vga_framebuffer;

#ifdef INPUT_CARDKB
// cardkb.h uses vm86, so include after vm86 is declared:
#include "cardkb.h"
#endif

#ifdef INPUT_USB_HID_ESPUSBHOST
// usbhid_espusbhost.h uses vm86, so include after vm86 is declared:
#include "usbhid_espusbhost.h"
Faux86UsbHost USBHost;
#endif

void vm86_task(void *param) {
  (void)param;
  Serial.println("vm86_task: started");

  Serial.println("vm86_task: mounting FFat");
  if (!FFat.begin(false)) {
    Serial.println("WARNING: FFat mount failed");
  } else {
    Serial.println("vm86_task: FFat OK");
  }

  Serial.println("vm86_task: init SPI3 / SD");
  spi3.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
  bool sd_ok = SD.begin(SD_CS, spi3);
  if (!sd_ok) {
    Serial.println("WARNING: SD card mount failed");
  } else {
    Serial.println("vm86_task: SD OK");
  }

  /* CPU settings */
  vmConfig.singleThreaded = true; // only WIN32 support multithreading
  vmConfig.slowSystem = true;
  vmConfig.cpuSpeed = 0; // no limit

  /* Video settings */
  vmConfig.frameDelay = 60; // ms; Core 1 handles display, Core 0 just downscales
  vmConfig.framebuffer.width = 720;
  vmConfig.framebuffer.height = 480;

  /* Audio settings */
  vmConfig.enableAudio = false;
  vmConfig.useDisneySoundSource = false;
  vmConfig.useSoundBlaster = false;
  vmConfig.useAdlib = false;
  vmConfig.usePCSpeaker = false;
  vmConfig.audio.sampleRate = 22050;
  vmConfig.audio.latency = 200;

  /* set BIOS ROM */
  vmConfig.biosFile = new Faux86::EmbeddedDisk(pcxtbios_bin, pcxtbios_bin_len);

  /* set Basic ROM */
  vmConfig.romBasicFile =
      new Faux86::EmbeddedDisk(rombasic_bin, rombasic_bin_len);

  /* set Video ROM */
  vmConfig.videoRomFile =
      new Faux86::EmbeddedDisk(videorom_bin, videorom_bin_len);

  /* set ASCII FONT Data */
  vmConfig.asciiFile = new Faux86::EmbeddedDisk(asciivga_dat, asciivga_dat_len);

  // harddisk drive image: try SD card first, fall back to FFat
  if (sd_ok && SD.exists("/hd0.img")) {
    Serial.println("Loading disk image from SD card");
    vmConfig.diskDriveC = hostInterface.openFile("/sd/hd0.img");
  } else {
    Serial.println("Loading disk image from FFat");
    vmConfig.diskDriveC = hostInterface.openFile("/ffat/hd0_12m_games.img");
  }

  /* set boot drive */
  vmConfig.setBootDrive("hd0");

  Serial.printf("vm86_task: free heap before alloc: %u\n", esp_get_free_heap_size());
  vga_framebuffer = (uint16_t *)calloc(
      VGA_FRAMEBUFFER_WIDTH * VGA_FRAMEBUFFER_HEIGHT, sizeof(uint16_t));
  if (!vga_framebuffer) {
    Serial.println("FATAL: Failed to allocate vga_framebuffer");
    vTaskDelete(NULL);
  }
  Serial.printf("vm86_task: vga_framebuffer OK (%u bytes), free heap: %u\n",
                VGA_FRAMEBUFFER_WIDTH * VGA_FRAMEBUFFER_HEIGHT * sizeof(uint16_t),
                esp_get_free_heap_size());

  Serial.println("vm86_task: creating VM");
  vm86 = new Faux86::VM(vmConfig);
  Serial.println("vm86_task: calling vm86->init()");
  if (vm86->init()) {
    Serial.println("vm86_task: VM init OK, calling hostInterface.init()");
    hostInterface.init(vm86);
    Serial.println("vm86_task: entering simulate loop");
  } else {
    Serial.println("FATAL: VM init failed");
    vTaskDelete(NULL);
  }

  while (1) {
    vm86->simulate();
    vTaskDelay(1);
  }
}

void setup() {
  WiFi.mode(WIFI_OFF);

  Serial.begin(115200);
  Serial.setDebugOutput(true);  // route ESP-IDF log/panic output to Serial
  Serial.println("esp32-faux86");

  // Initialise SPI2 bus once (default pins: SCK=12, MISO=13, MOSI=11);
  // ILI9341 and XPT2046 both share it.
  spi2.begin();

  Serial.println("Init display");
  gfx->begin(TFT_SPEED_HZ);
  gfx->setRotation(TFT_ROTATION);
  gfx->fillScreen(0x0000);

#if defined(TFT_BL) && (TFT_BL != -1)
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);
#endif

#ifdef INPUT_CARDKB
  Serial.println("Init CardKB");
  Wire.begin(CARDKB_SDA, CARDKB_SCL);
#endif

  Serial.println("Init touchscreen");
  touch_init(gfx->width(), gfx->height(), gfx->getRotation());
  Serial.println("Init touchscreen done");

  Serial.printf("Free heap before tasks: %u\n", esp_get_free_heap_size());

  // Start the render task on Core 1 before the VM task:
  Serial.println("Starting render task");
  hostInterface.beginRender();
  Serial.println("Render task started");

  // VM runs on Core 0, priority 10:
  Serial.println("Starting vm86 task");
  xTaskCreateUniversal(vm86_task, "vm86", 8192, NULL, 10, NULL, 0);
  Serial.println("setup() done");

#ifdef INPUT_USB_HID_TINYUSB
  Serial.println("Init USB host (TinyUSB)");
  if (!USBHost.begin(1)) {
    Serial.println("Failed to init USBHost");
  }
#endif

#ifdef INPUT_USB_HID_ESPUSBHOST
  Serial.println("Init USB host (EspUsbHost)");
  USBHost.begin();
#endif
}

void loop() {
#if defined(INPUT_USB_HID_TINYUSB) || defined(INPUT_USB_HID_ESPUSBHOST)
  USBHost.task();
#endif

#ifdef INPUT_CARDKB
  if (vm86) {
    cardkb_poll();
  }
#endif

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
}

#ifdef INPUT_USB_HID_TINYUSB
//--------------------------------------------------------------------+
// USB HID keyboard callbacks (TinyUSB host)
//--------------------------------------------------------------------+

static bool find_key_in_report(hid_keyboard_report_t const *report,
                               uint8_t keycode) {
  for (uint8_t i = 0; i < 6; i++) {
    if (report->keycode[i] == keycode)
      return true;
  }
  return false;
}

static void process_kbd_report(hid_keyboard_report_t const *report) {
  static hid_keyboard_report_t prev_report = {0, 0, {0}};

  // modifiers
  for (uint8_t i = 0; i < 8; i++) {
    uint8_t const mask = 1 << i;
    uint8_t const new_mod = report->modifier & mask;
    uint8_t const old_mod = prev_report.modifier & mask;
    if (new_mod != old_mod) {
      if (new_mod)
        vm86->input.handleKeyDown(modifier2xtMapping[i]);
      else
        vm86->input.handleKeyUp(modifier2xtMapping[i]);
      vm86->input.tick();
    }
  }

  // keycodes (up to 6 simultaneous)
  for (uint8_t i = 0; i < 6; i++) {
    uint8_t const new_key = report->keycode[i];
    if (new_key && !find_key_in_report(&prev_report, new_key)) {
      vm86->input.handleKeyDown(usb2xtMapping[new_key]);
      vm86->input.tick();
    }
    uint8_t const old_key = prev_report.keycode[i];
    if (old_key && !find_key_in_report(report, old_key)) {
      vm86->input.handleKeyUp(usb2xtMapping[old_key]);
      vm86->input.tick();
    }
  }

  prev_report = *report;
}

extern "C" {

void tuh_hid_mount_cb(uint8_t dev_addr, uint8_t instance,
                      uint8_t const *desc_report, uint16_t desc_len) {
  (void)desc_report;
  (void)desc_len;
  if (tuh_hid_interface_protocol(dev_addr, instance) == HID_ITF_PROTOCOL_KEYBOARD) {
    Serial.println("HID Keyboard mounted");
    if (!tuh_hid_receive_report(dev_addr, instance)) {
      Serial.println("Error: cannot request keyboard report");
    }
  }
}

void tuh_hid_umount_cb(uint8_t dev_addr, uint8_t instance) {
  Serial.printf("HID device %d instance %d unmounted\r\n", dev_addr, instance);
}

void tuh_hid_report_received_cb(uint8_t dev_addr, uint8_t instance,
                                uint8_t const *report, uint16_t len) {
  (void)len;
  if (tuh_hid_interface_protocol(dev_addr, instance) == HID_ITF_PROTOCOL_KEYBOARD) {
    process_kbd_report((hid_keyboard_report_t const *)report);
  }
  if (!tuh_hid_receive_report(dev_addr, instance)) {
    Serial.println("Error: cannot request report");
  }
}

} // extern "C"
#endif // INPUT_USB_HID_TINYUSB
