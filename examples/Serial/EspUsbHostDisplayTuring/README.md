# EspUsbHostDisplayTuring

> 日本語版: [README.ja.md](README.ja.md)

Drive a 3.5-inch USB smart screen — a Turing Smart Screen revision A or one of the
many compatibles — and expose it as a LovyanGFX panel.

The panel enumerates as a CDC serial device (`1a86:5722`, product `UsbMonitor`)
and takes 6-byte commands followed by raw RGB565 rectangles. It is the same idea
as [`EspUsbHostDisplayDl1xx`](../../Vendor/EspUsbHostDisplayDl1xx/) — a USB
display behind a LovyanGFX panel — over a completely different transport. See
[`docs/usb-display.md`](../../../docs/usb-display.md) for how the two compare.

The sketch keeps only what you would change — what to draw, and how the panel is
set up. Everything panel-specific sits in the headers beside it, so they can be
copied into another project as they are:

| File | Contents |
|---|---|
| `TuringProtocol.hpp` | The wire format: 6-byte command packing, the DISPLAY_BITMAP rectangle, orientation, brightness, RGB565 pixels. No Arduino / LovyanGFX / USB dependencies |
| `TuringDevice.hpp` | Find the panel, set the line coding, and stream commands and pixels through the asynchronous CDC OUT queue |
| `Panel_Turing.hpp` | `lgfx::Panel_Device` subclass plus the `LGFX_Turing` device |

## Requirements

- **LovyanGFX** and **LGFXVirtualCanvas 1.2.0 or later** (the version that added
  diff transfer). Both are declared in `sketch.yaml`.
- A 3.5-inch USB smart screen enumerating as `1a86:5722`. This is the hardware
  that reports itself as `USB35INCHIPSV2` and speaks the revision A Turing Smart
  Screen protocol. Native resolution 320x480.
- Power. The backlight draws real current, so use a self-powered hub or an
  external supply unless the host board feeds its OTG connector.

## How it works

The panel has its own frame buffer and keeps showing it with no USB traffic at
all, so nothing has to be refreshed periodically. The LovyanGFX panel therefore
holds no frame buffer either: each drawing operation turns straight into a
DISPLAY_BITMAP rectangle followed by its pixels.

There is no compression in this protocol. Every pixel sent costs exactly 2 bytes,
so a full 320x480 repaint is 307,200 bytes no matter what it contains. That makes
this example **transfer-bound**, the opposite of the DL-1xx one, and it is why
LGFXVirtualCanvas matters here: rendering the screen as horizontal bands lets diff
transfer skip the bands that did not change, and skipping a band skips its bytes.

The panel's color depth is fixed at `rgb565_nonswapped`, whose memory layout is
little-endian RGB565 — exactly the wire format. Converted pixels reach USB with no
byte swapping anywhere.

The measured example scene runs at **2 fps**, sending 26% of the screen per frame
(40,000 of 153,600 pixels, 160 KB/s) with zero transfer errors. The first frame
sends everything and takes about two seconds.

## Two things the panel requires

Neither is stated in the protocol references, and both fail in ways that look
like a working driver, so they are worth knowing before adapting this code.

**A command must not arrive before the previous rectangle's pixels have landed.**
The panel keeps consuming the bytes either way, so nothing errors and no transfer
is lost — the command is simply discarded along with the rectangle it opened.
`TuringDevice` therefore treats a bitmap as the unit of synchronisation: it
counts the pixels still owed to the open rectangle and drains the link when the
last one is in. A rectangle left short is padded rather than left owing, since
the panel counts those pixels itself and would otherwise read the next command as
pixel data (`underfilled()` reports that this happened).

Skip it and the symptom is a screen where only the first rectangle of each burst
appears, plus a throughput figure that gets *better* the more rectangles you send
— because most of them are being thrown away. That is what the band sweep below
guards against.

**A command must travel as its own USB transfer.** The panel takes a command from
the start of a transfer and ignores the remainder of it, so payload bytes packed
in behind a header are lost. The reference Python drivers get this for free, since
each `serial.write()` becomes its own transfer; here the header is written and
then pushed on its own. Skip it and every rectangle is drawn a few pixels
sideways, with the leftover pixels wrapping to the previous row.

## The CDC OUT queue

A display writer produces bytes far faster than a full-speed bulk endpoint drains
them, so it needs somewhere to push back. `sendSerial()` on its own does not push
back: it allocates a transfer per call and hands it to the driver, so a writer
that outruns the bus just grows the in-flight set until DMA memory runs out.

