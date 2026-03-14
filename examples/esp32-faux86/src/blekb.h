/*******************************************************************************
 * BLE keyboard input for Faux86 — T-HMI-C64 Android app compatible
 *
 * Advertises as "THMIC64" with the same BLE service/characteristic UUIDs as
 * the T-HMI-C64 project (https://github.com/retroelec/T-HMI-C64), so the
 * same Android app can control Faux86 over BLE.
 *
 * Protocol (3 bytes written by the Android app to the BLE characteristic):
 *   Byte 0: DC00 — CIA1 Port A row-select (active-low; one bit cleared)
 *   Byte 1: DC01 — CIA1 Port B column data (active-low; one bit cleared)
 *   Byte 2: Modifier — bit 0 = Shift, bit 1 = Ctrl, bit 2 = Commodore,
 *            bit 7 = external command (ignored by us); 0xFF = key release
 *
 * C64 keyboard matrix:
 *   Row R is selected when DC00 bit R is 0 (DC00 = ~(1 << R)).
 *   Col C is active  when DC01 bit C is 0 (DC01 = ~(1 << C)).
 *
 *   Row 0: DEL,  RETURN, CRSR→, F7,    F1,  F3,  F5,  CRSR↓
 *   Row 1: 3,    W,      A,     4,     Z,   S,   E,   LSHIFT
 *   Row 2: 5,    R,      D,     6,     C,   F,   T,   X
 *   Row 3: 7,    Y,      G,     8,     B,   H,   U,   V
 *   Row 4: 9,    I,      J,     0,     M,   K,   O,   N
 *   Row 5: +,    P,      L,     -,     .,   :,   @,   ,
 *   Row 6: £,    *,      ;,     HOME,  R/S, =,   ↑,   /
 *   Row 7: 1,    ←,      CTRL,  2,     SPC, CBM, Q,   SHFTLCK
 *
 * Cursor direction reversal (C64 quirk):
 *   CRSR→ + modifier bit 0 (Shift) → Left arrow in Faux86
 *   CRSR↓ + modifier bit 0 (Shift) → Up arrow in Faux86
 *   When the Android app has 4 separate ←↑→↓ cursor buttons, pressing ← or ↑
 *   sends the cursor key with the shift bit set in byte 2 (without a separate
 *   LSHIFT matrix event). If the user also holds the on-screen LSHIFT key,
 *   Faux86 sees Shift+reversed-arrow (e.g., Shift+Left = careful movement in
 *   Prince of Persia).
 *
 * Modifier key handling:
 *   LSHIFT (row1,col7=0x2A), CTRL (row7,col2=0x1D), and CBM/Alt (row7,col5=0x38)
 *   are tracked independently so they can be held while other keys are pressed.
 *   All other keys are tracked as a single "last pressed key" (CardKB style).
 *
 * Board / USB settings for INPUT_BLE:
 *   USB Mode:        Hardware CDC and JTAG
 *   USB CDC On Boot: Enable  (serial monitor via USB or UART)
 *
 * This file must be included after vm86 is declared.
 ******************************************************************************/
#pragma once
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLECharacteristic.h>

#define BLE_DEVICE_NAME  "THMIC64"
#define BLE_SERVICE_UUID "695ba701-a48c-43f6-9028-3c885771f19f"
#define BLE_CHAR_UUID    "3b05e9bf-086f-4b56-9c37-7b7eeb30b28b"

// C64 keyboard matrix → XT scan codes: c64_to_xt[row][col]
// Row R: DC00 = ~(1<<R); Col C: DC01 = ~(1<<C).
// 0x0000 = unmapped key.
// Extended XT codes include the 0xE0 prefix (Faux86 only uses the low byte,
// but written in full to match the style used in cardkb.h).
//
// Key mapping notes:
//   CRSR→/CRSR↓  handled with direction reversal via modifier bit 0 (see below)
//   C64 +        → numpad + (0x4E)  — XT = key maps to 0x0D
//   C64 :        → ;: key (0x27)    — : needs Shift on PC keyboard
//   C64 @        → 2 key (0x03)     — @ needs Shift+2 on PC keyboard
//   C64 ↑ (^)   → backslash (0x2B)
//   C64 ← (←)  → backtick/~ (0x29)
//   C64 CBM      → Left Alt (0x38)
//   C64 RUN/STOP → Escape (0x01)
//   C64 HOME     → numpad 7 / Home (0x47)
//   C64 £        → unmapped (0x0000)
static const uint16_t c64_to_xt[8][8] = {
//          col0    col1    col2    col3    col4    col5    col6    col7
/* row0 */ {0x000E, 0x001C, 0xe04d, 0x0041, 0x003B, 0x003D, 0x003F, 0xe050},
/* row1 */ {0x0004, 0x0011, 0x001E, 0x0005, 0x002C, 0x001F, 0x0012, 0x002A},
/* row2 */ {0x0006, 0x0013, 0x0020, 0x0007, 0x002E, 0x0021, 0x0014, 0x002D},
/* row3 */ {0x0008, 0x0015, 0x0022, 0x0009, 0x0030, 0x0023, 0x0016, 0x002F},
/* row4 */ {0x000A, 0x0017, 0x0024, 0x000B, 0x0032, 0x0025, 0x0018, 0x0031},
/* row5 */ {0x004E, 0x0019, 0x0026, 0x000C, 0x0034, 0x0027, 0x0003, 0x0033},
/* row6 */ {0x0000, 0x0037, 0x0027, 0x0047, 0x0001, 0x000D, 0x002B, 0x0035},
/* row7 */ {0x0002, 0x0029, 0x001D, 0x0003, 0x0039, 0x0038, 0x0010, 0x003A},
};

