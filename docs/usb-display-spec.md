# DL-1xx USB display protocol notes

> 日本語版: [usb-display-spec.ja.md](usb-display-spec.ja.md) — the full development record, including the test plan, the implementation phases and the open items. This English page carries the protocol findings from it.

What was worked out about the DisplayLink DL-1xx bulk protocol while writing [`examples/Vendor/EspUsbHostDisplayDl1xx`](../examples/Vendor/EspUsbHostDisplayDl1xx/). Nothing display-specific lives in the library: the example is built entirely on the vendor bulk/control API.

For the other USB display families (AX206 photo frame, CDC smart screen) and practical guidance on frame rates and tiling, start from [usb-display.md](usb-display.md).

## Sources and licensing

This library is MIT-licensed. **No code from GPL/LGPL drivers or from the commercial SDK was read or incorporated** — the implementation is written from scratch against public documentation and permissively licensed implementations only.

| Source | License | Used |
|--------|---------|------|
| Florian Echtler's reverse-engineering write-up | Public document | Yes — primary source |
| OpenBSD `sys/dev/usb/udl.c` | ISC | Yes — covers DL-120/125/160/165/195 |
| htlabnet/Pico_USB_Disp `docs/protocol.md` | MIT | Yes — includes findings verified on a real DL-165 |
| Linux `drivers/video/fbdev/udlfb.c` | GPL-2.0 | **No** |
| `libdlo` | LGPL-2.1 | **No** |
| DisplayLink Embedded SDK (Synaptics) | Commercial / NDA | **No** |

On naming: "DisplayLink" is a trademark of Synaptics Incorporated. Identifiers, file names and feature names use the part number (`DL-1xx` / `Dl1xx`); the brand appears only in prose that identifies compatible hardware. This project is not affiliated with, endorsed by, or certified by Synaptics. Linux (`udlfb`), OpenBSD (`udl`) and Pico_USB_Disp (`usb_disp_prot_dl-1xx.cpp`) all keep the brand out of code the same way.

## USB configuration

DL-1x0 ("Alex") and DL-1x5 ("Ollie") share one protocol. VID `0x17E9`.

Measured on a real DL-165 (output of [`tests/manual/vendor_bulk_out_only`](../tests/manual/vendor_bulk_out_only/)):

- `17e9:0360`, manufacturer `DisplayLink`, product `USB to DVI-17`
- One vendor-class (0xFF) interface with three endpoints:
  - bulk OUT `0x01`, MPS 64 — commands and the pixel stream
  - interrupt IN `0x82`, MPS 8, interval 4 — unused by this implementation
  - bulk OUT `0x0a`, MPS 64 — **a second bulk OUT**, purpose unknown, unused
- Bus-powered, `bMaxPower` 500 mA
- The same layout appears when it falls back to full speed, which is why it works on an ESP32-S3 (whose host port is always full speed)

Because there are two bulk OUT endpoints, endpoint selection matters: `vendorOpen()` takes the **first** bulk OUT in descriptor order (`0x01`). An earlier version took the last one and picked `0x0a`, which is silent failure — worth checking on any device with more than one endpoint per direction.

## Control requests

| bmRequestType | bRequest | wValue | wIndex | Data | Purpose |
|---------------|----------|--------|--------|------|---------|
| `0x40` | `0x12` | 0 | 0 | 16-byte key | Channel select (the standard key leaves encryption off) |
| `0xC0` | `0x02` | `i << 8` | `0xA1` | 2 bytes IN | EDID read, one byte at a time; the value is in `buf[1]` |

Standard channel key: `57 CD DC A7 1C 88 5E 15 60 FE C6 97 16 3D 47 F2`

Both go through the existing API unchanged — `vendorControlOut(0x12, 0, 0, key, 16)` and `vendorControlIn(0x02, i << 8, 0xA1, buf, 2)`.

The chip also carries a vendor-specific descriptor (type `0x5F`), which would report the maximum pixel count. It is **not read** by this implementation: fetching it takes a *standard* GET_DESCRIPTOR (`bmRequestType 0x80`), and none of the public sources document its layout, so the resolution is decided from the EDID instead.

## Bulk commands

Every command starts with `0xAF`.