This example therefore calls `serialWriteQueueBegin()`, which preallocates a fixed
pool of transfers. Submits stay non-blocking, but acquiring a slot blocks once the
pool is busy, and that is what paces the whole path. Bytes are written straight
into a pooled DMA buffer:

```cpp
uint8_t *buffer = usb.serialWriteAcquire(&capacity, timeoutMs);
// ... fill it ...
usb.serialWriteSubmit(buffer, length);
```

While the queue is active `sendSerial()` and `EspUsbHostCdcSerial::write()` route
through it too, so ordinary serial code inherits the backpressure unchanged.

## Making it faster

Numbers below are from `tests/manual/usb_display_turing` on an ESP32-S3
(full-speed USB). The same host reaches 1.098 MB/s on vendor bulk, so the ceiling
here is the panel, not the bus.

**Send fewer pixels.** With no compression this is the only lever that changes
anything. The panel renders at a fixed ~0.155 MB/s, so time on screen is just
bytes divided by that rate, and the byte count is two per pixel. Leave
`setDiffMode(LGFXVirtualDiffMode::Tile)` on, and redraw only what moves.

**How you split the update does not matter.** The same 307,200 bytes cost the same
time whether they go out as one rectangle or as ninety-six:

| Rectangles | Rows each | Time | Rate |
|---|---|---|---|
| 1 | 480 | 1.883 s | 0.156 MB/s |
| 3 | 160 | 1.884 s | 0.156 MB/s |
| 8 | 60 | 1.884 s | 0.156 MB/s |
| 24 | 20 | 1.894 s | 0.155 MB/s |
| 48 | 10 | 1.911 s | 0.153 MB/s |
| 96 | 5 | 1.920 s | 0.153 MB/s |

The slight decline past 24 is the per-rectangle round trip, which is why
`setMemoryLimit(16 * 1024)` — about 25 rows per band — is a reasonable default:
big enough that the round trips are noise, small enough that diff transfer can
skip at a useful granularity. Nothing here rewards tuning it much.

If this table ever comes out *rising* instead of flat, rectangles are being
dropped rather than drawn; see the two requirements above.

**The queue shape does not matter either.** Depth 3 with 4 KB slots and depth 4
with 8 KB slots measure identically, because the panel NAKs until it has consumed
what it was sent. The pool exists for backpressure, not for throughput, so keep it
small.

## Limitations

- Rotation is done by the panel, not by LovyanGFX. `setRotation()` is forced back
  to 0; call `TuringDevice::setOrientation()` instead, before `init()`.
- No read-back (`isReadable()` is false), so `readRect()`, ARGB blending and
  anything that needs the current screen contents do not work.
- `copyRect()` does nothing. The protocol has no on-screen copy and read-back is
  not possible, so there is no way to implement it.
- 16 bpp only. The protocol has no other pixel format.
- One panel at a time.
- `COMMAND_RESET` is deliberately not used by the sketch: it reboots the panel,
  which then re-enumerates on USB.

## Tests

The protocol layer is covered by a host unit test, since a packing bug is only
visible as a corrupted image on real hardware:

```sh
cd tests
uv run --env-file .env pytest unit/turing -v -s
```

Real-hardware bring-up (orientation, solid fills, color bars, checkerboard,
partial rectangles, the band sweep above, image persistence, brightness):

```sh
uv run --env-file .env pytest manual/usb_display_turing/usb_display_turing.py -v -s
```

## Protocol references

The implementation was written from scratch from these MIT-licensed sources:

- [gerph/turing-smart-screen-python-mit](https://github.com/gerph/turing-smart-screen-python-mit)
  — a clean-room reimplementation for these panels; the source for the 6-byte
  command form, the rectangle packing and the RGB565 little-endian pixel order
- [Kwonsunuk/turing-lcd-monitor](https://github.com/Kwonsunuk/turing-lcd-monitor)
  — names `1A86:5722` / `USB35INCHIPSV2` explicitly, so it is the closest match to
  this hardware; the source for `DISPLAY_BITMAP = 197` and the 115200 line coding

Everything in `TuringProtocol.hpp` was then verified against the real panel by
`tests/manual/usb_display_turing`.

Turing Smart Screen is a product name of its respective owner. This project is not
affiliated with, endorsed by, or certified by that vendor; the name is used here
only to identify the protocol these panels speak, because that is the name people
search for. The device itself reports the model string `USB35INCHIPSV2`, and many
of the panels this example drives are sold under other brands entirely.
