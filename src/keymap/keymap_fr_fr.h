#pragma once

#include <stdint.h>

// French AZERTY (fr_FR)
// Reference: QMK keymap_french.h
// AZERTY: letters are remapped from QWERTY physical positions.
// Physical key positions (HID keycodes) vs. characters produced:
//   0x04(A pos)=q/Q  0x10(M pos)=,/?  0x14(Q pos)=a/A
//   0x1A(W pos)=z/Z  0x1D(Z pos)=w/W  0x33(; pos)=m/M
// Number row: symbols unshifted, digits shifted (AZERTY style)
//   0x1E: &/1  0x1F: é/2  0x20: "/3  0x21: '/4  0x22: (/5
//   0x23: -/6  0x24: è/7  0x25: _/8  0x26: ç/9  0x27: à/0
//   0x2D: )/°  0x2E: =/+
static const uint8_t KEYCODE_TO_ASCII_FR_FR[128][2] = {
    {0, 0},
    {0, 0},
    {0, 0},
    {0, 0},     // 0x00-0x03
    {'q', 'Q'}, // 0x04  A pos → q
    {'b', 'B'},
    {'c', 'C'},
    {'d', 'D'},
    {'e', 'E'}, // 0x05-0x08
    {'f', 'F'},
    {'g', 'G'},
    {'h', 'H'},
    {'i', 'I'}, // 0x09-0x0C
    {'j', 'J'},
    {'k', 'K'},
    {'l', 'L'}, // 0x0D-0x0F
    {',', '?'}, // 0x10  M pos → ,/?
    {'n', 'N'},
    {'o', 'O'},
    {'p', 'P'}, // 0x11-0x13
    {'a', 'A'}, // 0x14  Q pos → a
    {'r', 'R'},
    {'s', 'S'},
    {'t', 'T'}, // 0x15-0x17
    {'u', 'U'},
    {'v', 'V'}, // 0x18-0x19
    {'z', 'Z'}, // 0x1A  W pos → z
    {'x', 'X'},
    {'y', 'Y'}, // 0x1B-0x1C
    {'w', 'W'}, // 0x1D  Z pos → w
    {'&', '1'},
    {'\xe9', '2'},
    {'"', '3'},
    {'\'', '4'}, // 0x1E-0x21
    {'(', '5'},
    {'-', '6'},
    {'\xe8', '7'},
    {'_', '8'}, // 0x22-0x25
    {'\xe7', '9'},
    {'\xe0', '0'}, // 0x26-0x27
    {'\r', '\r'},
    {'\x1b', '\x1b'},
    {'\b', '\b'},
    {'\t', '\t'},
    {' ', ' '},    // 0x28-0x2C
    {')', '\xb0'}, // 0x2D  )/°
    {'=', '+'},    // 0x2E  =/+
    {0, 0},        // 0x2F  dead ^ / dead ¨
    {'$', '\xa3'}, // 0x30  $/£
    {0, 0},        // 0x31  (not on ISO layout)
    {'*', '\xb5'}, // 0x32  NUHS: */µ
    {'m', 'M'},    // 0x33  ; pos → m
    {'\xf9', '%'}, // 0x34  ' pos → ù/%
    {'\xb2', 0},   // 0x35  ` pos → ²
    {';', '.'},    // 0x36  , pos → ;/.
    {':', '/'},    // 0x37  . pos → :/
    {'!', '\xa7'}, // 0x38  / pos → !/§
    {0, 0},
    {0, 0},
    {0, 0},
    {0, 0},
    {0, 0},
    {0, 0},
    {0, 0},
    {0, 0}, // 0x39-0x40
    {0, 0},
    {0, 0},
    {0, 0},
    {0, 0},
    {0, 0},
    {0, 0},
    {0, 0},
    {0, 0}, // 0x41-0x48
    {0, 0},
    {0, 0},
    {0, 0},
    {0, 0},
    {0, 0},
    {0, 0},
    {0, 0},
    {0, 0}, // 0x49-0x50
    {0, 0},
    {0, 0},
    {0, 0}, // 0x51-0x53
    {0, 0},
    {0, 0},
    {0, 0},
    {0, 0},
    {0, 0},
    {0, 0},
    {0, 0},
    {0, 0}, // 0x54-0x5B keypad
    {0, 0},
    {0, 0},
    {0, 0},
    {0, 0},
    {0, 0},
    {0, 0},
    {0, 0},
    {0, 0},     // 0x5C-0x63 keypad
    {'<', '>'}, // 0x64  NUBS </>
};
