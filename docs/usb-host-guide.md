# USB Host Development Guide

> 日本語版: [usb-host-guide.ja.md](usb-host-guide.ja.md)

How to get a USB device working on an ESP32. The first half covers USB Host itself, the second half covers what is specific to the ESP32 series and how to actually run the experiments.

The deeper material -- descriptor byte layouts, channels and the FIFO split, error recovery, throughput design, callback context, and implementing your own class -- is in the [advanced guide](usb-host-advanced.md).

It is written so that someone who has never touched USB can follow it, but the goal is that you can diagnose "why doesn't this work" yourself, so the mechanics are not skipped. If you are in a hurry, start at [Running the experiments](#4-running-the-experiments) and come back when a term is unfamiliar.

## Contents

1. [USB Host fundamentals](#1-usb-host-fundamentals)
2. [Power and hubs](#2-power-and-hubs)
3. [ESP32-specific limits](#3-esp32-specific-limits)
4. [Running the experiments](#4-running-the-experiments)
5. [Working out a protocol](#5-working-out-a-protocol)
6. [Troubleshooting](#6-troubleshooting)
7. [Tool index](#7-tool-index)
8. [References](#8-references)

---

## 1. USB Host fundamentals

### 1.1 Host and device

USB is not a peer-to-peer bus. There is **one host and the devices hanging off it**, and every transfer is started by the host. A device cannot send data on its own; it answers when the host asks.

A PC is the host; a keyboard or a flash drive is a device. With this library the ESP32 takes the PC's role: it is the **host**.

The first thing that confuses people is the USB connector on the ESP32 board. Two physically identical USB-C connectors can have completely different roles:

- **The connector you plug into a PC** — the ESP32 is the device here; used for flashing and the serial monitor.
- **The connector you plug USB devices into** — the ESP32 is the host here; this is the one this library uses.

Which is which differs per board, and the silkscreen (`USB`, `OTG`, `UART`) is not reliable. Check the schematic.

### 1.2 The four wires

A standard USB 2.0 cable has four wires.

| Wire | Role |
|------|------|
| VBUS | +5 V power. **The host supplies it to the device** |
| D+ / D- | The differential data pair |
| GND | Ground |

Being the host also means **being the power source**. This is where embedded boards trip people up first: if the board's connector does not put 5 V on VBUS, no amount of correct wiring will start the device ([chapter 2](#2-power-and-hubs)).

Charge-only cables have no D+/D-. When a device is not detected at all, suspect the cable first.

### 1.3 Speeds

| Speed | Short | Rate | Typical use |
|-------|-------|------|-------------|
| Low Speed | LS | 1.5 Mbps | Older mice and keyboards |
| Full Speed | FS | 12 Mbps | Keyboards, mice, serial bridges, MIDI, most small devices |
| High Speed | HS | 480 Mbps | Flash drives, cameras, audio interfaces, Ethernet adapters |
| SuperSpeed | SS | 5 Gbps and up | USB 3.x. **Not usable on ESP32** |

Most USB 3.x devices fall back to USB 2.0, but that is up to the device. SS-only devices cannot be used.

**The device decides the speed; the host does not choose it** — but a device whose speed the host does not implement will not work. The ESP32-S2/S3 host is full speed only; the ESP32-P4 has both a full-speed and a high-speed port ([chapter 3](#3-esp32-specific-limits)).

### 1.4 Enumeration

Between plugging a device in and the application being able to use it, the host goes through these steps. Knowing them lets you read where in the log it failed.

1. **Attach detection** — the D+/D- pull-up tells the host that a device is present, and at what speed
2. **Reset** — the bus is reset
3. **Address assignment** — the device is given an address between 1 and 127
4. **Device descriptor** — VID/PID, USB version, class
5. **Configuration descriptor** — interfaces and endpoints, read as one block
6. **Set configuration** — normally the first one
7. **Interface claim** — the driver (this library) claims the interfaces it handles and opens their endpoints

`usb.onDeviceConnected()` fires after step 7. If you never get there, the problem is electrical (1–2), a descriptor that cannot be read or is too long (4–5), or a resource shortage (7).

### 1.5 The descriptor hierarchy

A device declares what it is through **descriptors**:

```
Device (the whole device: VID/PID, EP0 size)
└── Configuration (current draw, bus- or self-powered)
    ├── Interface 0 (class / subclass / protocol <- "what it is" lives here)
    │   ├── class-specific descriptors (HID descriptor, CDC functional descriptors, ...)
    │   ├── Endpoint 0x81 (IN, interrupt, max packet 8, interval 10)
    │   └── Endpoint 0x01 (OUT, ...)
    └── Interface 1
        └── ...
```

Three things matter here:

- **The class belongs to the interface, not the device.** The device descriptor's class is often `0x00` ("see the interfaces"), so you have to look at the interfaces to know what a device really is.
- **One device can have several interfaces (a composite device).** Keyboard plus mouse, or keyboard plus a vendor-specific function, is entirely normal. An Interface Association Descriptor (`bDescriptorType=0x0b`) may group several interfaces into one function.
- **Class-specific descriptors do not appear in a parsed dump.** The HID descriptor's report-descriptor reference, CDC functional descriptors, the CCID class descriptor and so on require walking the raw configuration descriptor bytes. [`EspUsbHostDeviceExplorer`](../examples/Info/EspUsbHostDeviceExplorer/) does that.

### 1.6 Endpoints and transfer types

An endpoint is a communication port. It has an address (`0x81`: bit 7 is the direction, the low 4 bits are the number), a direction (IN = device to host, OUT = host to device), a transfer type, a max packet size (MPS), and a polling interval.

| Transfer type | Character | Used by |
|---------------|-----------|---------|
| **Control** | Request/response on EP0. Every device has it | Enumeration, class requests, HID SET_REPORT |
| **Interrupt** | Polled by the host at a fixed interval. Small and low-latency | Keyboards, mice, CCID status notifications |
| **Bulk** | Bulk data. No bandwidth guarantee, errors are retried | Flash drives, printers, serial bridges, vendor protocols |
| **Isochronous** | Reserved bandwidth, no retries | Audio, video |

Directions are **from the host's point of view**: "IN" means data coming from the device.

Each endpoint consumes a hardware resource (a channel) on the host side, which matters on the ESP32 ([3.4](#34-the-channel-limit)).

### 1.7 Classes, and how the same product can look different

USB defines standard classes; devices with the same class code are driven the same way.

| Class | Code | How this library handles it |
|-------|------|-----------------------------|
| Audio / MIDI | `0x01` | Library API ([Audio](../examples/Audio/) / [MIDI](../examples/MIDI/)) |
| CDC (serial, Ethernet) | `0x02` / `0x0a` | Library API ([Serial](../examples/Serial/) / [UsbNetwork](../examples/UsbNetwork/)) |
| HID | `0x03` | Library API ([HID](../examples/HID/)) |
| Printer | `0x07` | Example ([EspUsbHostPrinterEscPos](../examples/Vendor/EspUsbHostPrinterEscPos/)) |
| Mass Storage | `0x08` | Library API ([Storage](../examples/Storage/)) |
| Hub | `0x09` | Library API |
| Smart Card (CCID) | `0x0b` | Library API ([Ccid](../examples/Ccid/)) |
| Video (UVC) | `0x0e` | **Not supported** ([3.5](#35-the-256-byte-control-transfer-wall)) |
| Application Specific (USBTMC, DFU) | `0xfe` | Example ([EspUsbHostUsbtmcScpi](../examples/Vendor/EspUsbHostUsbtmcScpi/)) |
| Vendor Specific | `0xff` | Library API ([Vendor](../examples/Vendor/)) |

The thing every beginner hits is that **two products with the same purpose can look completely different on USB**:

| Product | What it commonly looks like |
|---------|-----------------------------|
| Barcode scanner | HID keyboard (scans arrive as keystrokes) / CDC serial / vendor HID reports. **Often switchable by model or by scanning a configuration barcode** |
| Receipt printer | Printer class `0x07` / CDC serial `0x02` / vendor-specific `0xff`. Some models switch with a DIP switch or a config tool |
| USB serial bridge | CDC-ACM `0x02` / vendor-specific `0xff` (FTDI, CP210x, CH34x are the latter) |
| Test instrument | USBTMC `0xfe/0x03` / CDC serial / vendor-specific |
| Gamepad | HID / vendor-specific (XInput family) |
| Power supply, measurement module | A private protocol carried inside HID reports (e.g. [DP100](../examples/HID/EspUsbHostDp100Power/)) |

So there is no such feature as "barcode scanner support" — only "support for how *that* barcode scanner presents itself". This is why the first step is always to find out what the device in your hand looks like ([chapter 4](#4-running-the-experiments)).

A device that looks like a keyboard works straight away with `onKeyboard()`. That is by far the easiest case, so if your device can be switched into HID-keyboard or CDC mode, it is worth checking before writing any code.

---

## 2. Power and hubs

Most USB Host trouble is power, not software.

### 2.1 Does the board supply VBUS?

**Check this first.** Some boards do not put 5 V on the host connector at all.

- Espressif's official **ESP32-S3-DevKitC-1** does not power an attached device through its USB OTG connector. Either wire a separate supply to the device or use a self-powered hub.
- Some M5Stack products can switch USB connector power in software. Check the schematic and procedure for your product.
- Boards such as the **Freenove ESP32-S3-WROOM Board** can power a device from the OTG Type-C connector. For a straightforward start, use a board like this.

Measuring VBUS to GND with a meter is the reliable check. If it reads 0 V, nothing you do in firmware will help on that connector.

During development, prefer a **board with two USB connectors** so that flashing/serial monitor (USB-UART) and the host port (OTG) stay separate. The finished product can use a single-connector board such as an AtomS3.

### 2.2 Is there enough current?

`bMaxPower` in the configuration descriptor is what the device *declares* it draws from the bus (in 2 mA units). Actual peaks during operation can be much higher.

Typical symptoms of insufficient current:

- Nothing enumerates
- Enumeration works, but the device drops the moment it is loaded (writing, printing, a motor starting)
- The ESP32 itself resets (`Brownout detector was triggered`)

The fix is a **self-powered (mains-powered) USB hub**. A bus-powered hub only splits the ESP32's own supply and solves nothing. 2.5-inch hard drives, printers, wireless adapters and USB displays deserve particular care.

### 2.3 Hubs

A hub is needed for several devices at once. In practice:

- Use a **self-powered USB 2.0 hub**. USB 3.x hubs and products with internally cascaded hubs behave differently and are not fully validated.
- A hub itself consumes channels and an address ([3.4](#34-the-channel-limit)).
- Some hub/device pairs are known to crash the ESP-IDF hub driver. See "Hub combinations that crash ESP-IDF" in [tests/manual/README.md](../tests/manual/README.md).
- With a per-port-power-switching (PPPS) hub, [`EspUsbHostHubPPPS`](../examples/Info/EspUsbHostHubPPPS/) can power a device down and force re-enumeration, which is useful while debugging.

---

## 3. ESP32-specific limits

### 3.1 Chips and ports

| Chip | Host capability |
|------|-----------------|
| ESP32-S2 | One FS host. Little internal RAM, so `ESP_USB_HOST_MAX_DEVICES` defaults to 3 |
| ESP32-S3 | One FS host. The primary target of this library |
| ESP32-P4 | FS OTG and HS OTG — but **only one of them can be the host at a time** |
| ESP32 (original), C3, C6, ... | No USB OTG, or no host capability. **This library does not run on them** |

On ESP32-P4, `EspUsbHostConfig::port` selects `ESP_USB_HOST_PORT_FULL_SPEED` or `ESP_USB_HOST_PORT_HIGH_SPEED`. The ESP-IDF host stack can drive only one host peripheral, so FS and HS cannot both be hosts.

The P4's FS port shares its PHY with USB Serial/JTAG and is not fixed to one pin pair. See the ESP32-P4 notes in [README.md](../README.md) and [`EspUsbHostP4FsPhyRouting`](../examples/Info/EspUsbHostP4FsPhyRouting/).

### 3.2 Choosing the FS or the HS port (P4)

This is the choice that decides what you can actually plug in.

**With the FS port**

- Every attached device runs at full (or low) speed.
- Several devices through a hub work. Keyboard + mouse + serial belongs here.
- **Some devices do not work at full speed at all**: HS-only devices, and devices with no full-speed configuration — an endpoint with a 1024-byte max packet size (which only exists at high speed), or a UAC2 audio device with no FS configuration. A concrete case: the [Mirabox N3 family macro pad](../examples/HID/EspUsbHostMacroPadN3/) has a 1024-byte interrupt OUT endpoint, so it works only on the P4's HS port.

**With the HS port**

- HS devices run at their real speed. Measured bulk OUT throughput is about 36 MB/s against about 1.1 MB/s at full speed ([`vendor_bulk_throughput`](../tests/manual/vendor_bulk_throughput/)).
- **Hubs are effectively unusable.** The USB 2.0 specification allows FS/LS devices below a high-speed hub through its Transaction Translator, but that path is not usable in the current ESP-IDF environment, so a hub on the HS port does not work in practice.
- Consequently **an HS device and an FS keyboard cannot be used at the same time**. Treat the HS port as one directly attached device.

So the choice is speed versus simultaneous devices. When in doubt, try the FS port first and move only the devices that need it — those that do not work at FS, or that need the bandwidth — to the HS port.

The ESP32-S2/S3 have no HS port, so the choice does not exist there. A fast device that will not work on an S3 may still work on a P4's HS port.

### 3.3 Limits that come from the Arduino build configuration

This library runs on the **precompiled** ESP-IDF USB Host stack shipped by Arduino-ESP32. That build's configuration (sdkconfig) cannot be changed from a sketch or from the library. This is why some things are simply unavailable in the Arduino environment.

The control transfer size limit below is the main example, and it is the direct reason USB cameras cannot be used.

### 3.4 The channel limit

The ESP32-S3 USB host has **8 channels** (`OTG_NUM_HOST_CHAN`). EP0, every claimed endpoint, and hubs all consume them.

Composite devices, hubs, audio, MSC and several serial devices exhaust them quickly. One recorded example: through a hub, FTDI + CP210x works, while FTDI + CH34x fails on channel exhaustion ([tests/manual/README.md](../tests/manual/README.md)).

Any of these lines in the log means you ran out:

```
No more HCD channels available
EP Alloc error: ESP_ERR_NOT_SUPPORTED
Claiming interface error: ESP_ERR_NOT_SUPPORTED
```

`usb.endpointChannelCount()`, `usb.maxEndpointChannelCount()` and `usb.estimatedHcdChannelCount()` report usage; [`EspUsbHostBringUpCheck`](../examples/Info/EspUsbHostBringUpCheck/) and [`EspUsbHostDeviceInfo`](../examples/Info/EspUsbHostDeviceInfo/) print it.

### 3.5 The 256-byte control transfer wall

Arduino-ESP32's precompiled host stack is built with `CONFIG_USB_HOST_CONTROL_TRANSFER_MAX_SIZE=256`. One control transfer carries at most 256 bytes including the 8-byte setup packet. Two consequences:

1. **A device whose configuration descriptor exceeds 256 bytes in total fails to enumerate**, before any class driver runs. This is why USB cameras (UVC) do not work — a Logitech C920's configuration descriptor is 1,974 bytes. No sketch or library option can work around it; it would take a change on the Arduino-ESP32 side.
2. **One GET_DESCRIPTOR can read at most 248 bytes.** Tools that dump raw descriptor bytes stop there.

### 3.6 ESP32-S2 memory

The ESP32-S2 has much less internal RAM, so `ESP_USB_HOST_MAX_DEVICES` defaults to 3 (8 on the S3). Raise it with `-DESP_USB_HOST_MAX_DEVICES=N` and watch for `dram0_0_seg overflowed`. Larger examples (such as `UsbNetwork`) do not fit on an S2.

### 3.7 Other notes

- **P4 cache coherency** — on the P4, the DMA memory used for USB transfers is cached. The library writes those lines back before every IN submit, so no application-side workaround is needed (verified by [`msc_cache_coherency`](../tests/manual/msc_cache_coherency/)).
- **Core version** — ESP32-S2/S3 need arduino-esp32 3.2.0 or later, ESP32-P4 needs 3.3.1 or later.
- **Log level** — the reason enumeration failed usually appears only in the ESP-IDF log, so set Core Debug Level to `Verbose` while investigating.

---

## 4. Running the experiments

The route from an unknown device to working code. **Go top to bottom without skipping** — most wasted time comes from debugging a later step while an earlier one (power, connector) is still broken.

### Step 0. Bench checks, before any code

- Measure 5 V on VBUS of the host connector
- Confirm the cable carries data (not charge-only)
- Confirm the device works when plugged into a PC

### Step 1. Does the host start, and does the device enumerate?

Flash [`examples/Info/EspUsbHostBringUpCheck`](../examples/Info/EspUsbHostBringUpCheck/).

- Does `usb.begin(): ok` appear? If not, it is a build / target / core version problem
- Does `ENUMERATED` appear when you plug the device in? If not, it is power, connector, cable, or the device itself — the sketch prints the checklist
- Note the speed (FS/HS/LS) and the VID:PID

Try a plain USB keyboard or flash drive first. If that enumerates, the board is fine and the problem is specific to your device.

### Step 2. Find out what the device is

Flash [`examples/Info/EspUsbHostDeviceExplorer`](../examples/Info/EspUsbHostDeviceExplorer/). It prints:

- The parsed device / interface / endpoint state
- Per interface: what it is, and which API or example drives it
- The **raw** device and configuration descriptor bytes, plus a block-by-block walk

What to look for:

- How many interfaces there are, and each one's class / subclass / protocol
- Endpoint types (interrupt / bulk / isochronous), directions and max packet sizes
- Which interfaces the library claimed (`claimed=yes`)
- Any unfamiliar class-specific descriptors (`CS_INTERFACE` and friends)

The developer-side equivalents are [`tests/manual/device_dump`](../tests/manual/device_dump/) and [`tests/manual/raw_descriptor`](../tests/manual/raw_descriptor/), run from pytest so the output is logged:

```sh
cd tests
uv run --env-file .env pytest manual/device_dump/device_dump.py -v -s
uv run --env-file .env pytest manual/raw_descriptor/raw_descriptor.py -v -s
```

Capturing `lsusb -v -d <vid>:<pid>` (Linux) or USB Tree Viewer (Windows) output on a PC as well makes later comparison easier.

### Step 3. Try the matching class API

Once you know the class, flash the matching example as-is (the [table in 1.7](#17-classes-and-how-the-same-product-can-look-different)). Most devices work at this point.

- HID → [`EspUsbHostHIDRawDump`](../examples/HID/EspUsbHostHIDRawDump/) for raw reports; keyboard/mouse have dedicated examples
- CDC / VCP → [`EspUsbHostUSBSerial`](../examples/Serial/EspUsbHostUSBSerial/)
- MSC → [`EspUsbHostMSCFatList`](../examples/Storage/EspUsbHostMSCFatList/)
- CCID → [`EspUsbHostCcidReader`](../examples/Ccid/EspUsbHostCcidReader/)
- MIDI / Audio → [MIDI](../examples/MIDI/) / [Audio](../examples/Audio/)

### Step 4. For HID, read the report descriptor

A HID device declares the meaning of its data in a **report descriptor**. [`EspUsbHostHIDReportDescriptor`](../examples/Info/EspUsbHostHIDReportDescriptor/) fetches and decodes it.

For gamepads and custom devices, the Usage Page / Usage / Report Size / Report Count items tell you which bits of an input report mean what. A vendor-defined usage page (`0xff00` and above) means the report content is a private protocol. A **private protocol wrapped in HID** is common (the DP100 and the macro pad both work this way). In that case continue with Step 5.

### Step 5. Look for published information

Once you know it is a private protocol, look for existing work **before** analysing it yourself. Most devices have already been worked out by someone.

- Vendor protocol documents, SDKs, API documentation
- Linux kernel drivers (`drivers/usb/`, `drivers/hid/`, `drivers/net/usb/`). Working code is a specification
- libusb-based open source projects, Python implementations, `sigrok` protocol decoders
- Libraries for other microcontrollers that target the same device
- **Industry-standard protocols** such as ESC/POS, SCPI, CCID and ADB. The class may be `0xff` while the payload is standard

The examples in this repository are built exactly that way: open the interface with the generic API, then speak a documented protocol over it — printers (ESC/POS), instruments (USBTMC + SCPI), Android (ADB), DisplayLink, AX206. The protocol notes live under [`docs/`](.) ([printer-spec.ja.md](printer-spec.ja.md), [usbtmc-spec.ja.md](usbtmc-spec.ja.md), [ccid-api-spec.ja.md](ccid-api-spec.ja.md), [dp100-spec.ja.md](dp100-spec.ja.md), ...).

### Step 6. If nothing is published, capture it on a PC

When nothing is published, or what you find is incomplete, capture what the vendor's own software says to the device **on a PC** and read it back out. The procedure is in [chapter 5](#5-working-out-a-protocol).

### Step 7. Reproduce it on the ESP32

Replay the transfers you read out of the capture with [`examples/Vendor/EspUsbHostProtocolConsole`](../examples/Vendor/EspUsbHostProtocolConsole/). One transfer at a time, with no rebuild in between, which keeps the analysis cycle fast:

```
> ctl 80 06 0100 0000 12        # GET_DESCRIPTOR(DEVICE) first, to confirm the path
> open 0                        # claim interface 0
> out 10 04 01                  # send the command seen in the capture
> in 40                         # read the answer
```

When the answer matches the capture, that exchange is understood. Failures are informative too — see [Reading failures](../examples/Vendor/EspUsbHostProtocolConsole/README.md#reading-failures).

### Step 8. Write it into a sketch

Turn the console session into `vendorOpen()` / `vendorWrite()` / `vendorReadSync()` / `vendorControlTransfer()` calls. [`EspUsbHostVendorBulk`](../examples/Vendor/EspUsbHostVendorBulk/) is the minimal skeleton.

Things to get right:

- **Reproduce the init sequence completely**, including order and delays. When it does not work, look for a request in the capture that you skipped.
- **ZLP (zero-length packet)** — when a transfer length is a multiple of the max packet size, some protocols need a zero-length packet to terminate it (`vendorWriteZlp()` / `vendorSetAutoZlp()`).
- **Read mode** — open a request/response device with `ESP_USB_HOST_VENDOR_READ_ON_DEMAND` and read after writing. A device that streams unprompted wants `CONTINUOUS`.
- **Max packet size and splitting** — large payloads may have to be split by the caller.

### Step 9. Write down what you found

Once it is reproduced, record it in the following form — it helps both your future self and others.

- Put a minimal working sketch under `examples/` (in this repository, with README.md, README.ja.md and sketch.yaml)
- Add anything that needs a human to judge it as a manual test under `tests/manual/`
- Write the protocol findings into `docs/<device>-spec.ja.md`
- Record the exact VID:PID you verified, and any model-specific behaviour

---

## 5. Working out a protocol

### 5.1 Capturing

**Windows: USBPcap + Wireshark**

1. Install [USBPcap](https://desowin.org/usbpcap/) (the Wireshark installer may include it)
2. Start Wireshark and pick a `USBPcap1`-style interface. These are **per root hub**, so check which hub the device is on
3. **Start the capture before plugging the device in**, so the whole enumeration is recorded
4. Start the vendor software and perform exactly one operation
5. Stop and save

**Linux: usbmon**

```sh
sudo modprobe usbmon
sudo wireshark          # pick a usbmonX interface
# or as text
sudo cat /sys/kernel/debug/usb/usbmon/0u
```

For descriptors alone, `lsusb -v -d <vid>:<pid>` is quicker, and `usbhid-dump` gives HID report descriptors.

**macOS**: USB Prober from Xcode's Additional Tools, or Wireshark's USB capture.

### 5.2 Reading a capture

The practical order to work through a capture in Wireshark:

1. **Filter down to the device** — `usb.device_address == 5`. The address is visible in the enumeration packets
2. **Separate enumeration from operation** — the first GET_DESCRIPTOR burst is enumeration; the protocol starts after it
3. **Work by transfer type**
   - `URB_CONTROL` — note `bmRequestType` / `bRequest` / `wValue` / `wIndex` / `wLength` from the setup packet. Those are exactly the arguments of the console's `ctl` command
   - `URB_BULK` / `URB_INTERRUPT` — the payloads. Follow OUT (host to device) and IN (device to host) as pairs
4. **Spot the repetition** — a request that repeats on a timer is polling, and can be left for later

### 5.3 Diffing captures is the shortest path

The most effective way to read an unknown protocol is to **compare two captures that differ in exactly one thing**.

- Set brightness to 10 in the vendor app, capture; set it to 11, capture → the byte that changed is the brightness
- Idle capture versus one button press → the difference is the event
- Output on versus output off → the state bit

That is how you pin down the frame layout (header, command id, length, payload, checksum) one byte at a time.

Common clues:

- Fixed leading bytes (a magic number)
- A length field (look for a value that matches the payload length)
- A sequence number (increments by one)
- A trailing checksum (a plain sum, XOR, or CRC-16/MODBUS are common — see the [DP100 notes](dp100-spec.ja.md))
- Little-endian 16/32-bit values (all standard USB fields are little-endian)

### 5.4 Cautions

- Keep to **analysing hardware you own, for your own use**. Do not do anything that breaks software licence terms or local law.
- Captures can contain what you typed or scanned (card IDs, entered text). Check the content before sharing a capture.
- **Be careful with write commands.** An unverified command can corrupt settings or brick firmware. Establish the read side first, and only send writes whose meaning you are sure of — especially where the effect is physical, such as switching a power supply's output on.

---

## 6. Troubleshooting

| Symptom | Likely cause | Check / fix |
|---------|--------------|-------------|
| `usb.begin()` fails | Target has no USB host, or the core is too old | Is it an S2/S3/P4? Is arduino-esp32 3.2.0+ (P4: 3.3.1+)? |
| Nothing happens on plug-in | No VBUS | Measure it. Add a self-powered hub |
| Nothing happens on plug-in | Wrong connector (plugged into the UART side) | Check the schematic; use [`BringUpCheck`](../examples/Info/EspUsbHostBringUpCheck/) |
| Nothing happens on plug-in | Charge-only cable | Use a data cable |
| Only one device never enumerates | Configuration descriptor over 256 bytes | Check the verbose log. UVC cameras cannot be supported ([3.5](#35-the-256-byte-control-transfer-wall)) |
| Only one device never enumerates | The device does not work at full speed | Try the P4 HS port ([3.2](#32-choosing-the-fs-or-the-hs-port-p4)) |
| Drops out under load | Not enough current | Self-powered hub, external supply |
| The ESP32 reboots | Brownout from current draw | As above; look for `Brownout detector` in the log |
| Fails with several devices | Out of channels | Look for `No more HCD channels available`; use fewer devices ([3.4](#34-the-channel-limit)) |
| Hub does not work on the HS port | Current limitation | Use one device directly on HS; use the FS port if you need a hub ([3.2](#32-choosing-the-fs-or-the-hs-port-p4)) |
| Enumerates but `supported=no` | No class driver in the library for it | Inspect it with [`DeviceExplorer`](../examples/Info/EspUsbHostDeviceExplorer/) and drive it with the vendor API |
| `vendorOpen()` fails | The library already claimed that interface | Check `claimed`; name a different interface number |
| Bulk IN times out | Polling a request/response device | Open with `ESP_USB_HOST_VENDOR_READ_ON_DEMAND`, write first, then read |
| Transfers stall part way | The protocol needs a ZLP | `vendorWriteZlp()` / `vendorSetAutoZlp()` |
| Reboot loop when a hub is attached | A known hub/device combination | See the table in [tests/manual/README.md](../tests/manual/README.md) |
| No idea what is happening | The ESP-IDF log is not visible | Reproduce with Core Debug Level set to `Verbose` |

---

## 7. Tool index

### For users (examples/)

| Tool | Purpose |
|------|---------|
| [`Info/EspUsbHostBringUpCheck`](../examples/Info/EspUsbHostBringUpCheck/) | **Run this first.** Does the host start, does the device enumerate, at what speed — with a checklist when nothing does |
| [`Info/EspUsbHostDeviceExplorer`](../examples/Info/EspUsbHostDeviceExplorer/) | **Run this second.** The whole layout, per-interface instructions on how to drive it, raw descriptors and a block walk |
| [`Info/EspUsbHostDeviceInfo`](../examples/Info/EspUsbHostDeviceInfo/) | Periodic dump of every connected device, hub info and channel usage |
| [`Info/EspUsbHostHIDReportDescriptor`](../examples/Info/EspUsbHostHIDReportDescriptor/) | Fetch and decode HID report descriptors |
| [`HID/EspUsbHostHIDRawDump`](../examples/HID/EspUsbHostHIDRawDump/) | Raw HID input report dump |
| [`Vendor/EspUsbHostProtocolConsole`](../examples/Vendor/EspUsbHostProtocolConsole/) | **For protocol work.** Type control/bulk transfers from the serial monitor and watch the answers |
| [`Vendor/EspUsbHostVendorBulk`](../examples/Vendor/EspUsbHostVendorBulk/) | The skeleton to turn your findings into a sketch |
| [`Info/EspUsbHostHubPPPS`](../examples/Info/EspUsbHostHubPPPS/) | Power a hub port off and on to force re-enumeration |
| [`Info/EspUsbHostP4FsPhyRouting`](../examples/Info/EspUsbHostP4FsPhyRouting/) | Route the ESP32-P4 FS OTG to the GPIO24/25 connector |

### For developers (tests/manual/)

Run from pytest so the output is logged. See [tests/manual/README.md](../tests/manual/README.md) for how to run them.

| Tool | Purpose |
|------|---------|
| [`smoke`](../tests/manual/smoke/) | Checks the manual-test workflow itself. Run it first on a new machine |
| [`device_dump`](../tests/manual/device_dump/) | Parsed dump of every device, plus the bulk/interrupt endpoints of unclaimed interfaces |
| [`raw_descriptor`](../tests/manual/raw_descriptor/) | Raw DEVICE/CONFIGURATION descriptors and a block walk, for comparing against USBPcap |
| [`hid_report_descriptor`](../tests/manual/hid_report_descriptor/) | HID report descriptor retrieval |
| [`hotplug`](../tests/manual/hotplug/) | Survives repeated connect/disconnect cycles |
| [`hub_info`](../tests/manual/hub_info/) / [`hub_power`](../tests/manual/hub_power/) | Hub topology and per-port power control |
| [`vendor_bulk_throughput`](../tests/manual/vendor_bulk_throughput/) | Effective bulk OUT throughput — the practical ceiling of the board |
| others | Per-class and per-device manual tests ([catalog](../tests/manual/README.md)) |

---

## 8. References

- [USB 2.0 Specification](https://www.usb.org/document-library/usb-20-specification)
- [USB Class Codes](https://www.usb.org/defined-class-codes)
- [USB Device Class Documents](https://www.usb.org/documents) — HID, CDC, MSC, CCID, UAC and the rest
- [USB Made Simple](https://www.usbmadesimple.co.uk/) — a readable introduction
- [USBPcap](https://desowin.org/usbpcap/) / [Wireshark](https://www.wireshark.org/) — capturing on Windows
- [Linux USB project](https://www.kernel.org/doc/html/latest/driver-api/usb/index.html) — kernel drivers as de-facto specifications
- [ESP-IDF USB Host Library](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/api-reference/peripherals/usb_host.html) — what this library is built on
- [Tested Devices and Boards](tested-devices.md) — verified units with their VID:PIDs and conditions, and board notes
- [README.md](../README.md) in this repository — API reference and per-class status
- [docs/](.) — per-device protocol notes