| Command | Length | Meaning |
|---------|--------|---------|
| `AF 20 reg val` | 4 | Register write |
| `AF 6B addr[3] count data...` | variable | RLE-compressed pixel write (base16 plane, RGB565) |
| `AF 60 addr[3] count data...` | variable | Uncompressed write (base8 plane, 24 bpp only) |
| `AF 6A dst[3] count src[3]` | 9 | On-screen rectangle copy (base16) |
| `AF 62 dst[3] count src[3]` | 9 | On-screen rectangle copy (base8, 24 bpp only) |
| `AF A0` | 2 | Flush — force execution of buffered commands |
| `AF` repeated | — | Padding (no-op) |

Addresses are byte addresses into the device frame buffer. A `count` of 256 is encoded as `0`.

## Video registers

Register writes are bracketed by a lock held in register `0xFF`: write `0x00` to lock, set the registers, then write `0xFF` to unlock, which applies them.

Timing values are not raw numbers: they are **16-bit LFSR counts** (taps 15, 4, 2, 1, starting from `0xFFFF` and stepped N times).

| Register | Encoding | Contents |
|----------|----------|----------|
| `0x00` | raw | Colour depth (0 = 16 bpp, 1 = 24 bpp) |
| `0x01` / `0x03` | LFSR16 | Horizontal display start / end (from sync start, `xds = HBP + HSYNC`) |
| `0x05` / `0x07` | LFSR16 | Vertical display start / end |
| `0x09` | LFSR16 | Horizontal total − 1 |
| `0x0B` | LFSR16(1) | Horizontal sync start |
| `0x0D` | LFSR16 | Horizontal sync end (`HSYNC + 1`) |
| `0x0F` | raw BE | Horizontal pixel count |
| `0x11` | LFSR16 | Vertical total |
| `0x13` | LFSR16(0) | Vertical sync start |
| `0x15` | LFSR16 | Vertical sync end (`VSYNC`) |
| `0x17` | raw BE | Vertical line count |
| `0x1B` | raw LE | Pixel clock / 5 kHz |
| `0x1F` | raw | Blanking (`0x00` = display on) |
| `0x20`–`0x22` | 24-bit | base16 plane start address (0) |
| `0x26`–`0x28` | 24-bit | base8 plane start address (`width * height * 2`) |

On byte order: the public sources state one only for `0x0F` (raw BE) and `0x1B` (raw LE). The 16-bit LFSR values carry no note; this implementation writes them high byte first.

A complete mode set for one table mode measured **130 bytes** on the wire (128 bytes of register writes plus the 2-byte flush), about 2 ms at full speed. Resending that same sequence is also the recovery path after an HPD event (see below).

## The RLE encoding

`AF 6B` alternates raw runs (`raw_cnt` literal pixels) with repeat runs (how many additional copies of the previous pixel follow). Pixels are RGB565, big-endian. One command carries at most 256 pixels.

- 256 pixels of one colour: 10 bytes
- Worst case (every pixel different): 519 bytes — slightly *larger* than the 512 raw bytes

The one external oracle for the format is the worked example in the public write-up: 256 pixels of one colour at address 0 encode as `AF 6B 00 00 00 00 01 F8 00 FF` (one literal pixel, then 255 additional repeats). The encoder in `tests/unit/dl1xx` reproduces it byte for byte, which pins down the raw-run / repeat-run interpretation.

## Display persistence

Confirmed on a real DL-165:

- After the mode is set and pixels are drawn, **stopping all bulk traffic keeps the picture**: the chip keeps scanning out of its internal frame buffer. No keepalive is needed.
- However, an HPD-class event on the monitor side (unplugging the monitor, a capture device closing) drops the output and it stays black. Writing pixels does **not** bring it back; resending the mode register sequence restores it immediately.

## Is Full HD possible?

Yes for 1920x1080 at 16 bpp. Every protocol-level ceiling was checked:

| Item | Value | Verdict |
|------|-------|---------|
| `AF 6B addr[3]` — 24-bit byte address | 16 MB ceiling | OK |
| Frame buffer for 1920x1080x2 | 4,147,200 B = `0x3F4800` | OK (even the 24 bpp dual-plane case fits at `0x5EEC00`) |
| Register `0x1B` (pixel clock / 5 kHz, 16-bit) | 148.5 MHz → 29700 | OK (16 bits reaches ~327 MHz) |
| DL-1x5 internal DRAM | 16 MB | OK |
| DL-165 maximum resolution | Family ceiling 2048x1152; products implement 1920x1080 or 1600x1200 | OK, confirmed with a DL-165 on a Full HD monitor |
| DL-120 / DL-160 maximum | 1600x1200 / 1680x1050 | No Full HD — useful for lower-resolution testing |

