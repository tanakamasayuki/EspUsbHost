# EspUsbHostDisplayDl1xx

> 日本語版: [README.ja.md](README.ja.md)

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
  or an external supply unless the host board feeds its OTG connector. On our
  ESP32-P4 board the host port could not supply enough for the adapter at all, and
  a self-powered hub was required. Keep other devices off that hub —
  [see below](#on-an-esp32-p4-the-bus-stops-mattering) for what a full-speed
  device sharing it can do.

## How it works

The adapter has its own frame buffer and keeps scanning out of it with no USB
traffic at all, so nothing has to be refreshed periodically. The panel therefore
holds no frame buffer either: each drawing operation is turned straight into RLE
pixel commands and sent.

LGFXVirtualCanvas renders the screen as a series of horizontal bands through one
small sprite, so a Full HD surface needs no full-size buffer on the host — a few
tens of KB is enough. Its diff transfer then skips bands that did not change,
which pairs well with an adapter that holds its own image.

As written, this sketch redraws everything every frame and reaches about 2.5 fps
at 1920x1080 on an ESP32-S3 (5.8 fps on an ESP32-P4), using a few percent of the
USB bandwidth. USB is not the limit — drawing is. See
[Making it faster](#making-it-faster) below.

## Scope: this is a vendor-protocol example, not a display library

This example exists to show what the generic vendor bulk API can do, and to keep
the DL-1xx protocol out of the library core. It deliberately covers one chip
family, 16 bpp, one adapter at a time, rotation 0, and no read-back.

**If you need more than that, use a library built for the job.**
[Pico_USB_Disp](https://github.com/htlabnet/Pico_USB_Disp) (MIT) supports several
adapter families beyond DL-1xx (MacroSilicon MS912x / MS913x, MCT Trigger 6), has
its own ESP32 backend, and is written for throughput rather than as a protocol
demonstration. It is the better starting point when you want a different adapter,
higher frame rates, or a maintained display stack. Nothing here depends on it;
the two are independent implementations of the same published protocol notes.

## Making it faster

Numbers below are from `tests/manual/usb_display_throughput` on an ESP32-S3
(full-speed USB) with a DL-165 at 1920x1080. "bus" is the share of the 1.098 MB/s
full-speed ceiling measured by `tests/manual/vendor_bulk_throughput`.

**Find out what is limiting you first.** With this adapter the answer flips
depending on what you draw:

| Scene | fps | USB | bus | Limited by |
|---|---|---|---|---|
| Solid fill | 3.65 | 28 KB/s | 2.5% | drawing |
| Vertical gradient | 3.22 | 27 KB/s | 2.4% | drawing |
| Text + color bars + moving circle | 2.53 | 70 KB/s | 6.2% | drawing |
| Pseudo-random pixels | 0.27 | 1122 KB/s | 99.8% | USB |

Anything the RLE encoder compresses well leaves the bus almost idle, and the
limit is the draw callback. Only noise-like content saturates USB.

**Redraw less.** This is worth more than every other knob combined. A
`LGFXVirtualSprite` over just the part that changes:

| | fps |
|---|---|
| Whole screen every frame | 2.53 |
| 240x120 sprite over the moving area | **91.64** |

**Use fewer, larger bands.** The draw callback re-runs per band, so band count is
close to a direct multiplier on draw cost:

| Tile budget | Bands | Band height | fps |
|---|---|---|---|
| default (~19 KB) | 216 | 5 px | 2.15 |
| 32 KB | 135 | 8 px | 2.41 |
| 64 KB | 64 | 17 px | 2.53 |
| 96 KB | 44 | 25 px | **2.57** |
| 128 KB | - | - | allocation fails |

Somewhere between 96 and 128 KB is the ceiling for a single tile buffer in ESP32-S3
internal RAM at Full HD, and it moves with how much else the sketch has allocated —
an earlier build of this same sweep could not fit 96 KB either. The gain flattens
out well before that ceiling (64 KB to 96 KB buys 1.6%), so there is no reason to
sit right at the edge.

**Skip double buffering when you are draw-bound.** It overlaps a band's transfer
with the next band's drawing, which only helps if transfer is a real cost:

| | fps |
|---|---|
| 32 KB, single buffer | 2.41 |
| 32 KB, double buffer | 2.41 |

At 6% bus use there is nothing to hide, and the second buffer doubles the tile
RAM — memory that buys more with a larger single tile instead. Call
`setDoubleBuffer(false)` explicitly; the default turns it on for any surface that
needs two or more bands.

**Keep diff transfer on.** The hash cost is small next to what it saves:

| | fps | Pixels sent | USB |
|---|---|---|---|
| `setDiffMode(Tile)` | 2.53 | 14% of the screen | 70 KB/s |
| `Off` | 1.66 | 100% | 187 KB/s |

**Drawing straight to the panel is faster, but it flickers.** Without
LGFXVirtualCanvas there is no per-band callback re-run, so a full redraw is
quicker than the tiled path — except the screen is cleared in front of the viewer
because there is no buffer to hide it:

| | fps | USB | bus |
|---|---|---|---|
| Direct, clear and redraw everything | 5.75 | 679 KB/s | 60% |
| Direct, repaint only the moving part | 692 | 1123 KB/s | 100% |

There is no buffer between the drawing code and the screen, so a full clear is
visible: at 5.75 fps the adapter shows the cleared background for a good part of
each 174 ms frame, and the content is painted back over it. Expect tearing and
flicker, and do not use this pattern for animation. Repainting only what moved
avoids it entirely and is fast enough to saturate the bus, but you have to erase
the old content yourself. The tiled path buys you the "just redraw everything"
programming model at the cost of re-running the callback per band.

**`setAutoClear(false)` changes little** (2.56 vs 2.52 fps) when the scene starts
with its own `fillScreen`, and it is unsafe when it does not.

### On an ESP32-P4 the bus stops mattering

The same sweep on an ESP32-P4 runs the adapter at high speed, where
`vendor_bulk_throughput` measures 36.4 MB/s instead of 1.098 MB/s:

| Condition | fps (S3) | fps (P4) | P4 USB | P4 bus |
|---|---|---|---|---|
| Whole screen, 64 KB tiles | 2.53 | 5.76 | 119 KB/s | 0.3% |
| Sprite over the moving area | 91.64 | 185.96 | 336 KB/s | 0.9% |
| Direct, clear and redraw everything | 5.75 | 12.24 | 1445 KB/s | 3.9% |
| Direct, repaint only the moving part | 692 | ~1000 | 1623 KB/s | 4.4% |
| Pseudo-random pixels | 0.27 | 1.55 | 6392 KB/s | 17.1% |

The ordering of every knob is unchanged, because they all act on draw cost: fewer
larger bands is still 1.20x, diff transfer still 1.7x, double buffering still
nothing, and redrawing only what changed is still worth about 32x. The P4 also
fits a 128 KB tile, which an ESP32-S3 cannot allocate, so the band count can drop
to 32.

What changes is which side is the limit, and the worst case shows it. A frame that
the RLE encoder cannot compress at all costs

```
256 px per command -> 6 + 1 + 512 = 519 bytes
1920 x 1080 / 256  = 8,100 commands
                   = 4,203,900 bytes per frame (1.4% protocol overhead)
```

which the checkerboard step of `usb_display_dl1xx` confirms at 4,209,523 bytes.
Against that frame size the bus alone allows:

| | Ceiling | Worst-case frame | Measured |
|---|---|---|---|
| ESP32-S3, full speed | 1.098 MB/s | **0.27 fps** | 0.27 fps, 99.8% of the bus |
| ESP32-P4, high speed | 36.4 MB/s | **9.08 fps** | 1.55 fps, 17.1% of the bus |

So full speed is exactly transfer-bound in the worst case — the measurement lands
on the arithmetic — while high speed is not: at 1.55 fps the encoder and the
per-band redraw give out with 83% of the bus unused. Raising the bus 33x bought
5.7x on the hardest content, and nothing at all on content that compresses, where
the draw callback was already the limit.

**Keep the adapter alone on its hub.** These P4 numbers were taken through a
self-powered hub, which the adapter needs: connected straight to the host port it
did not get enough current to run at all. The hub itself is not the problem — but
another device on it that fails its downstream port reset is. With a full-speed
touch panel sharing the high-speed hub, ESP-IDF's own external-hub driver aborted
the whole USB host right after `begin()`:

```
E (5678) EXT_PORT: [1:2] Failed to issue downstream port reset
assert failed: handle_recycle ext_port.c:891 (ext_port->status.wPortChange.val == 0)
```

Note the port: `[1:2]` is hub port 2, the touch panel, not port 4 where the
adapter was. Removing that device fixed it, with the adapter still behind the same
hub. This is reproducible 100% of the time and independent of the sketch — the
prebuilt IDF is configured with `CONFIG_USB_HOST_EXT_PORT_RESET_ATTEMPTS=1`, so
one failed reset is fatal, and because the failure path is an assert no library
code can intercept it.

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

The tuning sweep the numbers above come from:

```sh
uv run --env-file .env pytest manual/usb_display_throughput/usb_display_throughput.py -v -s
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
