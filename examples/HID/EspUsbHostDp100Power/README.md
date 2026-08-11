# EspUsbHostDp100Power

日本語: [README.ja.md](README.ja.md)

Read an ALIENTEK DP100 digital power supply (`ATK-MDP100`, `2e3c:af01`) over USB:
device identity, input rail, output voltage and current, and temperatures.

> **Status: reading works, verified on an ESP32-S3.** Every field below was
> measured against the real instrument by `tests/probe/dp100` and is checked by
> `tests/manual/dp100`; the capture is in [What was measured](#what-was-measured).
> **The setpoint frame is implemented but not verified** — see
> [The write side](#the-write-side-unverified).

| File | Contents |
|---|---|
| `Dp100Protocol.hpp` | The wire format: the 64-byte report frame, CRC-16/MODBUS, the read payloads. No Arduino / USB dependencies |
| `Dp100Device.hpp` | Find the HID interface, send requests on the interrupt OUT endpoint, pair them with the reports arriving on the interrupt IN endpoint |
| `Dp100Power.hpp` | The meaning layer: volts, amps, degrees |
| `EspUsbHostDp100Power.ino` | Connect, print the device info, then poll measurements |

## Why this is under `examples/HID/`

The DP100 is a plain HID device — interface class `0x03`, subclass `0x00`,
protocol `0x00`, with a 64-byte interrupt IN and a 64-byte interrupt OUT. Its own
protocol rides inside those reports.

The `examples/` directories are organised by *which library API an example
drives*, not by what the device does. This one uses the HID API, so it is in
`HID/`. The other power supply example, [`EspUsbHostUsbtmcScpi`](../../Vendor/EspUsbHostUsbtmcScpi/),
drives USBTMC through the vendor bulk API and therefore sits in `Vendor/` —
same kind of instrument, different directory, because the axis is the API.

## How it works

### The library side: no additions needed

```cpp
// Requests: raw bytes to the HID interrupt OUT endpoint, no report ID prepended.
usb.sendHIDVendorOutput(report, 64, address);

// Answers: onHIDInput() gets every HID IN report.
usb.onHIDInput([](const EspUsbHostHIDInput &input) { /* latch input.data */ });
```

`onHIDInput()` is the callback to use, **not** `onHIDVendorInput()`. The vendor
callback only fires when the report's first byte matches a specific report ID, and
a DP100 frame starts with its own direction marker (`0xfa`), so it would never
arrive. `onHIDInput()` is called before that dispatch and receives everything.

Two consequences shape `Dp100Device.hpp`:

- `onHIDInput()` runs on the USB task, so the callback only copies the report and
  sets a flag. Waiting happens on the caller's task.
- `sendHIDVendorOutput()` does not wait for completion, so pairing a request with
  its answer is the example's job: clear the latch, send, then wait for a report
  that decodes *and* carries the opcode that was asked for. Frames for another
  opcode are dropped rather than returned, or the pairing would stay one answer
  behind forever.

### The frame

```
[dir][opcode][reserved][len][data ... ][crc lo][crc hi]   zero padded to 64 bytes
```

- `dir` is `0xfb` host→device and `0xfa` device→host
- `crc` is **CRC-16/MODBUS** over byte 0 through the last data byte, appended
  little endian
- a read request carries no data, so `len` is 0

A `DEVICE_INFO` request is therefore:

```
fb 10 00 00 <crc lo> <crc hi> 00 00 ... 00
```

**A rejected frame is answered, not ignored:** the opcode is echoed with a
one-byte body of `0x00`. That was established by sending the same request with
three wrong CRC variants — all three came back as `fa 10 00 01 00`. `isRefusal()`
recognises it so a refusal is never read as a payload.

### Opcodes

| Code | Name | Used here |
|---|---|---|
| 0x10 | DEVICE_INFO | yes |
| 0x30 | BASIC_INFO | yes |
| 0x35 | BASIC_SET | implemented, unverified |
| 0x40 | SYSTEM_INFO | yes, as raw bytes |
| 0x45 | SYSTEM_SET | no |
| 0x50 / 0x55 | SCAN_OUT / SERIAL_OUT | no |
| 0x12–0x15 | firmware update | deliberately not implemented |

### Payloads

`DEVICE_INFO` answers 40 bytes:

| offset | size | field |
|---|---|---|
| 0 | 16 | device type, `0xff` padded — `"ATK-DP100"`, which is **not** the USB product string `"ATK-MDP100"` |
| 16 | 2 | hardware version |
| 18 | 2 | application version |
| 20 | 2 | boot version |
| 22 | 2 | run area |
| 24 | 12 | serial bytes — **not** the USB serial string either |
| 36 | 2 | year |
| 38 | 1 | month |
| 39 | 1 | day |

`BASIC_INFO` answers 16 bytes, all little-endian 16-bit:

| offset | field | unit |
|---|---|---|
| 0 | input voltage | mV |
| 2 | output voltage | mV |
| 4 | output current | mA |
| 6 | max output voltage the present input allows | mV |
| 8 | temperature 1 | 0.1 °C |
| 10 | temperature 2 | 0.1 °C |
| 12 | internal 5 V rail | mV |
| 14 | output mode (1 byte), work status (1 byte) | |

The units were settled by measurement rather than assumed: the input read 12160
against a ~12.16 V supply, the internal rail 5067, and the two temperatures 298 and
292 in a room at about 29 °C. `max output voltage` is the ceiling the *present*
input supports — a 12 V input cannot make 20 V out — so it is the value to check a
setpoint against, not the model's 30 V rating.

`SYSTEM_INFO` answers 8 bytes (`50 00 1a 04 02 02 01 00` here). The field meanings
are not established, so they are exposed as raw bytes rather than guessed at.

## What was measured

`tests/manual/dp100` on an ESP32-S3 with the DP100 connected directly:

```
connected address=1 vid=2e3c pid=af01 product="ATK-MDP100" serial="16A1C1C74000"
dp100 interface address=1 interface=0
device type="ATK-DP100" hw=14 app=14 boot=11 run_area=0x00aa built=2024-12-02 serial=c7819d000040041622a75005
basic in=12.160V out=0.000V 0.000A max_out=11.800V rail5v=5.067V temp=29.6/29.0C mode=2 status=0
system info raw=50001a0402020100 len=8
repeated reads done refusals=0 received=53
interleaved reads done
```

50 repeated reads and 5 rounds of interleaved `DEVICE_INFO` / `BASIC_INFO` ran with
no refusals and no mispaired frames.

Descriptors, for reference (`tests/manual/device_dump`):

```
VID:PID 2e3c:af01 class=0x00(per-interface)
Strings manufacturer="ALIENTEK" product="ATK-MDP100" serial="16A1C1C74000"
  Interface 0 alt=0 class=0x03(HID) subclass=0x00 protocol=0x00 endpoints=2 claimed=yes
    Endpoint iface=0 ep=0x81 dir=IN  type=interrupt max_packet=64 interval=1
    Endpoint iface=0 ep=0x01 dir=OUT type=interrupt max_packet=64 interval=1
```

## The write side (unverified)

`BASIC_SET (0x35)` carries the voltage and current setpoints, the protection
thresholds **and the output enable, in one frame**. Probing it blind can switch a
bench supply's output on, so `tests/probe/dp100` stops short of it and
`tests/manual/dp100` never sends it. The layout in `Dp100Protocol.hpp` is what the
public reverse-engineering projects describe; unlike everything above, it is not
confirmed by this project's own measurement.

In the sketch it sits behind `APPLY_SETPOINT`, which is `false`. Before enabling
it: disconnect whatever is on the output terminals, keep the values low, and watch
the front panel after each call. A failure means the request form is not
confirmed, not that the supply is broken.

## Retargeting

`Dp100Protocol.hpp` and `Dp100Device.hpp` are a general "framed protocol inside
64-byte HID reports" pair — the shape a great many instruments and gadgets use. For
a different device, the frame header and CRC in the protocol header are what
change; the request/response pairing in the device layer stays as it is.

## References

No first-party protocol document is published for the DP100 — ALIENTEK ships a
Windows DLL (`ATK-DP100DLL`) and nothing else — so **this implementation's
reference is the device itself**, captured by `tests/probe/dp100`. The public
projects below were used to know what to look for and to cross-check the field
order.

| Source | License | Use |
|---|---|---|
| [ElluIFX/DP100-PyQt5-GUI](https://github.com/ElluIFX/DP100-PyQt5-GUI) | Unlicense | consulted |
| [scottbez1/webdp100](https://github.com/scottbez1/webdp100) | Apache-2.0 | consulted |
| [lessu/open_dp100](https://github.com/lessu/open_dp100) | no LICENSE file | cross-checked against, nothing copied |
| [weigu1/dp100_manipulator](https://github.com/weigu1/dp100_manipulator) | GPL-3.0 | **not consulted** |
| ALIENTEK's Windows DLL | proprietary | **not reverse engineered here** |

This library is MIT. No GPL-licensed code was consulted, and the vendor DLL was
not disassembled.

> ALIENTEK and DP100 are trademarks of their respective owner. This project is not
> affiliated with, endorsed by, or certified by ALIENTEK.
