# EspUsbHostUsbtmcScpi

> 日本語版: [README.ja.md](README.ja.md)

Talk SCPI to a USBTMC instrument: read its identity, set and read back values, and
take measurements. Verified against a KIKUSUI PMX18-5A DC power supply
(`0b3e:1029`).

> **Status: working, verified on an ESP32-S3.** The bytes below were confirmed
> against the real instrument by `tests/manual/usbtmc_scpi`; its output is in
> [What was measured](#what-was-measured).

| File | Contents |
|---|---|
| `UsbtmcProtocol.hpp` | The wire format: the 12-byte bulk message header, the 4-byte alignment rule, the bTag rules, the class request codes, the `GET_CAPABILITIES` payload. No Arduino / USB dependencies |
| `UsbtmcDevice.hpp` | Find the USBTMC interface, claim it through the vendor bulk API, run the class requests on EP0, exchange messages on the bulk endpoints. No SCPI, no VID/PID |
| `ScpiPmx.hpp` | The instrument-specific layer: SCPI commands for a PMX-series supply. Replace this file to target another instrument |
| `EspUsbHostUsbtmcScpi.ino` | Connect, print capabilities and `*IDN?`, apply settings, then poll measurements |

## Why this is under `examples/Vendor/`

**USBTMC is interface class `0xfe` (Application Specific), subclass `0x03` - not
the vendor-specific class `0xff`.**

The `examples/` directories are organised by *which library API an example
drives*, not by the device's USB class. `Vendor/` means "built on the vendor
bulk/control API", which is what this example does: the library has no USBTMC
support of its own. The same is true of its neighbours - `EspUsbHostDisplayAx206`
speaks a mass-storage-style Bulk-Only Transport, and `EspUsbHostDisplayTuring`
lives under `Serial/` because it drives a display over the CDC API.

Directories named after a class (`Ccid/`, `UsbNetwork/`, `Storage/`, `Audio/`)
exist only where the library has a dedicated API for that class.

## How it works

USBTMC is a message layer on two bulk endpoints, plus a set of class requests on
EP0. SCPI text is the payload; the class carries it and says nothing about it.

### The library side

Everything except one call already existed for vendor-specific devices:

```cpp
// The interface number is explicit, so a class 0xfe interface is claimed even
// though it is not vendor-specific. READ_ON_DEMAND is right for a strictly
// request/response protocol - a continuous IN transfer would sit there NAKing.
usb.vendorOpen(address, interfaceNumber, ESP_USB_HOST_VENDOR_READ_ON_DEMAND);
usb.vendorSetAutoZlp(true, address);   // terminate every OUT with a short packet
usb.vendorWrite(message, length, address);
usb.vendorReadSync(buffer, sizeof(buffer), &received, timeoutMs, address);
```

The instrument also has an interrupt IN endpoint (USB488 service requests). This
example does not open it; `*OPC?` polling covers what SRQ would be used for.

The one addition to the library is `vendorControlTransfer()`, which takes the
`bmRequestType` as a parameter. `vendorControlIn()` / `vendorControlOut()` send
`0xc0` / `0x40` - vendor type, device recipient - while every USBTMC class request
is `0xa1` to an *interface*:

```cpp
usb.vendorControlTransfer(0xa1, usbtmc::REQ_GET_CAPABILITIES, 0, interfaceNumber,
                          data, sizeof(data), &received, address);
```

### The bulk message header

Both directions start with 12 bytes, and every message is padded with zeros until
its total length is a multiple of 4:

| offset | contents |
|---|---|
| 0 | `MsgID`: 1 = DEV_DEP_MSG_OUT, 2 = REQUEST_DEV_DEP_MSG_IN / DEV_DEP_MSG_IN |
| 1-2 | `bTag` (1..255, never 0, never a repeat) and its bitwise complement |
| 4-7 | `TransferSize`, little endian |
| 8 | `bmTransferAttributes`: EOM on an OUT, TermCharEnabled on an IN request |
| 9 | `TermChar` on an IN request |

`*IDN?` therefore goes out as 20 bytes:

```
01 01 fe 00  05 00 00 00  01 00 00 00  2a 49 44 4e 3f 00 00 00
                                       *  I  D  N  ?  <padding>
```

A query is two messages: DEV_DEP_MSG_OUT with the command, then
REQUEST_DEV_DEP_MSG_IN followed by a bulk IN read. A response too large for the
requested chunk arrives with EOM clear and is continued by another request.

### The CLEAR sequence, and the step this example leaves out

`INITIATE_CLEAR`, then poll `CHECK_CLEAR_STATUS` until the device is done,
draining bulk IN while it reports data still queued.

USBTMC ends the sequence with `CLEAR_FEATURE(ENDPOINT_HALT)` on bulk OUT.
`UsbtmcDevice::clearOutHalt()` implements it but nothing calls it, because
clearing the halt resets the data toggle at the device end and this host cannot
resynchronise its own: the ESP-IDF host stack only resyncs a pipe that is actually
halted. Measured on a PMX18-5A - with that request in the sequence, the query
after a CLEAR times out; without it, CLEAR works and the next query answers. A
genuinely stalled endpoint is recovered by the library, which flushes and clears
the pipe when a transfer fails.

### GET_CAPABILITIES: mind the offsets

The USB488 capability bytes are at **14 and 15**, not 12 and 13 - `bcdUSB488`
occupies 12..13. Reading them two bytes early makes an instrument that supports
everything look like one that supports nothing, which is exactly what happened
here until the raw bytes were dumped. `tests/unit/usbtmc` pins the layout with the
PMX18-5A's real response.

## What was measured

`tests/manual/usbtmc_scpi` on an ESP32-S3, PMX18-5A behind a full-speed hub:

```
usbtmc interface address=2 interface=0 protocol=0x01
capabilities raw:01 00 00 01 00 01 00 00 00 00 00 00 00 01 07 0F 00 00 00 00 00 00 00 00
capabilities usbtmc=0100 usb488=0100 scpi=1 usb488.2=1 indicator=0 termchar=1
idn KIKUSUI,PMX18-5A,DR000046,IFC01.56.0015 IOC01.10.0070
setting readback 3.300V 0.250A
measured -0.004V 0.000A
output OFF
repeated queries done
error queue 0 "No error"
```

The device descriptor, for reference (`tests/manual/device_dump`):

```
VID:PID 0b3e:1029 class=0x00(per-interface)
Strings manufacturer="KIKUSUI" product="PMX18-5A" serial="DR000046"
  Interface 0 alt=0 class=0xfe subclass=0x03 protocol=0x01 endpoints=3
    Endpoint iface=0 ep=0x01 dir=OUT type=bulk      max_packet=64
    Endpoint iface=0 ep=0x82 dir=IN  type=bulk      max_packet=64
    Endpoint iface=0 ep=0x83 dir=IN  type=interrupt max_packet=16 interval=100
```

## SCPI commands used

IEEE 488.2 common commands and standard SCPI power-supply nodes, so most of these
work unchanged on other programmable supplies.

| Purpose | Command |
|---|---|
| Identity | `*IDN?` |
| Clear status | `*CLS` |
| Output voltage | `VOLT <v>` / `VOLT?` |
| Output current | `CURR <a>` / `CURR?` |
| Output on/off | `OUTP ON` / `OUTP OFF` / `OUTP?` |
| Measured values | `MEAS:VOLT?` / `MEAS:CURR?` |
| Error queue | `SYST:ERR?` |
| Operation complete | `*OPC?` |

`SYST:ERR?` returning `0,"No error"` is the instrument's own verdict that every
command it was sent was accepted - the check worth keeping when bringing up a new
model.

## Safety

The sketch changes the supply's output *settings*, and switches the output on only
if you set `TURN_OUTPUT_ON` to `true` (it is `false` by default). Run it with
nothing connected to the output terminals until you have seen what it does. The
manual test never enables the output.

## Retargeting to another instrument

Replace `ScpiPmx.hpp`. `UsbtmcProtocol.hpp` and `UsbtmcDevice.hpp` contain no
SCPI, no VID/PID and no model limits, so a DMM or an oscilloscope needs only its
own command set and, if it should be selected by identity rather than by being the
only USBTMC device present, its VID/PID passed to `UsbtmcDevice::begin()`.

Two things to check on a new instrument: whether it needs `TermChar` enabled on
IN requests (the PMX does not), and whether its responses exceed
`usbtmc::RESPONSE_CHUNK` per round, which is only a matter of how many rounds a
read takes.

## References

- USB-IF, *Universal Serial Bus Test and Measurement Class Specification (USBTMC)*, Revision 1.0
- USB-IF, *USBTMC USB488 Subclass Specification*, Revision 1.0
- IEEE 488.2 common commands; SCPI 1999 standard command set
- KIKUSUI PMX series communication interface manual

Implemented from these public specifications. No GPL-licensed USBTMC driver was
consulted.

> KIKUSUI and PMX are trademarks of KIKUSUI ELECTRONICS CORPORATION. This project
> is not affiliated with, endorsed by, or certified by KIKUSUI ELECTRONICS
> CORPORATION.
