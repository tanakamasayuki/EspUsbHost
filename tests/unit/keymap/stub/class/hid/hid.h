// Host-build stub for TinyUSB's <class/hid/hid.h>.
//
// keymap_en_us.h initializes its table from the TinyUSB HID_KEYCODE_TO_ASCII
// macro, which is only available in an ESP32/TinyUSB build. The en_US table is
// the fallback for several layouts but is NOT exercised by the host unit tests
// (the assertions cover ja_jp / nl_NL / pt_BR and the range guard, which returns
// 0 for en_US before ever indexing the table). We therefore stub the macro with
// 128 zero entries. Deliberately zero — not plausible-but-fake values — so that
// any future en_US assertion added here fails loudly instead of silently passing
// against wrong data.
#pragma once

// clang-format off
#define HID_KEYCODE_TO_ASCII \
  {0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0}, \
  {0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0}, \
  {0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0}, \
  {0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0}, \
  {0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0}, \
  {0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0}, \
  {0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0}, \
  {0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0}
// clang-format on
