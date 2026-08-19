# Unit Tests

> 日本語版: [README.ja.md](README.ja.md)

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

- `turing`: verifies the protocol layer of the 3.5-inch USB smart screen in
  `examples/Serial/EspUsbHostDisplayTuring` (`TuringProtocol.hpp`): the
  6-byte command packet with its four 10-bit coordinates, round-tripped against
  an independent decoder one field at a time and exhaustively over the panel's
  coordinate space, the DISPLAY_BITMAP rectangle (inclusive on both ends), the
  bounds guard for both the panel size and the packing limit, the 11-byte
  orientation packet with its big-endian size fields, the brightness levels that
  run backwards on the wire, and the RGB565 little-endian pixel bytes that let
  LovyanGFX's rgb565_nonswapped output reach USB without a byte swap.

- `ax206`: verifies the protocol layer of the AX206 USB display in
  `examples/Vendor/EspUsbHostDisplayAx206` (`Ax206Protocol.hpp`): the two 16-byte
  vendor command blocks reproduced byte for byte from the MIT reference, which
  anchor everything else; the blit rectangle with its inclusive corners and its
  data length; the bounds guard; the 31-byte Command Block Wrapper with its
  little-endian tag and transfer length, direction flag, LUN and command length;
  the Command Status Wrapper located by signature rather than offset, including
  tag mismatch, truncation and leading stray bytes; and the RGB565 big-endian
  pixel bytes that let LovyanGFX's rgb565_2Byte output reach USB without a byte
  swap.

- `dp100`: verifies the ALIENTEK DP100 frame layer in
  `examples/HID/EspUsbHostDp100Power` (`Dp100Protocol.hpp`): CRC-16/MODBUS against
  the canonical "123456789" check value and against a second, table-driven
  implementation over every length up to a full report; the request frame byte for
  byte, including where the CRC lands once there is data and that the rest of the
  64-byte report stays zero; response decoding against reports captured from a real
  DP100 by `tests/probe/dp100`, together with every case that must be rejected
  rather than trusted (short read, wrong direction byte, bad CRC, corrupted body, a
  length field past the end of the read); the one-byte status body (0x01 success,
  0x00 failure); the DEVICE_INFO and BASIC_INFO field offsets and units pinned to
  those captures; and BASIC_SET - the captured setpoint report, the index flags a
  request needs (0x80 to read, 0x20 to write, a write to a bare index being answered
  with success and then ignored), and the round trip.

- `usbtmc`: verifies the USBTMC message layer in
  `examples/Vendor/EspUsbHostUsbtmcScpi` (`UsbtmcProtocol.hpp`): the 4-byte
  alignment every message ends on, the bTag sequence that must never yield 0 or
  repeat, the DEV_DEP_MSG_OUT and REQUEST_DEV_DEP_MSG_IN headers byte for byte
  including the little-endian TransferSize and the EOM / TermChar attributes,
  response header decoding against headers built independently from the field
  layout together with every out-of-sync case that must be rejected rather than
  trusted (wrong MsgID, bad bTagInverse, bTag 0, TransferSize past the read), the
  GET_CAPABILITIES bit fields, and the bytes a real PMX18-5A returns as a
  regression on the USB488 field offsets, which sit at 14 and 15 because
  bcdUSB488 occupies 12..13.

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

- `felica_idm`: verifies the two protocol layers of the FeliCa IDm example in
  `examples/Ccid/EspUsbHostCcidFelicaIdm` (`FelicaProtocol.hpp` and
  `Rcs300Protocol.hpp`): the FeliCa Polling frame for the transit System Code
  0x0003 and for the wildcard 0xffff with the length byte that counts itself; the
  Polling answer with and without request data, the answering System Code, and its
  rejects (wrong response code, a declared length too small, too large, or past
  the buffer); every RC-S300 transparent session pseudo APDU byte for byte against
  what `tests/probe/rcs300_felica` actually sent (the four manage session
  commands, switch protocol to FeliCa, a transparent exchange carrying the
  Polling); the response objects the reader actually returned (accepted, switch
  protocol answering 8F 01 08, an exchange with nothing in the field, and the
  refusals 6301 / 6401 / 6700 / 6A81); and a successful exchange decoded all the
  way from response bytes to an IDm.

- `mouse_layout`: verifies the mouse report descriptor parser and report decoder
  in `src/EspUsbHostHidLayout.h`: the boot mouse descriptor producing exactly the
  layout the old fixed boot parsing assumed, the layout reported in issue #39 for
  a Logitech G502 HERO (16 buttons, 16-bit X/Y, wheel and AC Pan in 8 bytes)
  including the two regressions it caused (a Y-only report looking idle, X
  movement landing in the wheel), report IDs in a composite keyboard + mouse
  descriptor (offsets relative to the report body, a report with another ID
  rejected), a joystick collection whose Generic Desktop X / Y must not be taken
  for a mouse, 12-bit axes packed across byte boundaries with sign extension and
  Push / Pop, and rejects (null, empty and truncated descriptors, a collection
  without Y, decoding with an invalid layout).

