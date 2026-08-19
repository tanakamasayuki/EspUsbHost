# EspUsbHostPrinterEscPos

> 日本語版: [README.ja.md](README.ja.md)

Print on a USB receipt printer: ESC/POS text with a Japanese font, a barcode, a QR
code, and the auto cutter. Verified against an Xprinter XP-C58K (`0483:070b`).

> **Status: working, verified on an ESP32-S3.** The bytes below were confirmed
> against the real printer by `tests/manual/printer_escpos` (no paper used) and
> `tests/manual/printer_print` (one slip). Their output is in
> [What was measured](#what-was-measured).

| File | Contents |
|---|---|
| `PrinterProtocol.hpp` | The USB Printer Class: the three class requests, the `GET_PORT_STATUS` bits, the IEEE 1284 device ID format and field lookup. No Arduino / USB dependencies |
| `EscPos.hpp` | The ESC/POS print data language: a bounds-checked command builder, and the real-time status replies. No Arduino / USB dependencies |
| `PrinterDevice.hpp` | Find the printer interface, claim it through the vendor bulk API, run the class requests on EP0, send print data and read status on the bulk endpoints. No layout, no VID/PID |
| `ReceiptJa.hpp` | The content: a Japanese receipt as Shift-JIS byte arrays, plus an ASCII-only fallback slip. Replace this file for another layout or language |
| `EspUsbHostPrinterEscPos.ino` | Connect, print the device ID and paper status, then optionally print one receipt |

## Why this is under `examples/Vendor/`

**The printer interface class is `0x07`, not the vendor-specific `0xff`.**

The `examples/` directories are organised by *which library API an example drives*,
not by the device's USB class. `Vendor/` means "built on the vendor bulk/control
API", which is what this example does: the library has no printer support of its
own. Its neighbour `EspUsbHostUsbtmcScpi` is class `0xfe` and sits here for the
same reason.

Directories named after a class (`Ccid/`, `UsbNetwork/`, `Storage/`, `Audio/`) exist
only where the library has a dedicated API for that class.

## Paper

Printing consumes paper, so both actions are off by default in the sketch:

```cpp
static constexpr bool PRINT_SAMPLE = false;  // print one receipt
static constexpr bool CUT_PAPER = false;     // cut it afterwards
```

As shipped the sketch only reads the device ID and the paper status - nothing moves.
`PrinterDevice::checkPaper()` is called before printing and refuses when the printer
reports paper out or an error; that check matters because print data sent to a
printer that is out of paper is *buffered*, and comes out mixed into whatever is
printed after the roll is replaced.

## How it works

The class carries a print data stream and says nothing about its contents. So there
are two independent layers: a USB layer of two bulk endpoints plus three EP0
requests, and ESC/POS as the payload.

### The library side

Nothing needed adding to the library. This is the whole USB surface:

```cpp
// The interface number is explicit, so a class 0x07 interface is claimed even
// though it is not vendor-specific. READ_ON_DEMAND is right here: print data is
// one-way and status is only read when asked for, so a continuous IN transfer
// would sit there NAKing.
usb.vendorOpen(address, interfaceNumber, ESP_USB_HOST_VENDOR_READ_ON_DEMAND);
usb.vendorSetAutoZlp(true, address);   // terminate every OUT with a short packet
usb.vendorWrite(receipt, length, address);
usb.vendorReadSync(reply, sizeof(reply), &received, timeoutMs, address);
usb.vendorControlTransfer(0xa1, printer::REQ_GET_DEVICE_ID, 0, wIndex,
                          data, sizeof(data), &received, address);
```

A whole receipt goes out in **one** `vendorWrite()`. A printer starts printing as
soon as it has a full line, so a receipt split across transfers the host might delay
comes out stuttering, and on some models as a partial line before a timeout.

### The three class requests

| Request | bmRequestType | bRequest | Notes |
|---|---|---|---|
| GET_DEVICE_ID | `0xa1` | 0x00 | wValue = configuration index, **wIndex = (interface << 8) \| alternate setting** |
| GET_PORT_STATUS | `0xa1` | 0x01 | One byte; wIndex = interface |
| SOFT_RESET | `0x21` | 0x02 | No data stage; flushes the printer's buffers |

`GET_DEVICE_ID` is the odd one out: its wIndex is byte-swapped relative to the
others, which is why `printer::deviceIdIndex()` builds it rather than the call site.

The device ID itself is a two-byte **big-endian** length that counts itself,
followed by `KEY:value;` fields. Printers use either spelling of each key - `MFG`
or `MANUFACTURER`, `MDL` or `MODEL`, `CMD` or `COMMAND SET` - so both are worth
trying.

### GET_PORT_STATUS, and why `0x00` is treated as "no answer"

The bits are the old Centronics status lines, and two of them are inverted:

| bit | meaning |
|---|---|
| 5 | PaperEmpty - **1 when the paper is gone** |
| 4 | Select - 1 when the printer is selected |
| 3 | NotError - **1 when there is no error** |

Taken literally, `0x00` says "deselected, error, paper present". That is not a state
a printer that is answering EP0 and printing happily is in - and `0x00` is exactly
what the XP-C58K returns, every time, before and after every other exchange and
after `SOFT_RESET`, while its real-time status reports it ready throughout.

So `decodePortStatus()` reports `0x00` as `unknown` rather than as what it decodes
to. Reading it literally would have a print loop refuse to print on a healthy
printer. The cost is that a genuine deselected-with-error state that reports exactly
`0x00` reads as unknown, which is the safer way round: the caller falls back to the
real-time status, which is a real answer.

### Real-time status: `DLE EOT n`

This is the status path that actually works, and it is ESC/POS rather than USB. The
printer answers it ahead of its print buffer, so it works while the printer is busy
or offline - which is the point of it.

| n | reports |
|---|---|
| 1 | printer status (bit 3 = offline) |
| 2 | offline cause (cover open, paper feed button) |
| 3 | error status (bit 3 = an error state: cutter jam, cover open) |
| 4 | paper roll sensor (bits 2,3 = near end; bits 5,6 = out) |

Every reply has bit 0 clear and bit 1 set. Checking those two is how you tell a
status byte from print data a confused device echoed back, and the manual tests do.

`PrinterDevice::checkPaper()` prefers this path and falls back to
`GET_PORT_STATUS`, so a unidirectional printer (protocol `0x01`, no bulk IN) still
gets an answer where it can.

### Japanese text

A Japanese ESC/POS printer decodes two-byte characters as **Shift-JIS** (or JIS,
selected with `FS C n`) from its own font ROM. It knows nothing about UTF-8.

```cpp
out.kanjiCode(escpos::KANJI_CODE_SHIFT_JIS);  // FS C 1
out.kanjiOn();                                // FS &
out.bytes(sjisBytes, sizeof(sjisBytes));
out.kanjiOff();                               // FS .
```

`ReceiptJa.hpp` keeps the sample text as Shift-JIS byte arrays with the original
text in a comment above each one. Byte arrays rather than string literals on
purpose: a Shift-JIS second byte can be an ASCII hex digit, and `"\x82"` followed by
such a character is one `\x` escape in C, not two bytes.

Kanji mode must be switched off again for ASCII lines - with it on, a byte in
`0x81..0x9f` is taken as the first half of a two-byte character and swallows the byte
after it. The host unit test checks every `FS &` has its `FS .`.

For text that is not fixed at build time: keep a UTF-8 → Shift-JIS table for the
subset you need, or render to a bitmap and print it with `raster()`, which also
covers characters the font ROM lacks.

### Commands the builder emits

| Purpose | Command |
|---|---|
| Initialise | `ESC @` |
| Code table / kanji code | `ESC t n` / `FS C n` |
| Kanji mode | `FS &` / `FS .` |
| Align, bold, underline, inverse | `ESC a n`, `ESC E n`, `ESC - n`, `GS B n` |
| Character size | `GS ! n` (`(width-1) << 4 \| (height-1)`, 1..8 each) |
| Feed | `LF`, `ESC d n` |
| Cut | `GS V m [n]` - **66 feeds first**, which a receipt needs or the last lines are cut through |
| Barcode | `GS k m n d1..dn` (length-prefixed form, so any byte is allowed) |
| QR code | `GS ( k` - five commands: model, module size, error correction, store, print |
| Raster image | `GS v 0 m xL xH yL yH ...` - 1 bpp, MSB first, rows padded to bytes |
| Real-time status | `DLE EOT n` |

`escpos::Builder` is bounds-checked and latches an overflow flag instead of
truncating silently, and `PrinterDevice::write()` refuses an overflowed builder. That
matters more than it sounds: a truncated receipt is missing its tail, which may be
the cut command or the arguments of the command before it - and a printer waiting for
missing arguments eats the start of the *next* receipt.

## What was measured

`tests/manual/printer_escpos` (uses no paper) on an ESP32-S3:

```
printer address=2 interface=0 protocol=0x02 bidirectional=1 bulk_out=0x01 bulk_in=0x82 mps=64
device id raw 2 bytes: 00 02
device id ""
device id is empty - the request works, the printer has no ID string
port status 0x00 unknown=1 paper_empty=0 selected=0 error=0
port status carries no information on this printer
DLE EOT 1 0x16
DLE EOT 2 0x12
DLE EOT 3 0x12
DLE EOT 4 0x12
paper near_end=0 out=0
checkPaper out=0 near_end=0 error=0
repeated polling 20/20 answered
SOFT_RESET ok
port status after reset 0x00 unknown=1
DLE EOT 4 after reset 0x12
```

`tests/manual/printer_print` (one slip, cut):

```
status before: printer=0x16 offline=0 error=0x12 error_state=0 paper=0x12 near_end=0 out=0
receipt 586 bytes, cut=1
receipt sent
status after: printer=0x16 offline=0 error=0x12 error_state=0 paper=0x12 near_end=0 out=0
status after printing 5/5 answered
```

And on the slip, confirmed by looking at it: the Japanese title, the CODE128 barcode,
the QR code, and a clean cut. So this printer's font ROM is Shift-JIS - `FS C 1` plus
`FS &` with Shift-JIS bytes is the right combination for it.

The descriptors, for reference (`tests/manual/device_dump`):

```
VID:PID 0483:070b class=0x00(per-interface)
Strings manufacturer="Xprinter " product="" serial=""
  Interface 0 alt=0 class=0x07(Printer) subclass=0x01 protocol=0x02 endpoints=2
    Endpoint iface=0 ep=0x01 dir=OUT type=bulk max_packet=64
    Endpoint iface=0 ep=0x82 dir=IN  type=bulk max_packet=64
```

Enumeration logs `ENUM: Device returned less bytes than requested` twice: the printer
declares product and serial string descriptors and then returns them short. Harmless
- enumeration continues and everything works. It is registered as a known finding in
`tests/conftest.py` so the serial audit does not report it as new.

### What the printer taught us

1. **The class requests are optional, and this one implements neither usefully.**
   `GET_DEVICE_ID` returns a well-formed *empty* ID (`00 02`), and `GET_PORT_STATUS`
   always returns `0x00`. Both are answered, so neither stalls - a host cannot tell
   "unimplemented" from "nothing to report" except by what comes back. The first
   version of this example treated both as failures; `tests/probe/printer_class`
   swept the addressing variants and showed the requests were fine and the printer
   simply has nothing to say.
2. **Real-time status is the status path to rely on.** It answered 20/20 polls, and
   5/5 immediately after a 586-byte print transfer.
3. **The Shift-JIS path is right for this model.** Kanji and katakana came out as the
   text intended, so `FS C 1` (Shift-JIS) plus `FS &` and Shift-JIS bytes is what its
   font ROM wants. This is the one result that cannot be read off a log.
4. **`SOFT_RESET` is safe here.** Both the EP0 and the bulk path still worked after
   it - worth checking, because the USBTMC example had to drop
   `CLEAR_FEATURE(ENDPOINT_HALT)` for desynchronising the data toggle.

## Retargeting

- **Another layout or language**: replace `ReceiptJa.hpp`. Nothing else knows what a
  receipt is.
- **Another printer**: `PrinterProtocol.hpp`, `EscPos.hpp` and `PrinterDevice.hpp`
  contain no VID/PID and no model limits. Two things to check: whether the printer
  has a two-byte font ROM at all (`PRINT_JAPANESE = false` prints the ASCII slip
  instead), and its paper width - `receipt::COLUMNS` is 32, which is 58 mm in font A;
  an 80 mm printer is 48.
- **A unidirectional printer** (protocol `0x01`): everything prints, but there is no
  bulk IN, so `realtimeStatus()` returns false and only `GET_PORT_STATUS` remains.

## References

- USB-IF, *Universal Serial Bus Device Class Definition for Printing Devices*, version 1.1
- IEEE 1284-2000, device ID string format
- Epson, *ESC/POS Command Reference* (public documentation of `ESC`, `GS`, `FS` and `DLE EOT` commands)

Implemented from these public specifications. No GPL-licensed printer driver was
consulted.

> Xprinter is a trademark of its respective owner; Epson and ESC/POS are trademarks
> of Seiko Epson Corporation. This project is not affiliated with, endorsed by, or
> certified by either.
