# EspUsbHostMacroPadN3

> 日本語版: [README.ja.md](README.ja.md)

Drive a Mirabox N3 / Ajazz AKP03 family LCD macro pad from an ESP32-P4: paint the
six key screens, set the backlight, and read the keys and encoders — with no vendor
software involved.

> **Status: working, verified on a STREONOR S6 (`1500:3006`).** Confirmed on the
> device: the interface and endpoint layout, the firmware-version read
> (`V3.S6.02.011`), brightness, clear and refresh, uploading a 64x64 JPEG to each of
> the six keys and seeing it on the panel, the key index order, the quarter-turn the
> panel applies to an uploaded image, and the codes of all twelve controls. The
> session handshake and the keepalive interval come from a USB capture of the vendor
> application driving the same unit. See [What is confirmed](#what-is-confirmed).

| File | Contents |
|---|---|
| `MacroPadN3Protocol.hpp` | The wire format: the `CRT` packet, the command letters, the input report offsets. No Arduino / USB dependencies |
| `MacroPadN3Device.hpp` | Find the vendor HID interface, send packets on its interrupt OUT endpoint, decode the reports arriving on the interrupt IN endpoint |
| `KeyImage.hpp` | Draw an RGB565 tile and encode it to JPEG with the ESP32-P4's JPEG peripheral |
| `EspUsbHostMacroPadN3.ino` | Connect, read the firmware version, paint the six keys, print input events |

## Hardware this is for

These pads are one white-label design sold under many brands, with the same
descriptors, the same product string (`HOTSPOTEKUSB HID DEMO`) and different
VID:PIDs:

| Brand / model | VID:PID |
|---|---|
| STREONOR S6 | `1500:3006` (this example's test device) |
| Mirabox Stream Dock N3 | `6602:1000`, `6602:1002`, `6603:1002`, `6603:1003` |
| Ajazz AKP03 family | `0300:300x` |
| Unbranded / white label | `1500:3001` |

Identity is therefore a poor filter, and `MacroPadN3Device::begin()` matches on the
interface shape instead: a claimed HID interface that has both an interrupt IN and
an interrupt OUT endpoint of at least 1024 bytes. Pass a vid/pid to `begin()` to
pin a specific unit.

## ESP32-P4 only, and only on the high-speed port

The pad's vendor HID interface uses a **1024-byte interrupt OUT** endpoint. That is
legal for high-speed USB, but it exceeds what the ESP-IDF host driver can stage by
default, and no full-speed port can carry it at all:

```
Interface 0  vendor HID    EP 0x82 IN  interrupt  512 B / 1 ms
                           EP 0x03 OUT interrupt 1024 B / 1 ms   <- the problem
Interface 1  boot keyboard EP 0x81 IN  interrupt    8 B / 10 ms
```

Interrupt and isochronous OUT packets are staged in the controller's periodic TX
FIFO, and the driver's default split gives that FIFO 512 bytes. Claiming interface 0
then fails, and the host driver logs:

```
E HCD DWC: EP MPS (1024) exceeds supported limit (512)
E USBH: EP Alloc error: ESP_ERR_NOT_SUPPORTED
E USB HOST: Claiming interface error: ESP_ERR_NOT_SUPPORTED
```

The sketch repartitions the FIFO to fix it:

```cpp
EspUsbHostConfig cfg;
cfg.port = ESP_USB_HOST_PORT_HIGH_SPEED;
cfg.fifo = ESP_USB_HOST_FIFO_LARGE_PERIODIC_OUT;  // 1120 B for periodic OUT
usb.begin(cfg);
```

See the endpoint size limits section of the top-level [README](../../../README.md)
for the other splits. An ESP32-S3 or ESP32-S2 cannot run this example: their
full-speed FIFO is 1 kB in total, so 1024 bytes for one endpoint never fits, and a
full-speed link cannot transfer 1024-byte interrupt packets in the first place.

`PSRAM=enabled` is also required, because the P4's JPEG encoder allocates its
buffers from PSRAM. The profiles in `sketch.yaml` set both.

## The protocol

Every host→device packet is one 1024-byte interrupt OUT transfer:

```
"CRT" 00 00 | ASCII command | arguments | zero padding to 1024
```

The command is ASCII of no fixed length: most are three letters, but `CONNECT` is
seven and `QUCMD` five.

| Command | Arguments | Meaning |
|---|---|---|
| `DIS` | — | **Open the session.** The first thing the vendor application sends |
| `CONNECT` | — | **Keepalive.** The vendor application sends it every ~10 s |
| `LIG` | `00 00 <percent>` | Backlight brightness, 0..100 (the vendor application uses 25) |
| `QUCMD` | `1f 11 00 11 00 11` | Sent once at startup; purpose unknown, and the pad works without it |
| `CLE` | `00 00 00 <key>` | Clear one key's image; `0xff` clears all |
| `STP` | — | Show what was uploaded (the device does not redraw on its own) |
| `BAT` | `<size:4 BE> <key>` | A key image follows: `size` bytes of JPEG as raw 1024-byte packets |
| `LOG` | `<size:4 BE> 01` | The boot logo follows, as raw BGR888 of the panel's full size |

### The session is what makes the pad talk

Without `DIS` the pad is a standalone device: it acts on its own keys, paints its own
icons, and **sends no input reports at all**. `DIS` hands the screens to the host and
starts the reports. It also starts a timer — a session left quiet ends with the pad
dropping off the bus and re-enumerating about half a minute later — so `CONNECT` has
to keep arriving. The sketch sends it every 5 s, half the vendor application's rate.

This is the whole reason a first attempt at this device looks broken: uploads work,
the tiles appear, and then nothing is ever reported and the pad goes back to its own
icons the moment a key is touched.

The firmware version is not a packet but a class control transfer, `Get_Report`
(Input, id 0) on the vendor HID interface:

```cpp
usb.vendorControlTransfer(0xa1, 0x01, 0x0100, interfaceNumber, buffer, sizeof(buffer), &actual, address);
// -> "V3.S6.02.011"
```

The leading field is the protocol version, and it is worth reading first: version 3
is what this example implements (1024-byte packets, separate press and release
states). Version 1 devices — the Mirabox 293 and relatives — use 512-byte packets
instead, so `PACKET_SIZE` in `MacroPadN3Protocol.hpp` is what a port to those needs.

### Input reports

Reports arrive on the 512-byte interrupt IN endpoint, and only inside a session:

```
"ACK" 00 00 "OK" 00 00 | code | state | zero padding
        offset 9 --------^       ^-------- offset 10
```

| Code | State | Meaning |
|---|---|---|
| `0x01`..`0x06` | `01` / `00` | LCD key 1..6, pressed / released |
| `0x25`, `0x30`, `0x31` | `01` / `00` | Scene key 1..3, pressed / released |
| `0x90` / `0x91` | `00` | Encoder 1 turned, counter-clockwise / clockwise |
| `0x60` / `0x61` | `00` | Encoder 2 turned |
| `0x50` / `0x51` | `00` | Encoder 3 turned |
| `0x33`, `0x34`, `0x35` | `01` / `00` | Encoder 1..3 pushed, pressed / released |

Every code above was produced by working through all twelve controls of a STREONOR S6
in a known order. The codes are unrelated arithmetically, so `MacroPadN3Protocol.hpp`
holds them as a table.

The encoder numbering matches the pad's own: encoder 1 is the bottom-left knob, 2 the
bottom-right, 3 the top one. Another brand's unit may seat the same codes at different
positions, so a sketch that cares about position should check its own.

### Use `onHIDInput()`, not `onHIDVendorInput()`

The library's vendor HID path only fires for a report whose first byte is its vendor
report ID. These reports start with `"ACK"` instead, so `onHIDVendorInput()` never
fires for this device and `onHIDInput()` - which sees every HID report - is the
callback to use, filtered by device address and interface number. This is the same
shape [`EspUsbHostDp100Power`](../EspUsbHostDp100Power/) needs, for the same reason.

### Key images

Key indices for `BAT` and `CLE` run 1..6 in reading order:

```
1 2 3
4 5 6
```

The panel displays an uploaded tile turned a quarter turn, so a bar drawn across the
tile comes out running up the key. `KeyImage.hpp` compensates in `drawPixel()` -
`KEY_IMAGE_ROTATION` is 90 for this device - which keeps sketch coordinates upright
with x to the right and y down. The value is per-device and can only be checked by
looking at the panel; the test pattern this example paints is deliberately
asymmetric so one look is enough.

The image itself is 64x64 JPEG. Nothing in the protocol carries the dimensions - the
`BAT` header is only a byte count - so a wrong size shows up as a scaled picture
rather than as an error.

### Key images and who owns them

Outside a session the pad draws its own stored icon over a key that is pressed, which
is what makes an uploaded tile disappear - the single most confusing symptom of a
missing session, because uploading works perfectly and the picture still goes away.

Inside a session the tiles survive being pressed, so this example uploads once and
leaves them. The vendor application does not rely on that: in the capture it re-uploads
and refreshes several times a second regardless. A sketch that wants to be defensive
about a firmware that does overwrite a key can repaint it from the input callback, one
upload of about 8 ms.

### Going quiet ends the session

A session with no packets arriving ends with the pad detaching from the bus and coming
back a few seconds later at a new address (measured: 24 s and 36 s after a lone `DIS`
in two runs). That is what the keepalive prevents, and it is worth knowing because it
looks exactly like a hardware fault.

## What is confirmed

Measured on a STREONOR S6 (`1500:3006`, firmware `V3.S6.02.011`) with an ESP32-P4
on its high-speed port:

```
connected address=1 vid=1500 pid=3006 product="HOTSPOTEKUSB HID DEMO"
pad ready address=1 interface=0
firmware="V3.S6.02.011"
key 1: 1438 byte JPEG sent
key 2: 1479 byte JPEG sent
key 3: 1318 byte JPEG sent
key 4: 1360 byte JPEG sent
key 5: 1363 byte JPEG sent
key 6: 1428 byte JPEG sent
```

- All six tiles appear on the panel, upright, with the frame and the counting bar
  where the sketch drew them. That fixes 64x64 as the right size, 90 as the right
  `KEY_IMAGE_ROTATION`, and the key order as 1..3 then 4..6 in reading order.
- In a session the tiles survive a key press, and every control reports:
  ```
  key 1 down        raw=41434b00004f4b000001010000000000
  key 1 up          raw=41434b00004f4b000001000000000000
  scene key 1 down  raw=41434b00004f4b000025010000000000
  encoder 1 turn +1 raw=41434b00004f4b000091000000000000
  encoder 1 down    raw=41434b00004f4b000033010000000000
  ```
- The device stays on the bus through brightness, clear, six image uploads and
  refresh, with no transfer errors.
- Idle with the interface claimed and nothing sent: stable for 110 s.
- The `450 mA` this device asks for in its configuration descriptor is more than a
  bus-powered hub port budget. Give the host port a supply that can hold 500 mA or
  more, especially with the backlight up.

### What the vendor application does

The session handshake and the keepalive interval come from a USB capture of the vendor
application driving the same unit, over 40 s and 55k USB frames. Its startup, with
capture-relative timestamps:

```
282.887  CRT..DIS                         session start
282.888  CRT..LIG 00 00 19                brightness 0x19 = 25
282.889  CRT..QUCMD 1f 11 00 11 00 11     purpose unknown
282.945  CRT..CLE 00 00 00 ff             clear every key
282.953  CRT..BAT 00 00 10 7f 01          key 1, 4223 bytes of JPEG
282.958  CRT..BAT 00 00 0e 81 02          key 2, 3713 bytes
   ...   four more                        keys 3..6
282.979  CRT..STP                         show it
```

Then, with the pad idle:

```
293.458  CRT..CONNECT
302.879  CRT..CONNECT     9.42 s
312.914  CRT..CONNECT    10.03 s
322.879  CRT..CONNECT     9.97 s
```

Two more things the traffic shows. The application does not rely on the pad to hold an
image: over those 40 s it sent 3151 `BAT` uploads and 3131 `STP` refreshes, several a
second, with key image sizes between 2110 and 5025 bytes. And it uploads to seven image
targets, not six - key ids 1..6 plus `0x0b`, which it writes as often as keys 5 and 6.

Still open:

- What `QUCMD` asks for, and what `BAT` key id `0x0b` targets - the vendor application
  uploads to it as often as to keys 5 and 6, so the panel has an image target beyond
  the six keys.
- The boot logo: `LOG` is implemented from the protocol but the panel's full size is
  not known, so `setBootImage()` needs the caller to supply a correctly sized buffer
  and has not been tried.

## References

The protocol description here comes from public reverse-engineering work. Only
permissively licensed material was used as a source:

- [rigor789/mirabox-streamdock-node](https://github.com/rigor789/mirabox-streamdock-node)
  (MIT) — the `CRT` packet layout and the command letters, from a MiraBox 293
  (protocol version 1).
- [4ndv/mirajazz](https://github.com/4ndv/mirajazz) — its README documents which
  device belongs to which protocol version, and that version 2/3 devices use
  1024-byte packets. The library itself is MPL-2.0 and its code was not used.
- [4ndv/opendeck-akp03](https://github.com/4ndv/opendeck-akp03) — an OpenDeck plugin
  for this device family, listed here as the place to look for device coverage. It
  is GPL-3.0 and its code was not used.

Everything else above was measured on the device with `tests/manual/device_dump` and
the sketch in this directory.

Mirabox, Stream Dock, Ajazz and STREONOR are trademarks of their respective owners.
This project is not affiliated with, endorsed by, or certified by any of them.
