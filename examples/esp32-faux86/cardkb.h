/*******************************************************************************
 * M5Stack CardKB v1.1 I2C keyboard input
 *
 * Polls the CardKB at 0x5F and translates ASCII / special key codes to
 * XT scan codes expected by Faux86.
 *
 * CardKB returns 0x00 when no key is held, otherwise the key code.
 * Key-down is fired on transition from 0x00 to a code; key-up on the
 * reverse transition.
 *
 * Special key codes (v1.1 firmware, Fn combos):
 *   Arrow Left  0xB4    Arrow Right 0xB5
 *   Arrow Up    0xB6    Arrow Down  0xB7
 ******************************************************************************/
#pragma once
#include <Wire.h>

#define CARDKB_ADDR 0x5F

struct AsciiToXT {
  uint16_t xt_code;
  bool needs_shift;
};

// Maps ASCII 0x00-0x7F → XT scan code + whether Left Shift must be held.
// xt_code == 0 means the key is unmapped / ignored.
static const AsciiToXT ascii2xt[128] = {
    {0x0000, false}, // 0x00 (null)
    {0x0000, false}, // 0x01
    {0x0000, false}, // 0x02
    {0x0000, false}, // 0x03
    {0x0000, false}, // 0x04
    {0x0000, false}, // 0x05
    {0x0000, false}, // 0x06
    {0x0000, false}, // 0x07
    {0x000E, false}, // 0x08 Backspace
    {0x000F, false}, // 0x09 Tab
    {0x001C, false}, // 0x0A LF → Enter
    {0x0000, false}, // 0x0B
    {0x0000, false}, // 0x0C
    {0x001C, false}, // 0x0D CR / Enter
    {0x0000, false}, // 0x0E
    {0x0000, false}, // 0x0F
    {0x0000, false}, // 0x10
    {0x0000, false}, // 0x11
    {0x0000, false}, // 0x12
    {0x0000, false}, // 0x13
    {0x0000, false}, // 0x14
    {0x0000, false}, // 0x15
    {0x0000, false}, // 0x16
    {0x0000, false}, // 0x17
    {0x0000, false}, // 0x18
    {0x0000, false}, // 0x19
    {0x0000, false}, // 0x1A
    {0x0001, false}, // 0x1B Escape
    {0x0000, false}, // 0x1C
    {0x0000, false}, // 0x1D
    {0x0000, false}, // 0x1E
    {0x0000, false}, // 0x1F
    {0x0039, false}, // 0x20 Space
    {0x0002, true},  // 0x21 !  Shift+1
    {0x0028, true},  // 0x22 "  Shift+'
    {0x0004, true},  // 0x23 #  Shift+3
    {0x0005, true},  // 0x24 $  Shift+4
    {0x0006, true},  // 0x25 %  Shift+5
    {0x0008, true},  // 0x26 &  Shift+7
    {0x0028, false}, // 0x27 '
    {0x000A, true},  // 0x28 (  Shift+9
    {0x000B, true},  // 0x29 )  Shift+0
    {0x0009, true},  // 0x2A *  Shift+8
    {0x000D, true},  // 0x2B +  Shift+=
    {0x0033, false}, // 0x2C ,
    {0x000C, false}, // 0x2D -
    {0x0034, false}, // 0x2E .
    {0x0035, false}, // 0x2F /
    {0x000B, false}, // 0x30 0
    {0x0002, false}, // 0x31 1
    {0x0003, false}, // 0x32 2
    {0x0004, false}, // 0x33 3
    {0x0005, false}, // 0x34 4
    {0x0006, false}, // 0x35 5
    {0x0007, false}, // 0x36 6
    {0x0008, false}, // 0x37 7
    {0x0009, false}, // 0x38 8
    {0x000A, false}, // 0x39 9
    {0x0027, true},  // 0x3A :  Shift+;
    {0x0027, false}, // 0x3B ;
    {0x0033, true},  // 0x3C <  Shift+,
    {0x000D, false}, // 0x3D =
    {0x0034, true},  // 0x3E >  Shift+.
    {0x0035, true},  // 0x3F ?  Shift+/
    {0x0003, true},  // 0x40 @  Shift+2
    {0x001E, true},  // 0x41 A
    {0x0030, true},  // 0x42 B
    {0x002E, true},  // 0x43 C
    {0x0020, true},  // 0x44 D
    {0x0012, true},  // 0x45 E
    {0x0021, true},  // 0x46 F
    {0x0022, true},  // 0x47 G
    {0x0023, true},  // 0x48 H
    {0x0017, true},  // 0x49 I
    {0x0024, true},  // 0x4A J
    {0x0025, true},  // 0x4B K
    {0x0026, true},  // 0x4C L
    {0x0032, true},  // 0x4D M
    {0x0031, true},  // 0x4E N
    {0x0018, true},  // 0x4F O
    {0x0019, true},  // 0x50 P
    {0x0010, true},  // 0x51 Q
    {0x0013, true},  // 0x52 R
    {0x001F, true},  // 0x53 S
    {0x0014, true},  // 0x54 T
    {0x0016, true},  // 0x55 U
    {0x002F, true},  // 0x56 V
    {0x0011, true},  // 0x57 W
    {0x002D, true},  // 0x58 X
    {0x0015, true},  // 0x59 Y
    {0x002C, true},  // 0x5A Z
    {0x001A, false}, // 0x5B [
    {0x002B, false}, // 0x5C backslash
    {0x001B, false}, // 0x5D ]
    {0x0007, true},  // 0x5E ^  Shift+6
    {0x000C, true},  // 0x5F _  Shift+-
    {0x0029, false}, // 0x60 `
    {0x001E, false}, // 0x61 a
    {0x0030, false}, // 0x62 b
    {0x002E, false}, // 0x63 c
    {0x0020, false}, // 0x64 d
    {0x0012, false}, // 0x65 e
    {0x0021, false}, // 0x66 f
    {0x0022, false}, // 0x67 g
    {0x0023, false}, // 0x68 h
    {0x0017, false}, // 0x69 i
    {0x0024, false}, // 0x6A j
    {0x0025, false}, // 0x6B k
    {0x0026, false}, // 0x6C l
    {0x0032, false}, // 0x6D m
    {0x0031, false}, // 0x6E n
    {0x0018, false}, // 0x6F o
    {0x0019, false}, // 0x70 p
    {0x0010, false}, // 0x71 q
    {0x0013, false}, // 0x72 r
    {0x001F, false}, // 0x73 s
    {0x0014, false}, // 0x74 t
    {0x0016, false}, // 0x75 u
    {0x002F, false}, // 0x76 v
    {0x0011, false}, // 0x77 w
    {0x002D, false}, // 0x78 x
    {0x0015, false}, // 0x79 y
    {0x002C, false}, // 0x7A z
    {0x001A, true},  // 0x7B {  Shift+[
    {0x002B, true},  // 0x7C |  Shift+backslash
    {0x001B, true},  // 0x7D }  Shift+]
    {0x0029, true},  // 0x7E ~  Shift+`
    {0xe053, false}, // 0x7F Delete
};

