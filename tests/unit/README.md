# Unit Tests

[日本語](README.ja.md)

Pure C++ / data-conversion tests that run on the host with g++. No board or
serial port is required.

```sh
uv run --env-file .env pytest unit/
```

## Tests

- `keymap`: verifies the HID usage -> character conversion in
  `src/EspUsbHostHid.cpp` against each layout's national standard (Windows layout
  data / ReactOS `kbd*.c`). Covers `espUsbHostKeycodeToUnicode` (Unicode code
  points) and its `espUsbHostKeycodeToAscii` Latin-1 wrapper, including: dead
  keys (-> 0), the AltGr (Right Alt) and AltGr+Shift levels across de/fr/es/it/
  nl/da/no/sv/fi/en_GB/pt_PT/fr_CH/hu, `€` and other non-Latin-1 AltGr output
  (unicode set, ascii 0), the ABNT2 `/ ?` and numpad comma keys, and the
  `keycode >= 0x80` range guard (only ja_jp / pt_BR may reach `0x80..0x8f`).

## How it works

`src/EspUsbHostHid.cpp` includes `Arduino.h` and the ESP USB host stack, so it
cannot be compiled on the host directly. To test the real conversion code
without duplicating it, `test_keymap.py` extracts the `EspUsbHostKeyboardLayout`
enum, the `keymap/*.h` include list, the `MOD_*` constants and the pure
conversion functions (keypad, `espUsbHostKeycodeToUnicode`, and the
`espUsbHostKeycodeToAscii` wrapper) from the actual sources, concatenates them into
`output/espusbhost_keymap_real.h`, and compiles that together with
`keymap_test.cpp`. The assertions therefore exercise the production tables and
logic. The `stub/` directory provides a host stand-in for TinyUSB's
`<class/hid/hid.h>` (the en_US fallback table, which these tests do not
exercise).