- `escpos`: verifies the USB Printer Class request layer and the ESC/POS builder in
  `examples/Vendor/EspUsbHostPrinterEscPos` (`PrinterProtocol.hpp`, `EscPos.hpp`,
  `ReceiptJa.hpp`): the three class requests' bmRequestType and codes, and the
  byte-swapped wIndex that only GET_DEVICE_ID uses; the IEEE 1284 device ID with its
  self-counting big-endian length, an empty ID being well formed (which is what a real
  XP-C58K answers) while a declared length past the response is rejected, and field
  lookup that matches whole keys so CMDL does not answer a search for CMD;
  GET_PORT_STATUS bit senses including the two that are inverted, and 0x00 reported as
  no information rather than as the "deselected, error" it literally decodes to; every
  ESC/POS command the example emits byte for byte, notably the GS ! size packing with
  its clamping, the cut variants that do and do not take a feed argument, the
  length-prefixed barcode, the five-command QR sequence with its little-endian store
  length, and the GS v 0 raster header; the builder's overflow behaviour, which is what
  stops a receipt whose tail was dropped from being sent; and the receipt itself -- it
  fits the shipped buffer, kanji mode is switched off as many times as it is switched
  on, and the ASCII fallback slip stays single-byte throughout.

- `midi_cable`: verifies the USB MIDI Streaming cable-count decoder in
  `src/EspUsbHost.h` (`espUsbHostMidiEndpointCableCount()`): one embedded jack
  on a CS_ENDPOINT / MS_GENERAL descriptor being one cable, the count coming
  from `bNumEmbMIDIJack` rather than the jack IDs, the 4-bit cable-number limit
  (16 accepted, 17 rejected), zero jacks yielding zero cables, rejected
  descriptors (CS_INTERFACE instead of CS_ENDPOINT, a non-MS_GENERAL subtype, a
  jack array shorter than `bNumEmbMIDIJack`, a header too short to hold it),
  and the `EspUsbHostMidiPortInfo` zero defaults. The multi-cable samples match
  what the sibling EspUsbDevice library emits, so this test and the peer test
  describe the same layouts.

- `audio_uac`: verifies the USB Audio descriptor and control decoders in
  `src/EspUsbHost.h`: the Feature Unit `bmaControls` layout for both class
  revisions (UAC1's `bControlSize` stride versus UAC2's fixed 4 bytes, including
  a UAC2 descriptor being rejected when read with UAC1 rules), control masks as
  one bit per control on UAC1 and 2-bit present / read-only / programmable fields
  on UAC2, the isochronous usage type that identifies an explicit feedback
  endpoint (and does not confuse implicit feedback data with it), the feedback
  payload itself (10.14 in three bytes, 16.16 in four, samples per frame at full
  speed versus per microframe at high speed, fractional and 44.1 kHz values,
  short and null payloads) with the +/-12.5% window that guards the pacing rate,
  and the UAC2
  `RANGE` responses: `wNumSubRanges`, discrete rates, continuous subranges walked
  by their resolution, truncated payloads, duplicate and zero rates, caller
  capacity limits, and the signed 1/256 dB volume range. It also covers
  `espUsbHostSelectAudioStreamForFormat()`, which `audioInputStart()` /
  `audioOutputStart()` delegate to: exact matches, the `0` = no preference
  wildcards, ranking across alternates that declare different widths and rates,
  a pinned rate beating the scoring preference, continuous ranges, and
  format-only (`startable == false`) alternates being skipped.

## How it works

The `dl1xx`, `ax206`, `felica_idm`, `usbtmc`, `dp100` and `escpos` headers,
`src/EspUsbHostCcidAtr.h` and `src/EspUsbHostHidLayout.h` are pure byte
formatting with no Arduino / USB dependencies, so `test_dl1xx.py`,
`test_ax206.py`, `test_felica_idm.py`, `test_usbtmc.py`, `test_dp100.py`,
`test_escpos.py`, `test_ccid_atr.py` and `test_mouse_layout.py` compile them
directly and need no extraction step.

`src/EspUsbHost.h` pulls in Arduino and the ESP USB host stack, so `audio_uac`
extracts the audio constants, structs and `inline` decoders it needs into
`output/espusbhost_audio_real.h` the same way the keymap test does, and
`midi_cable` extracts the MIDI constants, `EspUsbHostMidiPortInfo` and the
cable-count decoder into `output/espusbhost_midi_real.h`.

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
