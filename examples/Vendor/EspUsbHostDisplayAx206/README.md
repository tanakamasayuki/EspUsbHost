# EspUsbHostDisplayAx206

> 日本語版: [README.ja.md](README.ja.md)

Drive an AX206 USB photo-frame display (480x320, VID `0x1908` PID `0x0102`) as a
LovyanGFX panel.

> **Status: working, verified on an ESP32-S3 at 2 fps.** What the device expects
> was settled by a bus capture taken from a working Windows host; the bytes are in
> [Protocol, from a bus capture](#protocol-from-a-bus-capture) and the numbers in
> [What was measured](#what-was-measured).

| File | Contents |
|---|---|
| `Ax206Protocol.hpp` | The wire format: the 31-byte Command Block Wrapper, the 16-byte vendor command blocks, the blit rectangle, the status wrapper, RGB565 pixels. No Arduino / LovyanGFX / USB dependencies |
| `Ax206Device.hpp` | Claim the interface, run each command as a Bulk-Only Transport transaction, stream a frame's pixels through the asynchronous bulk OUT queue |
| `Panel_Ax206.hpp` | `lgfx::Panel_Device` subclass plus the `LGFX_Ax206` device |

## How it works

The device speaks USB mass-storage Bulk-Only Transport, but with vendor commands
in the 16-byte command block instead of SCSI ones. Every operation is a
transaction: a 31-byte wrapper carrying the command and the length of what
follows, then the data phase, then a 13-byte status wrapper read back on the bulk
IN endpoint.

The unit of drawing is a whole frame, because a whole frame is the only blit the
device accepts. So a frame is one transaction: it opens on `beginTransaction()`,
declaring all 307,200 bytes up front, and closes on `endTransaction()` once they
are all on the wire.

That is why this example is built around
[LGFXVirtualCanvas](https://github.com/tanakamasayuki/LGFXVirtualCanvas). It wraps
its whole tile loop in a single `startWrite()` / `endWrite()` pair and renders the
bands top to bottom — which is exactly the order the pixels have to leave in — so
a rendered screen becomes one USB transaction and **neither side ever holds a
frame buffer**. Pixels are streamed straight from each band into the bulk OUT
queue as they are drawn.

Streaming forwards is the whole contract. A caller may skip ahead, and the gap is
padded; it can never go back to a pixel already sent, and `seekTo()` turns such a
write into a dropped one it counts rather than a corrupted frame. Whatever the
caller leaves unwritten at the end is padded when the frame closes, because the
device waits for exactly the declared byte count and will not answer until it has
it. Code that needs arbitrary drawing order has to keep a frame buffer of its own.

Diff transfer is not usable here for the same reason: the data phase has to carry
all 307,200 bytes whatever changed, so a skipped band would leave a hole in the
stream rather than save anything.

The panel's color depth is fixed at `rgb565_2Byte`, whose memory layout is
big-endian RGB565 — exactly the wire format, so converted pixels reach USB with
no byte swapping. There is no compression: every pixel costs 2 bytes, so a frame
is 307,200 bytes regardless of content, which is what fixes the rate at 2 fps on
a full-speed host.

## Driving it from LGFXVirtualCanvas

Two rules, both consequences of the device taking whole-screen blits only:

**Do not enable tile diff transfer.** `LGFXVirtualDiffMode::Tile` skips bands that
did not change, and a skipped band is a hole in a data phase that has to carry all
307,200 bytes. It would not save a byte on the wire either, since the frame is
sent whole regardless. Leave the mode at its default, `Off`.

**Let one `render()` cover the whole screen.** The frame is opened by
`startWrite()` and closed by `endWrite()`, and `render()` puts exactly one of those
pairs around its tile loop — so one call is one frame. Rendering half the screen in
one call and half in another produces two frames, each with the other half padded.

Memory permitting, the drawing can be done in one pass instead of band by band:
give the canvas a budget large enough for the whole surface and it renders as a
single tile, with no per-band overhead. 307,200 bytes is more than an ESP32-S3 has
free internally, so that means PSRAM:

```cpp
screen.setUsePsram(true);              // LGFXVirtualCanvas 1.4.0 and later
screen.setMemoryLimit(480 * 320 * 2);  // 307,200 bytes: one tile, one pass
...
screen.tileIsPsram();                  // false if it quietly fell back
```

The allocation falls back to internal RAM without complaint when PSRAM is absent
or full, so `tileIsPsram()` after `begin()` is how you find out what you got.

None of this moves the frame rate. At 307,200 bytes per frame over full-speed USB
the wire takes 0.53 s and everything else is noise: the rate is 2 fps whether the
screen is drawn in one tile or twenty. LGFXVirtualCanvas says the same thing about
PSRAM tiles in general — they win when the draw callback is the bottleneck and lose
when the transfer is, and a PSRAM-backed sprite is pushed without DMA. Here the
transfer is the bottleneck by a wide margin. Choose the band size for RAM, not for
speed.

## Two library extensions this needed

Neither was expressible with the existing API, and both are now part of the
library.

**Claiming an interface that is not vendor-class.** This device declares class
`0xdc` / subclass `0xa0` / protocol `0xb0`, which means nothing in particular and
is not something any driver claims. `vendorOpen()` used to accept only class
`0xff`; naming an interface explicitly now claims it whatever its class, while
automatic selection stays restricted to vendor-specific interfaces so it can
never wander into one another part of the library drives.

**Reading on demand instead of continuously.** `vendorOpen()` used to start a
permanently outstanding IN transfer, which is right for a device that streams
unprompted and wrong for a transactional one: a Bulk-Only Transport device
answers only inside a transaction, and polling it outside one is a transfer
error. Opening with `ESP_USB_HOST_VENDOR_READ_ON_DEMAND` leaves the endpoint idle
until `vendorReadSync()` asks:

```cpp
usb.vendorOpen(address, ax206::INTERFACE_NUMBER, ESP_USB_HOST_VENDOR_READ_ON_DEMAND);
...
usb.vendorReadSync(packet, sizeof(packet), &length, timeoutMs, address);
```

Both are covered by the `tests/peer/usb_vendor` peer test, and both are exercised
by this display: on-demand reads are how the status wrapper is collected at the
end of every transaction.

## Protocol, from a bus capture

Everything below was read off a capture of a working Windows host driving this
display, and then replayed byte for byte on an ESP32-S3. The device speaks three
commands and nothing else.

**INIT — and its data phase is an IN.** The wrapper declares five bytes with
`bmCBWFlags = 0x00`, which by the specification means host to device. The device
ignores that and *sends* five bytes instead, and the host is expected to read
them:

```
OUT 31   55534243 deadbeef 05000000 00 00 10  cd 00 00 00 00 02 00 00 00 00 00 00 00 00 00 00
IN   5   e0 01 40 01 ff          <- 0x01e0 = 480, 0x0140 = 320, then one more byte
IN  13   55534253 deadbeef 00000000 00        <- status wrapper, passed
```

Getting this direction wrong is not a small mistake: the device is left holding
five bytes it wants to send, so every later read is made against a device that is
out of step, and the host controller reports the mess as babble
(`USB_TRANSFER_STATUS_OVERFLOW`). That single misreading is what made this example
look unworkable for as long as it did.

**BLIT — always the whole screen.** All nine blits in the capture address
`(0, 0)-(479, 319)` and carry 307,200 bytes. None of them is a partial rectangle,
which agrees with SimHub needing its "Disable partial draws" option for these
panels.

```
OUT 31       55534243 deadbeef 00b00400 00 00 10  cd 00 00 00 00 06 12 00 00 00 00 df 01 3f 01 00
OUT 307,200  pixels, RGB565 big-endian
IN  13       status wrapper, passed
```

**Brightness — no data phase.** Sent once the first frame is up. Byte 6 is the
command, and `01` / `07` read as property 1 (brightness) set to 7, the maximum.
The trailing `df 01 3f 01` is the same panel extent the blit command carries.

```
OUT 31   55534243 deadbeef 00000000 00 00 10  cd 00 00 00 00 06 01 01 00 07 00 df 01 3f 01 00
IN  13   status wrapper, passed
```

The tag is the constant `0xefbeadde` on every command; the host never varies it.
The capture splits the 307,200-byte data phase into 262,144 + 45,056 bytes, which
is a Windows URB size limit and means nothing to the device.

## What was measured

Replaying the captured sequence on an ESP32-S3 (full-speed DWC core), against the
real device:

| | Result |
|---|---|
| Interface claim by number (class `0xdc`) | works |
| INIT, reading the five-byte data phase | works, returns `e0 01 40 01 ff` |
| Every status wrapper | read back, `bCSWStatus = 0` |
| Full-screen blit, 307,200 bytes plus status | works, **0.57 MB/s**, 0.534 s per frame |
| Device state across repeated frames | stays enumerated |

The endpoint descriptors are exactly what the specification requires of a
full-speed device, and the ESP32 reads the same values Windows does: bulk IN
`0x81` and bulk OUT `0x01`, both `wMaxPacketSize = 64`, `bcdUSB = 0x0110`. There
was never anything wrong with the device's packet sizes.

## Tests

The protocol layer is covered by a host unit test:

```sh
cd tests
uv run --env-file .env pytest unit/ax206 -v -s
```

The two library extensions are covered on the peer rig:

```sh
uv run --env-file .env pytest peer/usb_vendor -v -s
```

There is no manual test for this example yet.

## Protocol references

The implementation was written from scratch from this MIT-licensed source:

- [sayajinpt/pyax206](https://github.com/sayajinpt/pyax206) — targets this exact
  device (`1908:0102`, 3.5-inch 480x320 "USB-Display"); the source for the
  CBW/CSW framing, the two 16-byte command blocks, the endpoint addresses and the
  RGB565 big-endian pixel order

[oae/sensorpanel](https://github.com/oae/sensorpanel) (MIT) was used to
cross-check the pixel format for VID `0x1908` panels.

It contains no code from the GPL-licensed `dpf-ax` or `lcd4linux` implementations.
Both are historically important for this hardware and neither was copied from.
