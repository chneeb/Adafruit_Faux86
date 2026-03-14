/*******************************************************************************
 * TinyUSB HID keyboard callbacks for Faux86
 *
 * Included by esp32-faux86.ino inside #ifdef INPUT_USB_HID_TINYUSB.
 * Must be included after vm86 is declared (accesses the global).
 * usb2xtMapping / modifier2xtMapping come from Adafruit_Faux86.h → Keymap.h.
 ******************************************************************************/
#pragma once
#include "Adafruit_TinyUSB.h"

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
