# USB displays with EspUsbHost

> 日本語版: [usb-display.ja.md](usb-display.ja.md)

USB displays do not form a device class. Each one is a different protocol over a
different transport, so nothing display-specific lives in the library: each is an
example built on a generic transport API, and this page is the index of them.

Examples are filed by **transport**, not by function — a DL-1xx adapter is a
vendor-class bulk device and sits under `examples/Vendor/`, a smart screen is a
CDC serial device and sits under `examples/Serial/`. That keeps each example next
to the other users of the same library API. This page is what ties them together
by function.

## Examples

| Example | Hardware | Transport | Resolution | Library API used |
|---|---|---|---|---|
| [`EspUsbHostDisplayDl1xx`](../examples/Vendor/EspUsbHostDisplayDl1xx/) | USB graphics adapter with a DisplayLink DL-1xx chip (`17e9:*`) | Vendor class, bulk OUT | up to 1920x1080 | `vendorOpen()`, `vendorControl*()`, `vendorWriteQueue*()` |
| [`EspUsbHostDisplayTuring`](../examples/Serial/EspUsbHostDisplayTuring/) | 3.5-inch USB smart screen — Turing Smart Screen revision A and the compatibles that report `USB35INCHIPSV2` (`1a86:5722`) | CDC ACM serial | 320x480 | `setSerialBaudRate()`, `serialWriteQueue*()` |
| [`EspUsbHostDisplayAx206`](../examples/Vendor/EspUsbHostDisplayAx206/) | AX206 USB photo frame (`1908:0102`) | Bulk-Only Transport over bulk, class `0xdc` | 480x320 | `vendorOpen()` by number + on-demand reads, `vendorReadSync()`, `vendorWriteQueue*()` |

All three expose the display as a `lgfx::Panel_Device` subclass and are meant to
be used with
[LGFXVirtualCanvas](https://github.com/tanakamasayuki/LGFXVirtualCanvas), which
renders the screen as horizontal bands through one small sprite. That keeps the
host free of a full-size frame buffer and, on the first two, lets diff transfer
skip the bands that did not change.

## Choosing one

**Full HD, or anything with a lot of pixels — the DL-1xx.** It is the only one of
the three that is not transfer-bound: RLE gets 5-20x on typical UI content, so the
bus sits at a few percent and the draw callback is what costs. Nothing else here
reaches those resolutions. In exchange it is the most complex device — mode
setting, timing registers, EDID.

**Otherwise, the 3.5-inch smart screen (Turing and compatibles).** This is the
default recommendation. It is the cheapest and most widely sold of the three, it
takes partial updates addressed as 2D rectangles, and so diff transfer works: only
the bands that changed cost anything. It is CDC serial, which makes it the
simplest to talk to.

**The AX206 photo frame — only if that is the hardware you have.** It works, but
whole-screen blits are all it accepts. Every frame is 307,200 bytes however little
moved, which is 0.53 s on a full-speed host: a flat 2 fps that no amount of
drawing cleverness improves, and diff transfer cannot be used at all. Fine for a
status panel that changes slowly; not for anything animated.

## What they have in common

All three devices keep their own frame buffer and keep showing it with no USB
traffic at all, so no panel implementation here holds a frame buffer of its own: a
drawing operation is turned straight into wire commands. None supports read-back,
so `readRect()`, ARGB blending and `copyRect()` are unavailable in all three.

All three stream through an asynchronous write queue rather than a synchronous
write.
The reason is the same in both cases: a display writer outruns a full-speed
endpoint, and without a bounded pool of preallocated transfers there is nothing to
push back on it. The two queues have the same shape — `…WriteQueueBegin()`,
`…WriteAcquire()` / `…WriteSubmit()` for the zero-copy path, `…WriteFlush()`, and
an `EspUsbHostWriteQueueStats` snapshot.

## Where they differ

|  | DL-1xx | 3.5-inch smart screen | AX206 |
|---|---|---|---|
| Addressing | linear byte address into the device frame buffer | 2D rectangle, inclusive on both ends | whole screen only |
| Partial update | yes | yes | no |
| Compression | RLE, 5-20x on typical UI content | none; 2 bytes per pixel always | none; 2 bytes per pixel always |
| Pixel order | big-endian RGB565 (`rgb565_2Byte`) | little-endian RGB565 (`rgb565_nonswapped`) | big-endian RGB565 (`rgb565_2Byte`) |
| Rotation | not available | done by the panel, via `setOrientation()` | not available |
| Mode setting | timing registers, EDID readable | fixed panel, orientation only | fixed panel |
| Status phase | none | none | every command ends with one read back |
| Diff transfer | worth it | worth it | unusable |
| What limits it | the per-band draw callback | USB transfer | USB transfer |

That last row is the practical one, and it flips the tuning advice. On the DL-1xx
adapter the bus sits at a few percent while the draw callback is the cost, so
fewer and larger bands wins. On the smart screen every pixel costs 2 bytes on the
wire and the panel paces the link at a flat 0.155 MB/s, so sending fewer pixels is
the only thing that helps at all; how an update is split into rectangles does not
change its cost. On the AX206 not even that helps, because the number of pixels
sent is not yours to choose. Each example's README has the measured sweep.

The smart screen also has two undocumented framing requirements that the DL-1xx
does not: a command is discarded if it arrives before the previous rectangle's
pixels have landed, and a command must occupy a USB transfer of its own. Both fail
silently — no error, no lost transfer — so its example README describes them and
its manual test guards against both.

## Adding another display

The library side is deliberately generic. If the device is a vendor-class bulk
device, `vendorWriteQueue*()` is already there; if it is CDC serial,
`serialWriteQueue*()` is. Put the new example under the directory for its
transport, keep the protocol layer free of Arduino / LovyanGFX / USB dependencies
so it can be unit tested with g++ on the host (see `tests/unit/dl1xx`,
`tests/unit/turing` and `tests/unit/ax206`), and add a row to the table above.

If you need a maintained display stack rather than a protocol example — more
adapter families, higher frame rates — use
[Pico_USB_Disp](https://github.com/htlabnet/Pico_USB_Disp) instead.

## Design notes

- [`usb-display-spec.md`](usb-display-spec.md) — the DL-1xx protocol in English:
  sources and licensing, the measured USB layout, the control requests, the `0xAF`
  bulk commands, the video registers and their LFSR16 encoding, the RLE format,
  display persistence, and the Full HD feasibility arithmetic.
- [`usb-display-spec.ja.md`](usb-display-spec.ja.md) — the same document in Japanese,
  plus the development record: test plan, phase breakdown and open items.
