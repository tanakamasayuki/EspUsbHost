# EspUsbHostDisplayDl1xx

日本語: [README.ja.md](README.ja.md)

Drive a USB graphics display adapter based on the DL-1xx chips and expose it as a
LovyanGFX panel.

The sketch keeps only what you would change -- what to draw, and at what
resolution. Everything adapter-specific sits in the headers beside it, so they can
be copied into another project as they are:

| File | Contents |
|---|---|
| `Dl1xxProtocol.hpp` | `0xAF` bulk command stream: register writes, RLE pixel writes, the timing-register LFSR. No Arduino / LovyanGFX / USB dependencies |
| `Dl1xxModes.hpp` | Standard VESA/CEA timings and the mode-set register sequence. Same, host-compilable |
| `Dl1xxDevice.hpp` | Claim the vendor interface, select the channel, read EDID, program a mode, stream pixels through the asynchronous bulk OUT queue |
| `Panel_Dl1xx.hpp` | `lgfx::Panel_Device` subclass plus the `LGFX_Dl1xx` device |

## Requirements

- **LovyanGFX** and **LGFXVirtualCanvas 1.2.0 or later** (the version that added
  diff transfer). Both are declared in `sketch.yaml`.
- A USB graphics adapter with a DisplayLink DL-1xx chip (VID `0x17e9`).
  DL-120 / DL-160 ("Alex") and DL-115 / DL-125 / DL-165 / DL-195 ("Ollie") speak
  the same protocol. DL-165 reaches 1920x1080; DL-120 / DL-160 top out around
  1600x1200.
- Power for the adapter. These draw significant current, so use a self-powered hub
  or an external supply unless the host board feeds its OTG connector.

## How it works

The adapter has its own frame buffer and keeps scanning out of it with no USB
traffic at all, so nothing has to be refreshed periodically. The panel therefore
holds no frame buffer either: each drawing operation is turned straight into RLE
pixel commands and sent.

LGFXVirtualCanvas renders the screen as a series of horizontal bands through one
small sprite, so a Full HD surface needs no full-size buffer on the host — a few
tens of KB is enough. Its diff transfer then skips bands that did not change,
which pairs well with an adapter that holds its own image.

Measured on an ESP32-S3 (full-speed USB) with a DL-165 adapter:

| | |
|---|---|
| Frame rate | 3 fps at 1920x1080 |
| Diff transfer | 215,040 of 2,073,600 pixels pushed per frame (10.4%) |
| USB throughput | about 42 KB/s, roughly 4% of the 1.098 MB/s full-speed ceiling |

USB is not the limit here — the draw callback is, because LGFXVirtualCanvas
re-runs it for every band. To go faster, make the bands larger
(`setMemoryLimit()`), update only what changed with `LGFXVirtualSprite`, or draw
less per frame.

## Limitations

- Rotation is not supported. The write paths address the device frame buffer
  linearly, which only holds for rotation 0; other values are forced back to 0.
- No read-back (`isReadable()` is false), so `readRect()`, ARGB blending and
  anything that needs the current screen contents do not work.
- `copyRect()` does nothing. On-screen rectangle copies would need the `AF 6A`
  command, which is not implemented yet.
- 16 bpp only. The chips also support a 24 bpp dual-plane mode.
- One adapter at a time.
- After a monitor-side HPD event (unplugging the monitor, a capture device
  closing) the output goes black and pixel writes do not bring it back. Re-sending
  the mode registers does; call `Dl1xxDevice::resendMode()`.

## Tests

The protocol layer is covered by a host unit test, since an encoder bug is only
visible as a corrupted image on real hardware:

```sh
cd tests
uv run --env-file .env pytest unit/dl1xx -v -s
```

Real-hardware bring-up (EDID, mode set, solid fills, color bars, checkerboard,
image persistence, mode resend):

```sh
uv run --env-file .env pytest manual/usb_display_dl1xx/usb_display_dl1xx.py -v -s
```

The design, the phase breakdown and the measured figures are in
[`docs/usb-display-spec.ja.md`](../../../docs/usb-display-spec.ja.md).

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
