/*******************************************************************************
 * EspUsbHost keyboard input for Faux86
 *
 * Subclasses EspUsbHost and overrides onKeyboard() to translate HID keyboard
 * reports into XT scan codes consumed by the Faux86 VM.
 *
 * Board settings (Arduino IDE):
 *   USB Mode:        Hardware CDC and JTAG
 *   USB CDC On Boot: Disable
 *
 * This file must be included after vm86 is declared (it accesses the global).
 * usb2xtMapping / modifier2xtMapping come from Adafruit_Faux86.h → Keymap.h.
 ******************************************************************************/
#pragma once
#include "EspUsbHost.h"

class Faux86UsbHost : public EspUsbHost {
public:
  // Called when a USB device is removed — useful to confirm mount/unmount cycle.
  void onGone(const usb_host_client_event_msg_t *eventMsg) override {
    Serial.println("USB: device gone");
  }

  void onKeyboard(hid_keyboard_report_t report,
                  hid_keyboard_report_t last_report) override {
    Serial.printf("USB kbd: mod=%02x keys=%02x %02x %02x %02x %02x %02x\n",
                  report.modifier,
                  report.keycode[0], report.keycode[1], report.keycode[2],
                  report.keycode[3], report.keycode[4], report.keycode[5]);
    if (!vm86) { Serial.println("USB kbd: vm86 not ready"); return; }

    // Modifier bits: 8 independent keys (LCtrl, LShift, LAlt, LWin, RCtrl, RShift, RAlt, RWin)
    for (uint8_t i = 0; i < 8; i++) {
      uint8_t const mask = 1 << i;
      if ((report.modifier & mask) != (last_report.modifier & mask)) {
        if (report.modifier & mask)
          vm86->input.handleKeyDown(modifier2xtMapping[i]);
        else
          vm86->input.handleKeyUp(modifier2xtMapping[i]);
        vm86->input.tick();
      }
    }

    // Keycodes: up to 6 simultaneous keys
    for (uint8_t i = 0; i < 6; i++) {
      uint8_t const new_key = report.keycode[i];
      if (new_key && !_findKey(last_report, new_key)) {
        vm86->input.handleKeyDown(usb2xtMapping[new_key]);
        vm86->input.tick();
      }
      uint8_t const old_key = last_report.keycode[i];
      if (old_key && !_findKey(report, old_key)) {
        vm86->input.handleKeyUp(usb2xtMapping[old_key]);
        vm86->input.tick();
      }
    }
  }

private:
  static bool _findKey(const hid_keyboard_report_t &r, uint8_t keycode) {
    for (uint8_t i = 0; i < 6; i++)
      if (r.keycode[i] == keycode) return true;
    return false;
  }
};
