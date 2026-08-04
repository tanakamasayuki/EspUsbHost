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

- `dl1xx`: verifies the DL-1xx protocol layer in
  `examples/Vendor/EspUsbHostDisplayDl1xx` (`Dl1xxProtocol.hpp`,
  `Dl1xxModes.hpp`): the 16-bit LFSR that encodes timing register values
  (reference values plus the maximal-length property that pins the tap set),
  register write byte order including the pixel clock's low-byte-first exception,
  the RLE pixel encoder (the documented 10-byte solid-run form, the 519-byte
  worst case, round trips against an independent decoder, buffer limits with an
  overrun canary), and the Full HD mode-set register stream.

- `ccid_atr`: verifies the CCID ATR parser in `src/EspUsbHostCcidAtr.h`: the ATR
  captured from a real Sony RC-S300 with an ISO 14443 A card (decoded to
  standard, level, card name and announced protocols), the PC/SC PIX.SS mapping
  for ISO 14443 A/B, ISO 15693, ISO 7816-10 memory cards, FeliCa and
  low-frequency contactless, an unlisted standard code keeping its raw value
  instead of being reported as an ISO 7816 card, a contact card's own ATR
  (interface-byte walking to find the historical bytes, T=1 detection, the T=0
  default), and rejected inputs (null, missing T0, invalid TS, truncated
  historical bytes, missing TD1, wrong RID, TLV longer than the historical
  bytes).

- `audio_uac`: verifies the USB Audio descriptor and control decoders in
  `src/EspUsbHost.h`: the Feature Unit `bmaControls` layout for both class
  revisions (UAC1's `bControlSize` stride versus UAC2's fixed 4 bytes, including
  a UAC2 descriptor being rejected when read with UAC1 rules), control masks as
  one bit per control on UAC1 and 2-bit present / read-only / programmable fields
  on UAC2, the isochronous usage type that identifies an explicit feedback
  endpoint (and does not confuse implicit feedback data with it), and the UAC2
  `RANGE` responses: `wNumSubRanges`, discrete rates, continuous subranges walked
  by their resolution, truncated payloads, duplicate and zero rates, caller
  capacity limits, and the signed 1/256 dB volume range.

## How it works

The `dl1xx` headers and `src/EspUsbHostCcidAtr.h` are pure byte formatting with
no Arduino / USB dependencies, so `test_dl1xx.py` and `test_ccid_atr.py` compile
them directly and need no extraction step.

`src/EspUsbHost.h` pulls in Arduino and the ESP USB host stack, so `audio_uac`
extracts the audio constants, structs and `inline` decoders it needs into
`output/espusbhost_audio_real.h` the same way the keymap test does.

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