static uint16_t cardkb_last_xt = 0;
static bool cardkb_last_shift = false;

static void cardkb_key_down(uint16_t xt, bool shift) {
  if (shift) {
    vm86->input.handleKeyDown(0x2A); // Left Shift down
    vm86->input.tick();
  }
  vm86->input.handleKeyDown(xt);
  vm86->input.tick();
}

static void cardkb_key_up(uint16_t xt, bool shift) {
  vm86->input.handleKeyUp(xt);
  vm86->input.tick();
  if (shift) {
    vm86->input.handleKeyUp(0x2A); // Left Shift up
    vm86->input.tick();
  }
}

void cardkb_poll() {
  Wire.requestFrom(CARDKB_ADDR, 1);
  if (!Wire.available())
    return;
  uint8_t key = Wire.read();

  if (key == 0x00) {
    // Key released
    if (cardkb_last_xt != 0) {
      cardkb_key_up(cardkb_last_xt, cardkb_last_shift);
      cardkb_last_xt = 0;
      cardkb_last_shift = false;
    }
    return;
  }

  uint16_t xt = 0;
  bool shift = false;

  // CardKB v1.1 special keys (Fn combos).
  // Note: 0xB6/0xB7 are swapped vs. the datasheet — verified empirically.
  switch (key) {
  case 0xB4: xt = 0xe04b; break; // Arrow Left
  case 0xB5: xt = 0xe048; break; // Arrow Up    (reversed from datasheet)
  case 0xB6: xt = 0xe050; break; // Arrow Down  (reversed from datasheet)
  case 0xB7: xt = 0xe04d; break; // Arrow Right (reversed from datasheet)
  default:
    if (key < 128) {
      xt = ascii2xt[key].xt_code;
      shift = ascii2xt[key].needs_shift;
    }
    break;
  }

  if (xt == 0)
    return;

  // Only act on key change, not while the same key is held
  if (xt == cardkb_last_xt)
    return;

  // Release previous key if a different one arrives without a 0x00 gap
  if (cardkb_last_xt != 0)
    cardkb_key_up(cardkb_last_xt, cardkb_last_shift);

  cardkb_key_down(xt, shift);
  cardkb_last_xt = xt;
  cardkb_last_shift = shift;
}
