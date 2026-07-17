#pragma once

#include <stdint.h>

// Dutch QWERTY (nl_NL)
// Reference: Windows Dutch Keyboard Layout (KBDNE), via kbdlayout.info
// Key differences from en_US:
//   number row shift: 1! 2" 3# 4$ 5% 6& 7_ 8( 9) 0'
//   0x2D: /?  0x2E: \xb0(degree)/dead~  0x2F: dead\xa8/dead^
//   0x30: */|  0x32: NUHS </>  0x33: +/\xb1  0x34: dead\xb4/dead`
//   0x35: @/\xa7  0x36: ,;  0x37: .:  0x38: -/=
//   0x64: NUBS ]/[
// Dead keys map to 0. Columns: unshifted, Shift, AltGr, AltGr+Shift (Unicode
// code points), AltGr layer per Windows KBDNE (ReactOS kbdne.c); AltGr+E = €.
static const uint16_t KEYCODE_TO_UNICODE_NL_NL[128][4] = {
    {0, 0},           // 0x00
    {0, 0},           // 0x01
    {0, 0},           // 0x02
    {0, 0},           // 0x03
    {'a', 'A'},       // 0x04
    {'b', 'B'},       // 0x05
    {'c', 'C', '\xa2'}, // 0x06 c C  AltGr:¢
    {'d', 'D'},       // 0x07
    {'e', 'E', 0x20ac}, // 0x08 e E  AltGr:€
    {'f', 'F'},       // 0x09
    {'g', 'G'},       // 0x0a
    {'h', 'H'},       // 0x0b
    {'i', 'I'},       // 0x0c
    {'j', 'J'},       // 0x0d
    {'k', 'K'},       // 0x0e
    {'l', 'L'},       // 0x0f
    {'m', 'M', '\xb5'}, // 0x10 m M  AltGr:µ
    {'n', 'N'},       // 0x11
    {'o', 'O'},       // 0x12
    {'p', 'P'},       // 0x13
    {'q', 'Q'},       // 0x14
    {'r', 'R', '\xb6'}, // 0x15 r R  AltGr:¶
    {'s', 'S', '\xdf'}, // 0x16 s S  AltGr:ß
    {'t', 'T'},       // 0x17
    {'u', 'U'},       // 0x18
    {'v', 'V'},       // 0x19
    {'w', 'W'},       // 0x1a
    {'x', 'X', '\xbb'}, // 0x1b x X  AltGr:»
    {'y', 'Y'},       // 0x1c
    {'z', 'Z', '\xab'}, // 0x1d z Z  AltGr:«
    {'1', '!', '\xb9'}, // 0x1e 1 !  AltGr:¹
    {'2', '"', '\xb2'}, // 0x1f 2 "  AltGr:²
    {'3', '#', '\xb3'}, // 0x20 3 #  AltGr:³
    {'4', '$', '\xbc'}, // 0x21 4 $  AltGr:¼
    {'5', '%', '\xbd'}, // 0x22 5 %  AltGr:½
    {'6', '&', '\xbe'}, // 0x23 6 &  AltGr:¾
    {'7', '_', '\xa3'}, // 0x24 7 _  AltGr:£
    {'8', '(', '{'},  // 0x25 8 (  AltGr:{
    {'9', ')', '}'},  // 0x26 9 )  AltGr:}
    {'0', '\''},      // 0x27
    {'\r', '\r'},     // 0x28
    {'\x1b', '\x1b'}, // 0x29
    {'\b', '\b'},     // 0x2a
    {'\t', '\t'},     // 0x2b
    {' ', ' '},       // 0x2c
    {'/', '?'},       // 0x2d
    {'\xb0', 0},      // 0x2e
    {0, 0},           // 0x2f
    {'*', '|'},       // 0x30
    {0, 0},           // 0x31
    {'<', '>'},       // 0x32
    {'+', '\xb1'},    // 0x33
    {0, 0},           // 0x34
    {'@', '\xa7'},    // 0x35
    {',', ';'},       // 0x36
    {'.', ':'},       // 0x37
    {'-', '='},       // 0x38
    {0, 0},           // 0x39
    {0, 0},           // 0x3a
    {0, 0},           // 0x3b
    {0, 0},           // 0x3c
    {0, 0},           // 0x3d
    {0, 0},           // 0x3e
    {0, 0},           // 0x3f
    {0, 0},           // 0x40
    {0, 0},           // 0x41
    {0, 0},           // 0x42
    {0, 0},           // 0x43
    {0, 0},           // 0x44
    {0, 0},           // 0x45
    {0, 0},           // 0x46
    {0, 0},           // 0x47
    {0, 0},           // 0x48
    {0, 0},           // 0x49
    {0, 0},           // 0x4a
    {0, 0},           // 0x4b
    {0, 0},           // 0x4c
    {0, 0},           // 0x4d
    {0, 0},           // 0x4e
    {0, 0},           // 0x4f
    {0, 0},           // 0x50
    {0, 0},           // 0x51
    {0, 0},           // 0x52
    {0, 0},           // 0x53
    {0, 0},           // 0x54
    {0, 0},           // 0x55
    {0, 0},           // 0x56
    {0, 0},           // 0x57
    {0, 0},           // 0x58
    {0, 0},           // 0x59
    {0, 0},           // 0x5a
    {0, 0},           // 0x5b
    {0, 0},           // 0x5c
    {0, 0},           // 0x5d
    {0, 0},           // 0x5e
    {0, 0},           // 0x5f
    {0, 0},           // 0x60
    {0, 0},           // 0x61
    {0, 0},           // 0x62
    {0, 0},           // 0x63
    {']', '['},       // 0x64
};