// State: modifier keys tracked independently (can be held while other keys pressed)
static bool     blekb_shift_held   = false;
static bool     blekb_ctrl_held    = false;
static bool     blekb_alt_held     = false;  // CBM key → Left Alt
// State: last non-modifier key pressed (only one at a time)
static uint16_t blekb_last_regular = 0;

static void blekb_key_down(uint16_t xt) {
    vm86->input.handleKeyDown(xt);
    vm86->input.tick();
}

static void blekb_key_up(uint16_t xt) {
    vm86->input.handleKeyUp(xt);
    vm86->input.tick();
}

static void blekb_release_all() {
    if (blekb_last_regular) { blekb_key_up(blekb_last_regular); blekb_last_regular = 0; }
    if (blekb_shift_held)   { blekb_key_up(0x002A); blekb_shift_held = false; }
    if (blekb_ctrl_held)    { blekb_key_up(0x001D); blekb_ctrl_held  = false; }
    if (blekb_alt_held)     { blekb_key_up(0x0038); blekb_alt_held   = false; }
}

static void blekb_handle_data(const uint8_t *data, size_t len) {
    if (!vm86 || len < 3) return;

    uint8_t dc00 = data[0], dc01 = data[1], mod = data[2];

    if (mod & 0x80) return; // external command byte, ignore

    // Decode row: find the single cleared bit in DC00
    int row = -1;
    for (int i = 0; i < 8; i++) {
        if (!(dc00 & (1 << i))) { row = i; break; }
    }
    // Decode col: find the single cleared bit in DC01
    int col = -1;
    for (int i = 0; i < 8; i++) {
        if (!(dc01 & (1 << i))) { col = i; break; }
    }
    if (row < 0 || col < 0) {
        // No valid key position: treat as full release
        blekb_release_all();
        return;
    }

    // Look up the XT code for this matrix position (pre-direction-adjustment)
    uint16_t base_xt = c64_to_xt[row][col];

    if (mod == 0xFF) {
        // Key release
        if (base_xt == 0x002A) {
            if (blekb_shift_held) { blekb_key_up(0x002A); blekb_shift_held = false; }
        } else if (base_xt == 0x001D) {
            if (blekb_ctrl_held)  { blekb_key_up(0x001D); blekb_ctrl_held  = false; }
        } else if (base_xt == 0x0038) {
            if (blekb_alt_held)   { blekb_key_up(0x0038); blekb_alt_held   = false; }
        } else {
            // Release the last regular key (cursor direction already resolved in last_regular)
            if (blekb_last_regular) { blekb_key_up(blekb_last_regular); blekb_last_regular = 0; }
        }
        return;
    }

    // Key press: resolve actual XT code, including cursor direction reversal
    uint16_t xt;
    bool shift_in_mod = (mod & 0x01) != 0;

    if (row == 0 && col == 2) {
        // CRSR→: right normally; when shift bit set in modifier, reverse to left
        xt = shift_in_mod ? 0xe04b : 0xe04d;
    } else if (row == 0 && col == 7) {
        // CRSR↓: down normally; when shift bit set in modifier, reverse to up
        xt = shift_in_mod ? 0xe048 : 0xe050;
    } else {
        xt = base_xt;
    }

    if (!xt) return; // unmapped key

    // Handle modifier keys independently
    if (xt == 0x002A) { // LSHIFT
        if (!blekb_shift_held) { blekb_key_down(0x002A); blekb_shift_held = true; }
        return;
    }
    if (xt == 0x001D) { // CTRL
        if (!blekb_ctrl_held)  { blekb_key_down(0x001D); blekb_ctrl_held  = true; }
        return;
    }
    if (xt == 0x0038) { // CBM → Left Alt
        if (!blekb_alt_held)   { blekb_key_down(0x0038); blekb_alt_held   = true; }
        return;
    }

    // Regular key: release previous if different
    if (xt == blekb_last_regular) return; // same key still held
    if (blekb_last_regular) { blekb_key_up(blekb_last_regular); blekb_last_regular = 0; }
    blekb_key_down(xt);
    blekb_last_regular = xt;
}

class BleKbCharCallbacks : public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic *pChar) override {
        String val = pChar->getValue();
        blekb_handle_data((const uint8_t *)val.c_str(), val.length());
    }
};

static BLEServer *blekb_server = nullptr;

class BleKbServerCallbacks : public BLEServerCallbacks {
    void onConnect(BLEServer *) override {
        Serial.println("BLE KB: client connected");
    }
    void onDisconnect(BLEServer *pSrv) override {
        Serial.println("BLE KB: client disconnected, restarting advertising");
        blekb_release_all();
        pSrv->startAdvertising();
    }
};

void blekb_init() {
    BLEDevice::init(BLE_DEVICE_NAME);
    blekb_server = BLEDevice::createServer();
    blekb_server->setCallbacks(new BleKbServerCallbacks());

    BLEService *pSvc = blekb_server->createService(BLE_SERVICE_UUID);
    BLECharacteristic *pChar = pSvc->createCharacteristic(
        BLE_CHAR_UUID,
        BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_NOTIFY
    );
    pChar->setCallbacks(new BleKbCharCallbacks());
    pSvc->start();

    BLEAdvertising *pAdv = BLEDevice::getAdvertising();
    pAdv->addServiceUUID(BLE_SERVICE_UUID);
    pAdv->setScanResponse(true);
    pAdv->setMinPreferred(0x12); // 22.5 ms min connection interval
    pAdv->setMaxPreferred(0x60); // 120 ms max connection interval
    BLEDevice::startAdvertising();
    Serial.println("BLE KB: advertising as " BLE_DEVICE_NAME);
}
