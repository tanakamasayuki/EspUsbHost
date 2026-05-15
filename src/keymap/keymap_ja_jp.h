#pragma once

#include <stdint.h>

static const uint8_t KEYCODE_TO_ASCII_JA_JP[128][2] = {
    {0, 0},           // 0x00 (no event)
    {0, 0},           // 0x01 (error rollover)
    {0, 0},           // 0x02 (POST fail)
    {0, 0},           // 0x03 (error undefined)
    {'a', 'A'},       // 0x04 a A
    {'b', 'B'},       // 0x05 b B
    {'c', 'C'},       // 0x06 c C
    {'d', 'D'},       // 0x07 d D
    {'e', 'E'},       // 0x08 e E
    {'f', 'F'},       // 0x09 f F
    {'g', 'G'},       // 0x0a g G
    {'h', 'H'},       // 0x0b h H
    {'i', 'I'},       // 0x0c i I
    {'j', 'J'},       // 0x0d j J
    {'k', 'K'},       // 0x0e k K
    {'l', 'L'},       // 0x0f l L
    {'m', 'M'},       // 0x10 m M
    {'n', 'N'},       // 0x11 n N
    {'o', 'O'},       // 0x12 o O
    {'p', 'P'},       // 0x13 p P
    {'q', 'Q'},       // 0x14 q Q
    {'r', 'R'},       // 0x15 r R
    {'s', 'S'},       // 0x16 s S
    {'t', 'T'},       // 0x17 t T
    {'u', 'U'},       // 0x18 u U
    {'v', 'V'},       // 0x19 v V
    {'w', 'W'},       // 0x1a w W
    {'x', 'X'},       // 0x1b x X
    {'y', 'Y'},       // 0x1c y Y
    {'z', 'Z'},       // 0x1d z Z
    {'1', '!'},       // 0x1e 1 !
    {'2', '"'},       // 0x1f 2 "
    {'3', '#'},       // 0x20 3 #
    {'4', '$'},       // 0x21 4 $
    {'5', '%'},       // 0x22 5 %
    {'6', '&'},       // 0x23 6 &
    {'7', '\''},      // 0x24 7 '
    {'8', '('},       // 0x25 8 (
    {'9', ')'},       // 0x26 9 )
    {'0', 0},         // 0x27 0
    {'\r', '\r'},     // 0x28 Enter
    {'\x1b', '\x1b'}, // 0x29 Escape
    {'\b', '\b'},     // 0x2a Backspace
    {'\t', '\t'},     // 0x2b Tab
    {' ', ' '},       // 0x2c Space
    {'-', '='},       // 0x2d - =
    {'^', '~'},       // 0x2e ^ ~
    {'@', '`'},       // 0x2f @ `
    {'[', '{'},       // 0x30 [ {
    {']', '}'},       // 0x31 ] }  (Non-US #/~, JIS home row: ] })
    {']', '}'},       // 0x32 ] }  (Non-US \/|, some JIS keyboards)
    {';', '+'},       // 0x33 ; +
    {':', '*'},       // 0x34 : *
    {0, 0},           // 0x35 半/全 (Hankaku/Zenkaku)
    {',', '<'},       // 0x36 , <
    {'.', '>'},       // 0x37 . >
    {'/', '?'},       // 0x38 / ?
    {0, 0},           // 0x39 Caps Lock
    {0, 0},           // 0x3a F1
    {0, 0},           // 0x3b F2
    {0, 0},           // 0x3c F3
    {0, 0},           // 0x3d F4
    {0, 0},           // 0x3e F5
    {0, 0},           // 0x3f F6
    {0, 0},           // 0x40 F7
    {0, 0},           // 0x41 F8
    {0, 0},           // 0x42 F9
    {0, 0},           // 0x43 F10
    {0, 0},           // 0x44 F11
    {0, 0},           // 0x45 F12
    {0, 0},           // 0x46 Print Screen
    {0, 0},           // 0x47 Scroll Lock
    {0, 0},           // 0x48 Pause
    {0, 0},           // 0x49 Insert
    {0, 0},           // 0x4a Home
    {0, 0},           // 0x4b Page Up
    {0, 0},           // 0x4c Delete
    {0, 0},           // 0x4d End
    {0, 0},           // 0x4e Page Down
    {0, 0},           // 0x4f Right Arrow
    {0, 0},           // 0x50 Left Arrow
    {0, 0},           // 0x51 Down Arrow
    {0, 0},           // 0x52 Up Arrow
    {0, 0},           // 0x53 Num Lock
    {'/', '/'},       // 0x54 KP /
    {'*', '*'},       // 0x55 KP *
    {'-', '-'},       // 0x56 KP -
    {'+', '+'},       // 0x57 KP +
    {'\r', '\r'},     // 0x58 KP Enter
    {'1', 0},         // 0x59 KP 1 / End
    {'2', 0},         // 0x5a KP 2 / Down Arrow
    {'3', 0},         // 0x5b KP 3 / Page Down
    {'4', 0},         // 0x5c KP 4 / Left Arrow
    {'5', '5'},       // 0x5d KP 5
    {'6', 0},         // 0x5e KP 6 / Right Arrow
    {'7', 0},         // 0x5f KP 7 / Home
    {'8', 0},         // 0x60 KP 8 / Up Arrow
    {'9', 0},         // 0x61 KP 9 / Page Up
    {'0', 0},         // 0x62 KP 0 / Insert
    {'.', 0},         // 0x63 KP . / Delete
    {0, 0},           // 0x64 Non-US \ | (JIS: ろ key uses 0x87)
    {0, 0},           // 0x65 Application (Menu)
    {0, 0},           // 0x66 Power
    {'=', '='},       // 0x67 KP =
};
