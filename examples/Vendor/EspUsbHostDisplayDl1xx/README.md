# EspUsbHostDisplayDl1xx

日本語: [README.ja.md](README.ja.md)

Drive a USB graphics display adapter based on the DL-1xx chips and expose it as a
LovyanGFX panel.

**Work in progress.** Only the protocol layer is present so far:

| File | Contents | Status |
|---|---|---|
| `Dl1xxProtocol.hpp` | `0xAF` bulk command stream: register writes, RLE pixel writes, the timing-register LFSR | Done, host-tested |
| `Dl1xxModes.hpp` | Standard VESA/CEA timings and the mode-set register sequence | Done, host-tested |
| `Dl1xxDevice.hpp` | Device layer: claim, channel key, EDID, mode set, pixel push | Not yet |
| `Panel_Dl1xx.hpp` | `lgfx::Panel_Device` subclass | Not yet |
| `EspUsbHostDisplayDl1xx.ino` | The example itself | Not yet |

Both headers are deliberately free of Arduino, LovyanGFX and USB dependencies, so
they compile on the host. `tests/unit/dl1xx` compiles them with g++ and checks the
LFSR, the register byte order, the RLE encoder (against an independent decoder)
and the Full HD mode-set stream. Run it with:

```sh
cd tests
uv run --env-file .env pytest unit/dl1xx -v -s
```

The design, the remaining phases and the measured throughput figures are in
[`docs/usb-display-spec.ja.md`](../../../docs/usb-display-spec.ja.md).

## Hardware

USB graphics adapters built on the DisplayLink DL-1xx chips (VID `0x17e9`):
DL-120 / DL-160 ("Alex") and DL-115 / DL-125 / DL-165 / DL-195 ("Ollie") speak the
same protocol. DL-165 reaches 1920x1080; DL-120 / DL-160 top out around
1600x1200.

These adapters draw significant current, so power the device from a
self-powered hub or an external supply unless the host board can supply it
through its OTG connector.

## Protocol references

The implementation was written from scratch using these sources:

- Florian Echtler's published reverse-engineering notes on the DL-1xx protocol
- OpenBSD `sys/dev/usb/udl.c` (ISC license)
- [Pico_USB_Disp](https://github.com/htlabnet/Pico_USB_Disp) `docs/protocol.md`
  (MIT license)

It contains no code from the GPL-2.0 `udlfb` driver, from LGPL-2.1 `libdlo`, or
from any Synaptics DisplayLink SDK.

DisplayLink is a trademark of Synaptics Incorporated. This project is not
affiliated with, endorsed by, or certified by Synaptics.