Effective full-speed bulk OUT throughput was measured at **1.098 MB/s** (ESP32-S3 with a DL-165, [`tests/manual/vendor_bulk_throughput`](../tests/manual/vendor_bulk_throughput/)) — about 90 % of the 1.216 MB/s theoretical ceiling. Against that:

| Case | Bytes | Time |
|------|-------|------|
| Whole screen, one colour (RLE, 256 px = 10 B) | ~81 KB | 0.07 s |
| Typical whole-screen UI (RLE compresses 5–20x) | 0.2–0.8 MB | 0.2–0.7 s |
| Whole-screen photo or noise (519 B / 256 px) | 4.2 MB | ~3.8 s |
| Diff transfer touching one or two tiles | 10–20 KB | 0.02 s |

Compression ratio is the one variable that depends on content; [`tests/manual/usb_display_throughput`](../tests/manual/usb_display_throughput/) measures it per drawing pattern, and the results became the guidance in the example README.

On an ESP32-P4 the same adapter enumerates at **high speed**. Effective HS bulk OUT throughput measured **36.4 MB/s** (queue depth 2, 8 KB transfers) — about 68 % of the 53.2 MB/s theoretical ceiling (13 × 512-byte packets per microframe), and 33× the full-speed figure. Two timing findings carry over from the sweep: queue depth 2 already reaches the ceiling at both speeds, and per-transfer overhead weighs more at HS — 512-byte transfers stop around 8 MB/s even asynchronously.

The worst case has a hard arithmetic ceiling. An RLE-incompressible Full HD frame is 8,100 commands × 519 bytes = **4,203,900 bytes** (1.4 % overhead over raw), which is 0.274 fps at full speed — matched by measurement (0.27 fps at 99.8 % bus) — and a 9.08 fps ceiling at high speed, where the measured 1.55 fps shows the encoder and per-band redraw, not the bus, as the limit.

## Why tiles suit this device

The example drives the panel through LGFXVirtualCanvas (1.2.0 or later, for `setDiffMode(LGFXVirtualDiffMode::Tile)`), and the fit is not accidental:

- Tiles are **full-width horizontal bands**, so on the device frame buffer they map to a completely contiguous address range. `AF 6B` addresses bytes, so an RLE run can continue across row boundaries and the per-command header overhead is minimal — more efficient than updating an arbitrary rectangle.
- **No full frame buffer on the host.** At Full HD / 16 bpp with the default 19 KB budget, that is 5-row tiles (1920 × 5 × 2 = 19,200 B) × 216 tiles, doubled to ~38 KB when double buffering kicks in. It fits on an ESP32-S3 without PSRAM.
- Diff transfer matches "the picture persists with no traffic": a tile that did not change is simply never sent.
- The RLE encoder always copies from the tile buffer into the USB DMA buffer, so the tile buffer is reusable as soon as `writeImage()` returns. **No zero-copy path hands a tile buffer straight to DMA** — that invariant is what makes per-transfer double buffering safe.
- Reallocation, configuration changes, and panel rotation/size/depth changes invalidate the canvas automatically. USB reconnect and HPD recovery do not, so the panel keeps a generation counter and a ready callback, and the sketch calls `screen.invalidate()`.

## What this device asked of the library

Four gaps in the vendor API were found and closed while implementing it — a useful list of what a bulk-heavy device needs:

1. `vendorOpen()` required a bulk IN/OUT pair. The DL-1xx has bulk OUT plus interrupt IN and no bulk IN, so it could not be opened. Endpoint selection was fixed at the same time (descriptor order, first match).
2. `vendorWrite()` was fully synchronous — store-and-forward per transfer, which left full-speed bandwidth unused on small transfers (80 % of the ceiling at 512 bytes). The asynchronous queue was added.
3. Bulk OUT packet-boundary handling (ZLP) was the caller's problem; it became a library responsibility (`vendorSetAutoZlp()`).
4. There were no transfer statistics; `vendorWriteStats()` was added.

Control transfers needed no additions.

Relevant ESP-IDF behaviour behind those decisions (`usb_host.h`, `usb_types_stack.h`):

- Bulk OUT transfers larger than the MPS are split into MPS-sized packets plus a short packet automatically, so one 8–16 KB transfer is fine
- Transfer objects are reusable without limit; the API is designed around pooling them
- Several transfers can be queued on one endpoint
- Completion callbacks run in the context of `usb_host_client_handle_events()`, i.e. on the existing client task
- When a bulk/interrupt OUT transfer length is a multiple of the MPS, the host must send the ZLP itself
