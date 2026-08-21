# EspUsbHost

> 日本語版: [README.ja.md](README.ja.md)

Arduino library for using USB Host on ESP32-S3, ESP32-S2 and ESP32-P4.

USB events are processed in a background FreeRTOS task, so `loop()` does not need to call any USB polling function. Register callbacks in `setup()`, call `begin()`, and the library handles the rest.

New to USB Host, or stuck with a device that will not work? Start with the **[USB Host Development Guide](docs/usb-host-guide.md)**: USB fundamentals, the ESP32-specific limits (power, speeds, hubs, channels), and the route from checking a board through identifying an unknown device to working out an undocumented protocol. The [advanced guide](docs/usb-host-advanced.md) continues into descriptor byte layouts, host channels and the FIFO split, error recovery, throughput design, callback context, and writing your own class wrapper. Devices and boards that have been verified on real hardware are listed in [Tested Devices and Boards](docs/tested-devices.md), and everything under `docs/` is indexed in [docs/README.md](docs/README.md).

## Contents

- [Requirements](#requirements)
- [Version 2 status](#version-2-status)
- [Sibling Library: EspUsbDevice](#sibling-library-espusbdevice)
- [Features](#features)
- [Supported USB classes](#supported-usb-classes)
- [Roadmap](#roadmap)
- [Current limits and cautions](#current-limits-and-cautions)
- [Hardware requirements](#hardware-requirements)
- [Installation](#installation)
- [Quick start](#quick-start)
- [Examples](#examples)
- [API reference](#api-reference)
  - [Core](#core)
  - [Device events](#device-events)
  - [HID input](#hid-input)
  - [HID output](#hid-output)
  - [USB serial (CDC ACM and VCP)](#usb-serial-cdc-acm-and-vcp)
  - [Vendor bulk/control](#vendor-bulkcontrol)
  - [CCID smart card reader](#ccid-smart-card-reader)
  - [MIDI](#midi-1)
  - [USB audio](#usb-audio)
  - [USB Mass Storage](#usb-mass-storage)
  - [USB Hub](#usb-hub)
  - [USB network (CDC-NCM / CDC-ECM)](#usb-network-cdc-ncm--cdc-ecm)
  - [Device discovery](#device-discovery)
  - [Error handling](#error-handling)
- [Design](#design)
- [Multiple devices](#multiple-devices)
- [Tests](#tests)
- [Release checklist](#release-checklist)
- [License](#license)

## Requirements

Minimum Arduino-ESP32 core (board package) version:

| Target | Minimum arduino-esp32 |
| --- | --- |
| ESP32-S2 / ESP32-S3 | 3.2.0 |
| ESP32-P4 | 3.3.1 |

Older cores are not supported: 3.1.x and earlier may fail to build. Per-library-version build results across core versions are published under [`docs/`](docs/) as `COMPATIBILITY.<version>.md`.

ESP32-S3 is the primary target and the one the peer/manual tests run on. ESP32-S2
is built for every release across all supported cores, but it has far less internal
RAM, so `ESP_USB_HOST_MAX_DEVICES` defaults to 3 there instead of 8 (raise it with
`-DESP_USB_HOST_MAX_DEVICES=N` and watch for `dram0_0_seg overflowed`), and the
`UsbNetwork` example does not fit. ESP32-P4 adds HS OTG; see
[ESP32-P4 notes](#esp32-p4-notes).

## Version 2 status

Version 2 is a ground-up redesign and is **not compatible with the 1.x API**. The old inheritance/virtual-method style is no longer the primary interface; use the callback-based APIs such as `onKeyboard()`, `onMouse()`, `onDeviceConnected()`, and class-specific send/start APIs.

This release has substantially broader USB class support than 1.x, but it is still a practical Arduino USB Host library under active development, not a fully validated replacement for every ESP-IDF USB class driver. APIs may still change incompatibly in later 2.x releases when that makes the API simpler, safer, or more consistent.

Test coverage is better than the 1.x series and includes example build checks plus peer/manual tests for major paths, but many combinations still depend on real hardware. Treat USB Audio, USB Hub edge cases, multi-device setups, and unusual/non-compliant USB devices as areas that need device-specific verification.

## Sibling Library: EspUsbDevice

The device-side sibling library is
[`EspUsbDevice`](https://github.com/tanakamasayuki/EspUsbDevice).
EspUsbDevice is a USB Device library expanded alongside EspUsbHost, and is used
for Host/Device combination tests and ESP32-P4 single-board loopback validation.

Arduino-ESP32's standard `USB`, `USBHIDKeyboard`, `USBHIDMouse`, `USBCDC`, and
similar APIs are convenient for short, common USB Device sketches. EspUsbDevice
instead focuses on explicit sketch-side control over port, speed, descriptors,
endpoint packet sizes, HID report IDs, output/feature reports, and raw class
reports.

For keyboards, the standard `USBHIDKeyboard` API is useful for sending simple
text, but it has limits when you need complete Japanese-layout coverage or exact
HID usages for keys such as Muhenkan, Henkan, Kana, Hankaku/Zenkaku, and
JIS-specific symbol keys. EspUsbDevice keeps raw HID usage/report control
available alongside text helpers so EspUsbHost keyboard layout behavior can be
tested precisely.

Use the standard Arduino-ESP32 USB Device APIs first when you only need a normal
keyboard, mouse, CDC, or similar device connected to a PC. Use EspUsbDevice when
you need detailed EspUsbHost validation, descriptors or reports that the
Arduino Core standard Device implementation cannot easily control, or ESP32-P4
Host/Device loopback tests.

## Features

- **HID input** — keyboard, mouse, consumer control (media keys), system control (power/standby), gamepad
- **HID output** — keyboard LED control, vendor output/feature reports
- **USB serial** — CDC ACM and common VCP devices (FTDI, CP210x, CH34x) via `EspUsbHostCdcSerial` (Arduino `Stream`/`Print` compatible)
- **MIDI** — USB MIDI input and output
- **USB audio** — raw isochronous IN payloads and isochronous OUT writes for USB Audio streaming interfaces
- **USB Mass Storage** — USB Mass Storage Bulk-Only Transport with SCSI capacity/read/write block access, FatFs/VFS mounting, and Arduino `fs::FS` / `File` compatibility
- **USB network** — CDC-NCM / CDC-ECM USB Ethernet adapters, either as raw Ethernet frames or attached as an lwIP (`esp_netif`) interface so `NetworkClient` / `HTTPClient` run over USB with no Wi-Fi
- **CCID smart card readers** — claim a CCID interface, card insertion/removal notifications, card activation with ATR, card type (ISO 14443 A/B, ISO 15693, FeliCa, ...) from the ATR, APDU exchange, and reader-specific escape commands
- **Vendor bulk/control** — generic non-HID vendor-specific interfaces with bulk IN/OUT, an asynchronous bulk OUT queue with zero-copy buffers and automatic ZLP handling, and EP0 vendor requests. Also how classes without a dedicated API are reached: USBTMC instruments and ESC/POS printers are examples built entirely on it
- **Device discovery** — enumerate connected devices, interfaces, and endpoints
- **Multiple devices** — each callback and send API accepts an optional `address` parameter to target a specific device

## Supported USB classes

Every USB class code this library has been used with, and how. **Library API** means
the library has a dedicated API for that class; **example** means the class is
handled entirely in a sketch on top of a general-purpose API, with nothing
class-specific in the library. The next section lists the same ground by maturity.

| Class | Code | How it is supported | Where |
|---|---|---|---|
| Audio (UAC1 / UAC2) | `0x01` | Library API — isochronous IN payloads and OUT writes | [`examples/Audio/`](examples/Audio/) |
| MIDI (Audio subclass 3) | `0x01`/`0x03` | Library API — MIDI in and out | [`examples/MIDI/`](examples/MIDI/) |
| CDC Control / Data (ACM) | `0x02`/`0x0a` | Library API — `EspUsbHostCdcSerial`, Arduino `Stream`/`Print` | [`examples/Serial/`](examples/Serial/) |
| HID | `0x03` | Library API — keyboard, mouse, gamepad, consumer/system control, vendor reports | [`examples/HID/`](examples/HID/) |
| **Printer** | **`0x07`** | **Example — ESC/POS receipt printers over the vendor bulk/control API** | [`examples/Vendor/EspUsbHostPrinterEscPos/`](examples/Vendor/EspUsbHostPrinterEscPos/) |
| Mass Storage (BOT/SCSI) | `0x08` | Library API — block I/O and FatFs / Arduino `fs::FS` | [`examples/Storage/`](examples/Storage/) |
| Hub | `0x09` | Library API — detection, topology, per-port power (PPPS) | [`examples/Info/`](examples/Info/) |
| Smart Card (CCID) | `0x0b` | Library API — ATR, card type, APDU exchange, escape commands | [`examples/Ccid/`](examples/Ccid/) |
| Video (UVC) | `0x0e` | **Not supported** — see the descriptor size limit below | — |
| CDC-NCM / CDC-ECM Ethernet | `0x02` subclasses | Library API — raw frames or an lwIP `esp_netif` | [`examples/UsbNetwork/`](examples/UsbNetwork/) |
| Application Specific (USBTMC) | `0xfe` | Example — USBTMC/USB488 + SCPI over the vendor bulk/control API | [`examples/Vendor/EspUsbHostUsbtmcScpi/`](examples/Vendor/EspUsbHostUsbtmcScpi/) |
| Vendor-specific | `0xff` | Library API — explicit interface claim, bulk IN/OUT, EP0 requests | [`examples/Vendor/`](examples/Vendor/) |

Devices whose interface class says nothing useful are reached the same way: USB
serial bridges (FTDI, CP210x, CH34x) are vendor-specific interfaces driven by
`EspUsbHostCdcSerial`, and an ALIENTEK DP100 power supply is a plain HID interface
carrying its own framed protocol
([`examples/HID/EspUsbHostDp100Power`](examples/HID/EspUsbHostDp100Power/)).

`printDeviceInfo()` and the [`device_dump`](tests/manual/device_dump/) manual test
print the class of anything you plug in, supported or not, so an unknown device can
be identified before any code is written for it.

## Roadmap

### USB class support

| Class | Status |
|-------|--------|
| HID — keyboard, mouse, gamepad, consumer control, system control, vendor | ✅ Done |
| USB serial — CDC ACM and VCP (FTDI, CP210x, CH34x) via `EspUsbHostCdcSerial`; baud, data bits, parity, and stop bits are configurable | ✅ Done |
| USB MIDI | ✅ Done |
| Vendor-specific bulk/control | ✅ Basic support implemented. Covers explicit interface claim, bulk IN/OUT (synchronous and an asynchronous queue), automatic ZLP, and EP0 vendor IN/OUT requests |
| CCID — smart card readers (bulk protocol) | ✅ Basic support implemented. Covers explicit interface claim, class descriptor parsing, slot status, power on/off with ATR, card type decoding from the ATR, APDU/XfrBlock exchange, escape and raw messages, and slot-change notifications. Verified with a Sony RC-S300; ICCD variants, chained (extended APDU) responses, and PIN-pad features are out of scope |
| USB graphics adapter (DL-1xx bulk protocol) | 📄 Example only, best effort. Implemented in [`examples/Vendor/EspUsbHostDisplayDl1xx`](examples/Vendor/EspUsbHostDisplayDl1xx/) on top of the vendor bulk API — nothing display-specific is in the library. A reference implementation for one chip family at 16 bpp; for other adapters or higher frame rates use a dedicated library such as [Pico_USB_Disp](https://github.com/htlabnet/Pico_USB_Disp) |
| AX206 USB photo-frame display | 📄 Example only, best effort. Implemented in [`examples/Vendor/EspUsbHostDisplayAx206`](examples/Vendor/EspUsbHostDisplayAx206/). Verified on an ESP32-S3 at 2 fps: the device takes whole-screen blits only, so every frame is one Bulk-Only Transport transaction carrying 307,200 bytes |
| USB smart screen (CDC serial protocol) | 📄 Example only, best effort. Implemented in [`examples/Serial/EspUsbHostDisplayTuring`](examples/Serial/EspUsbHostDisplayTuring/) on top of the CDC serial write queue — nothing display-specific is in the library. Covers the 3.5-inch `USB35INCHIPSV2` panel (`1a86:5722`) at 16 bpp. Indexed with the other display examples in [docs/usb-display.md](docs/usb-display.md) |
| USBTMC — SCPI test and measurement instruments | 📄 Example only, best effort. Implemented in [`examples/Vendor/EspUsbHostUsbtmcScpi`](examples/Vendor/EspUsbHostUsbtmcScpi/) on top of the vendor bulk/control API — nothing USBTMC-specific is in the library. The interface class is 0xFE (Application Specific), not vendor-specific; the example lives under `Vendor/` because that is the API it uses. Verified against a KIKUSUI PMX18-5A DC power supply (`0b3e:1029`): class requests, the bulk message layer, and SCPI queries. The USB488 interrupt IN (service requests) is not used |
| Printer — ESC/POS receipt printers | 📄 Example only, best effort. Implemented in [`examples/Vendor/EspUsbHostPrinterEscPos`](examples/Vendor/EspUsbHostPrinterEscPos/) on top of the vendor bulk/control API — nothing printer-specific is in the library. The interface class is 0x07; the example lives under `Vendor/` because that is the API it uses. Verified against an Xprinter XP-C58K (`0483:070b`): the three class requests, ESC/POS real-time status, and printing a Japanese receipt with a barcode, a QR code and an auto cut. Both class requests turned out to be answered-but-empty on that model, so real-time status (`DLE EOT n`) is the status path to rely on. IPP / PWG-Raster / PCL and IEEE 1284.4 packet mode are out of scope |
| ALIENTEK DP100 digital power supply | 📄 Example only, best effort. Implemented in [`examples/HID/EspUsbHostDp100Power`](examples/HID/EspUsbHostDp100Power/) on top of the HID API -- nothing DP100-specific is in the library. The device is a plain HID interface carrying its own framed protocol, so `onHIDInput()` and `sendHIDVendorOutput()` cover it. Reading is verified on an ESP32-S3 (identity, input rail, output V/A, temperatures, units confirmed by measurement); the setpoint frame is implemented but not verified, because it carries the output enable |
| Mirabox N3 / Ajazz AKP03 family LCD macro pad | 📄 Example only, best effort. Implemented in [`examples/HID/EspUsbHostMacroPadN3`](examples/HID/EspUsbHostMacroPadN3/) on top of the HID API -- nothing pad-specific is in the library. The pad is a composite HID device whose vendor interface carries its own `CRT` protocol, so `onHIDVendorInput()` and `sendHIDVendorOutput()` cover it. Verified on a STREONOR S6 (`1500:3006`): brightness, clear, refresh, and a 64x64 JPEG on each of the six key screens. ESP32-P4 high-speed port only, because the pad's interrupt OUT endpoint has a 1024-byte MPS; the input report codes for the scene keys and encoders are not pinned down |
| UAC — USB audio input/output | 🔲 Experimental. UAC1 Audio OUT/IN are peer-tested with the standard Arduino `USBAudioCard`, UAC2 with an `EspUsbDevice` peer (descriptors, Clock Source sample rates, Feature Unit mute/volume, and OUT/IN streaming); real USB microphone/audio-interface validation remains |
| HUB — hub detection, topology info, and port power control | ✅ Basic support implemented. `hub_info` and `hub_power` manual tests pass; change-bit handling, cascaded hubs, and USB 3.x hub compatibility remain ongoing |
| CDC-NCM / CDC-ECM — USB Ethernet with raw frame access and lwIP netif attach | 🔲 Experimental. Peer-tested against the EspUsbDevice `UsbNetwork` sketch and an AX88179A adapter. Adapters whose network function is not in the default configuration need `setConfigurationSelector()` and two enumeration passes |
| MSC — USB storage block I/O and FatFs/Arduino FS mount | 🔲 Experimental. Basic read/write and FatFs mounting with a single MSC device are peer/manual tested. Non-compliant devices, multiple MSC devices/LUNs, and full abnormal BOT recovery need further validation |
| UVC — USB camera | ❌ Currently unsupported. The configuration-descriptor length limit in Arduino-ESP32's precompiled USB Host stack prevents typical UVC devices from enumerating |

### Other planned features

| Feature | Status |
|---------|--------|
| `onHIDReportDescriptor()` — HID report descriptor access | ✅ Done |
| HID gamepad input — descriptor-decoded fields plus raw/report bytes for user-defined mapping | ✅ Mappable event API; mapping helpers still under consideration |
| Channel count and endpoint usage visibility | ✅ Implemented as experimental diagnostics; useful for understanding limits with multi-device, Audio, MSC, and HUB combinations |
| USB Audio IN real payload validation | ✅ Peer validation complete with standard Arduino `USBAudioCard` (UAC1) and an `EspUsbDevice` peer (UAC2); real USB microphone/audio-interface validation remains |
| ESP32-P4 validation | 🔲 Ongoing; verify FS/HS OTG, hub behavior, and loopback tests separately |
| Loopback tests (ESP32-P4 single-board) | 🔲 In progress in `EspUsbDevice`; `tests/loopback` in this repository only contains README files |
| Manual tests — VCP serial, multi-device, hot-plug | ✅ Main cases confirmed; additional device compatibility remains ongoing |

## Current limits and cautions

- **Version compatibility:** 2.x is not source-compatible with 1.x. Existing 1.x sketches should be ported to the callback-based API and the new class-specific APIs.
- **Future breaking changes:** more incompatible API changes may still happen in 2.x while the API is being shaped around real devices and examples.
- **USB host resources:** ESP32-S3 has a small number of USB host channels. Composite devices, hubs, audio, MSC, and multiple serial devices can exhaust channels quickly. Use `printDeviceInfo()` / `printAllDeviceInfo()` and the endpoint/channel diagnostic APIs to inspect resource use.
- **Hubs:** use a self-powered USB 2.0 hub for multi-device tests. USB 3.x hubs and internally cascaded hubs may behave differently and are not fully validated.
- **USB Audio:** input/output peer tests pass with the standard Arduino `USBAudioCard` (UAC1) and an `EspUsbDevice` peer (UAC2). Real microphone/audio-interface validation is still limited, and because the ESP32-S3/S2 host is full speed only, a UAC2 device that has no full-speed configuration cannot be used at all. Clock Selector / Clock Multiplier entities and advanced Audio Control units remain unsupported.
- **UVC / USB cameras:** currently unsupported. Arduino-ESP32's precompiled USB Host stack is built with `CONFIG_USB_HOST_CONTROL_TRANSFER_MAX_SIZE=256`, so a USB device whose configuration descriptor exceeds 256 bytes fails during enumeration, before a class driver can start. The Logitech C920 tested here has a 1,974-byte descriptor. A sketch or EspUsbHost build option cannot change this value because it belongs to the build configuration used to generate Arduino-ESP32's precompiled libraries. UVC support will be reconsidered if Arduino-ESP32 removes or raises this limit.
- **Mass Storage:** FAT access is intended for a single practical MSC device. Multiple MSC devices, multiple LUN devices, unusual block sizes, and abnormal BOT recovery need more real-device validation. Non-compliant devices may require the `SYNCHRONIZE CACHE(10)` fallback described in the MSC section.
- **Hot plug:** unplugging while files, serial transfers, audio streams, or class operations are active can still fail or lose data depending on device behavior.
- **ESP32-P4:** FS/HS OTG selection is supported through `EspUsbHostConfig::port`, but P4 validation is still ongoing. HS OTG has practical limitations, especially with hubs.

## Hardware requirements

- ESP32-S3, or any board supported by Arduino-ESP32 USB Host
- Arduino-ESP32 core

### Recommended ESP32-S3 hardware and cautions

Before using a board as a USB Host, first verify whether its USB connector supplies VBUS (5 V) to the attached USB device.

The official Espressif Systems ESP32-S3-DevKitC-1 does not supply power to an attached device through its USB OTG connector. This is convenient when the board is used as a USB Device, but in USB Host mode you must either wire a separate power supply to the attached USB device or use an externally powered (self-powered) USB hub.

Some M5Stack products can control USB connector power from software. Check the schematic and power-control procedure for the specific product you use.

For a straightforward USB Host setup, we recommend a board such as the Freenove ESP32-S3-WROOM Board, which can power an attached device through its USB Type-C OTG connector.

The final application can use a product with only one USB connector, such as the AtomS3. During development, however, we recommend a board with two USB connectors so that the USB-to-UART connector used for flashing and the Serial Monitor remains separate from the USB OTG connector used for the attached device.

Even when a board has two USB connectors, which one is connected to the USB-to-UART bridge and which one is connected to the ESP32-S3 USB OTG peripheral depends on the board. For example, the USB-to-UART and USB OTG connector positions are reversed between the official Espressif Systems ESP32-S3-DevKitC-1 and the Freenove ESP32-S3-WROOM Board. Do not rely on connector position alone; check the board silkscreen, product documentation, and schematic.

### ESP32-P4 notes

ESP32-P4 contains three USB functions. These are controllers/PHY paths inside the SoC, not necessarily three physical connectors on every board:

1. **USB Serial/JTAG** — a fixed-function Full-speed USB controller for flashing, console CDC, and JTAG.
2. **USB OTG FS** — a programmable Full-speed/Low-speed OTG controller that can operate as Host or Device.
3. **USB OTG HS** — a programmable High-speed OTG controller with dedicated USB pins that can operate as Host or Device.

A board may expose all three, combine roles on a connector, or expose only some of them. It may also add an external USB-to-UART bridge such as CH34x or CP210x. That bridge is not one of the P4 USB controllers, so such a board can appear to have four USB-related connectors or paths. Common arrangements include:

- USB OTG HS + USB OTG FS + built-in USB Serial/JTAG
- USB OTG HS + built-in FS USB Serial/JTAG + external USB-to-UART serial
- USB OTG HS + USB OTG FS + built-in USB Serial/JTAG + external USB-to-UART serial

Check the board schematic rather than relying on connector labels such as `USB`, `OTG`, `UART`, or `DOWNLOAD`. An external USB-to-UART connector carries a normal P4 UART behind the bridge and is independent of the built-in USB Serial/JTAG and OTG controllers.

The chip-level signal pins are as follows. Board connectors may be wired differently, so always check the board schematic before wiring or choosing a port.

| Typical ESP32-P4 role | D- | D+ | Notes |
|------------------------|----|----|-------|
| USB CDC FS / USB Serial/JTAG FS | GPIO24 | GPIO25 | Commonly used for built-in USB Serial/JTAG or a FS device-side CDC connector. This connector is easy to confuse with USB Host on some boards. |
| USB OTG FS | GPIO26 | GPIO27 | Commonly used as the full-speed OTG connector; selectable as USB Host with `ESP_USB_HOST_PORT_FULL_SPEED`. |
| USB OTG HS | package pin 49 | package pin 50 | High-speed OTG port; these are dedicated USB pins, not general GPIOs. Select with `ESP_USB_HOST_PORT_HIGH_SPEED`. |

ESP32-P4 has two internal Full-speed/Low-speed PHYs. USB OTG FS and USB Serial/JTAG are mapped to opposite PHYs:

| Mapping | GPIO24/GPIO25 (FSLS PHY0) | GPIO26/GPIO27 (FSLS PHY1) |
|---------|----------------------------|----------------------------|
| Default | USB Serial/JTAG | USB OTG FS |
| Swapped | USB OTG FS | USB Serial/JTAG |

The assignment can be swapped permanently with the `USB_PHY_SEL` eFuse or temporarily at runtime. This is a one-to-one swap, not signal duplication: USB OTG FS and USB Serial/JTAG always use different FSLS PHYs. **Burning the eFuse is irreversible and is not recommended just to support a particular board connector.** The runtime override is normally the safer choice during development and for board-specific firmware.

ESP-IDF exposes the runtime mapping through its P4 low-level HAL. Call it before `usb.begin()` and before any other USB OTG FS driver is initialized:

```cpp
#include "hal/usb_wrap_ll.h"
#include "soc/usb_wrap_struct.h"

// Route USB OTG FS to GPIO24/GPIO25. USB Serial/JTAG moves to GPIO26/GPIO27.
usb_wrap_ll_phy_select(&USB_WRAP, 0);

EspUsbHostConfig config;
config.port = ESP_USB_HOST_PORT_FULL_SPEED;
usb.begin(config);
```

`usb_wrap_ll_phy_select(&USB_WRAP, 0)` maps USB OTG FS to FSLS PHY0 (GPIO24/GPIO25) and simultaneously maps USB Serial/JTAG to FSLS PHY1 (GPIO26/GPIO27). Passing `1` applies the opposite mapping. It does not make the same USB function appear on both pin pairs. This is a software override of the FS PHY mapping; it does not burn an eFuse. Resetting the chip returns startup control to the configured eFuse/default mapping.

Moving USB Serial/JTAG to GPIO26/GPIO27 does not create or initialize another CDC stack. It relocates the built-in fixed-function USB Serial/JTAG controller; if that controller is enabled, its CDC/JTAG USB device can therefore operate on GPIO26/GPIO27. If the serial monitor, flashing connection, or JTAG debugger is using GPIO24/GPIO25, it disconnects when OTG FS is moved there. Use an external USB-to-UART connector, another available console, or connect USB Serial/JTAG to GPIO26/GPIO27 when logs are needed after the switch.

Do not change the mapping while an OTG FS Host or Device driver is running. If another framework initialized OTG FS earlier, stop and uninstall that driver before changing the route, then initialize `EspUsbHost`. In a normal sketch that has not started another FS stack, calling the function immediately before `usb.begin(config)` is sufficient. USB Serial/JTAG may already have been initialized by ROM or the Arduino core; switching it causes a physical USB disconnect from the old PHY and possible re-enumeration on the new PHY.

This routing changes D+/D- connectivity only. USB Host also requires correct VBUS sourcing, over-current protection, and, for USB-C, appropriate role/CC handling supplied by the board hardware. Never assume that a connector wired to GPIO24/GPIO25 is electrically capable of Host mode from PHY routing alone. Because USB Serial/JTAG moves onto GPIO26/GPIO27, ensure that connector is not still sourcing Host VBUS and is not connected to a device that conflicts with the relocated USB Serial/JTAG device role.

GPIO26/GPIO27 cannot remain in use as ordinary GPIOs or by another peripheral while USB Serial/JTAG is mapped to them. Stop that GPIO/peripheral use before changing the USB route. If the USB mapping is later restored and GPIO26/GPIO27 are returned to another purpose, initialize those pins again with `pinMode()` or restart the owning peripheral driver with its `begin()`/configuration API. USB PHY setup can change the pin mux, direction, and pull configuration, so the previous GPIO/peripheral initialization must not be assumed to remain valid.

Only the OTG ports are usable as USB Host ports. Some boards make it hard to tell which connector is FS OTG versus a CDC/device connector, so check the board schematic and examples carefully.

The ESP-IDF USB Host stack can use only one host peripheral at a time. On ESP32-P4, choose either FS OTG or HS OTG with `EspUsbHostConfig::port`; you cannot run both as USB Host simultaneously. In Arduino-library use, the USB device function uses HS, and FS cannot be selected for that device role.

On ESP32-P4 the DMA-capable memory used for USB transfers is cached, and ESP-IDF's host driver only invalidates an IN buffer after the transfer completes — it never writes the buffer back before the DMA starts. Cache lines left dirty by the allocator can therefore be evicted over data the controller is still writing. This library writes those lines back with `esp_cache_msync()` immediately before every IN submit, so no application-side workaround is needed; `tests/manual/msc_cache_coherency` verifies this on real hardware.

High-speed OTG support is still limited in practice. USB hubs are effectively not usable on HS OTG in the current environment. This library has confirmed a USB keyboard working as USB Host on ESP32-P4, but detailed ESP32-P4 validation is still incomplete.

## Installation

Open the Arduino IDE Library Manager, search for **EspUsbHost**, and install.

Or clone this repository into your Arduino `libraries/` folder:

```sh
git clone https://github.com/tanakamasayuki/EspUsbHost
```

## Quick start

```cpp
#include "EspUsbHost.h"

EspUsbHost usb;

void setup() {
  Serial.begin(115200);

  usb.onKeyboard([](const EspUsbHostKeyboardEvent &event) {
    if (event.pressed && event.ascii) {
      Serial.print((char)event.ascii);
    }
  });

  if (!usb.begin()) {
    Serial.printf("usb.begin() failed: %s\n", usb.lastErrorName());
  }
}

void loop() {
}
```

## Examples

### HID

| Sketch | Description |
|--------|-------------|
| [EspUsbHostKeyboard](examples/HID/EspUsbHostKeyboard/) | Read keyboard input and print typed characters to Serial |
| [EspUsbHostKeyboardDump](examples/HID/EspUsbHostKeyboardDump/) | Dump parsed keyboard events and show how to handle `onKeyboard` yourself |
| [EspUsbHostKeyboardNKRO](examples/HID/EspUsbHostKeyboardNKRO/) | Host an N-key rollover keyboard and count simultaneously-held keys |
| [EspUsbHostMouse](examples/HID/EspUsbHostMouse/) | Read mouse movement and button events |
| [EspUsbHostCompositeHID](examples/HID/EspUsbHostCompositeHID/) | Handle composite HID devices such as keyboard + mouse devices |
| [EspUsbHostConsumerControl](examples/HID/EspUsbHostConsumerControl/) | Detect media keys (volume, play/pause, etc.) |
| [EspUsbHostSystemControl](examples/HID/EspUsbHostSystemControl/) | Detect system keys (power, standby, wake) |
| [EspUsbHostGamepad](examples/HID/EspUsbHostGamepad/) | Read gamepad axes, hat switch, and buttons |
| [EspUsbHostHIDVendor](examples/HID/EspUsbHostHIDVendor/) | Vendor HID input and output/feature reports |
| [EspUsbHostHIDRawDump](examples/HID/EspUsbHostHIDRawDump/) | Raw hex dump with device address (supports multiple devices) |
| [EspUsbHostDp100Power](examples/HID/EspUsbHostDp100Power/) | Read an ALIENTEK DP100 power supply: a framed protocol inside 64-byte HID reports, with CRC-16/MODBUS |
| [EspUsbHostMacroPadN3](examples/HID/EspUsbHostMacroPadN3/) | Drive a Mirabox N3 / Ajazz AKP03 family LCD macro pad: paint the six key screens and read the keys (ESP32-P4 only) |

### Info

| Sketch | Description |
|--------|-------------|
| [EspUsbHostBringUpCheck](examples/Info/EspUsbHostBringUpCheck/) | First tool on a new board: did the host start, did anything enumerate, at what speed -- with a checklist when nothing does |
| [EspUsbHostDeviceExplorer](examples/Info/EspUsbHostDeviceExplorer/) | Identify an unknown device: per-interface "what it is and which API drives it", plus raw descriptor bytes and a block-by-block walk |
| [EspUsbHostDeviceInfo](examples/Info/EspUsbHostDeviceInfo/) | Print device descriptors, interfaces, and endpoints for all connected devices |
| [EspUsbHostHIDReportDescriptor](examples/Info/EspUsbHostHIDReportDescriptor/) | Print HID report descriptors and a simple item decode for HID investigation |
| [EspUsbHostCustomDeviceCallbacks](examples/Info/EspUsbHostCustomDeviceCallbacks/) | Define custom connect/disconnect callbacks and inspect connected devices |
| [EspUsbHostHubPPPS](examples/Info/EspUsbHostHubPPPS/) | Control power on a PPPS-capable USB hub port |
| [EspUsbHostP4FsPhyRouting](examples/Info/EspUsbHostP4FsPhyRouting/) | Route ESP32-P4 USB OTG FS to the GPIO24/GPIO25 USB connector at runtime |

### MIDI

| Sketch | Description |
|--------|-------------|
| [EspUsbHostMIDI](examples/MIDI/EspUsbHostMIDI/) | USB MIDI input and output |

### Audio

| Sketch | Description |
|--------|-------------|
| [EspUsbHostAudioInput](examples/Audio/EspUsbHostAudioInput/) | Receive USB Audio isochronous IN payloads |
| [EspUsbHostAudioOutputTone](examples/Audio/EspUsbHostAudioOutputTone/) | Generate a simple tone and send it to USB Audio OUT |
| [EspUsbHostAudioOutputHardwareVolume](examples/Audio/EspUsbHostAudioOutputHardwareVolume/) | Check USB Audio Feature Unit mute and hardware volume support |
| [EspUsbHostAudioOutputMP3PCMFlow](examples/Audio/EspUsbHostAudioOutputMP3PCMFlow/) | Decode embedded MP3 assets with PCMFlow and play them to USB Audio OUT |
| [EspUsbHostAudioOutputMP3ESP8266Audio](examples/Audio/EspUsbHostAudioOutputMP3ESP8266Audio/) | Decode embedded MP3 assets with ESP8266Audio and play them to USB Audio OUT |

### Serial

| Sketch | Description |
|--------|-------------|
| [EspUsbHostUSBSerial](examples/Serial/EspUsbHostUSBSerial/) | Bidirectional serial bridge (CDC ACM and VCP) |
| [EspUsbHostMultiUSBSerial](examples/Serial/EspUsbHostMultiUSBSerial/) | Use FTDI and CP210x USB serial devices at the same time |
| [EspUsbHostDisplayTuring](examples/Serial/EspUsbHostDisplayTuring/) | Drive a 3.5-inch USB smart screen (CDC serial protocol) as a LovyanGFX panel, with LGFXVirtualCanvas for diff transfer |

### Storage

| Sketch | Description |
|--------|-------------|
| [EspUsbHostMSCBlockDump](examples/Storage/EspUsbHostMSCBlockDump/) | Print MSC capacity and dump the first block |
| [EspUsbHostMSCFatList](examples/Storage/EspUsbHostMSCFatList/) | Mount MSC as Arduino `fs::FS`, list files, and run a small write/read/delete probe |

### CCID

| Sketch | Description |
|--------|-------------|
| [EspUsbHostCcidReader](examples/Ccid/EspUsbHostCcidReader/) | Open a CCID smart card reader, report card insertion/removal, read the ATR and card type, and send the PC/SC Get UID APDU |
| [EspUsbHostCcidFelicaIdm](examples/Ccid/EspUsbHostCcidFelicaIdm/) | Read a FeliCa IDm for a chosen System Code on a Sony RC-S300: take the RF field over with a transparent session and send the FeliCa Polling frame, which is the only way to reach one system rather than whatever the reader's own wildcard poll found |

### Network

| Sketch | Description |
|--------|-------------|
| [UsbNetwork](examples/UsbNetwork/) | Bring up a CDC-NCM/ECM USB Ethernet adapter as a DHCP-client lwIP netif and run an `HTTPClient` GET over USB. Prints the CDC-ECM/NCM candidates found in every configuration on connect |

### Vendor

| Sketch | Description |
|--------|-------------|
| [EspUsbHostProtocolConsole](examples/Vendor/EspUsbHostProtocolConsole/) | Interactive console for working out an undocumented protocol: type control/bulk transfers on the serial monitor and see the answers |
| [EspUsbHostVendorBulk](examples/Vendor/EspUsbHostVendorBulk/) | Generic non-HID vendor-specific interface: bulk IN/OUT and EP0 vendor control IN/OUT |
| [EspUsbHostAdbConnect](examples/Vendor/EspUsbHostAdbConnect/) | Authenticate Android ADB and run one shell stream over the generic vendor-bulk API |
| [EspUsbHostDisplayDl1xx](examples/Vendor/EspUsbHostDisplayDl1xx/) | Drive a USB graphics adapter (DL-1xx bulk protocol) as a LovyanGFX panel, with LGFXVirtualCanvas for a Full HD surface |
| [EspUsbHostDisplayAx206](examples/Vendor/EspUsbHostDisplayAx206/) | Drive an AX206 USB photo-frame display (Bulk-Only Transport with vendor commands) as a LovyanGFX panel, streaming a whole frame per transaction with no frame buffer |
| [EspUsbHostUsbtmcScpi](examples/Vendor/EspUsbHostUsbtmcScpi/) | Talk SCPI to a USBTMC instrument (class 0xFE): class requests on EP0, the bulk message layer, and a KIKUSUI PMX power supply wrapper |
| [EspUsbHostPrinterEscPos](examples/Vendor/EspUsbHostPrinterEscPos/) | Print on an ESC/POS receipt printer (class 0x07): class requests on EP0, real-time status, and a Japanese receipt with a barcode, a QR code and the cutter |

Both USB display examples are indexed together in [docs/usb-display.md](docs/usb-display.md).

## API reference

### Core

```cpp
bool begin();
bool begin(const EspUsbHostConfig &config);
void end();
bool ready() const;
bool setConfigurationSelector(ConfigurationSelector selector);
```

`setConfigurationSelector()` is registered before `begin()` and returns the
configuration value to activate for a given device descriptor (`0` keeps the
device default). It runs on the USB Host task during enumeration and must not
block. It requires Arduino-ESP32 3.3.11 or later (`enum_filter_cb`); on older
cores it returns `false` with `ESP_ERR_NOT_SUPPORTED`. It is mainly needed by
USB Ethernet adapters that hide CDC-NCM/ECM in a non-default configuration.

`end()` synchronously unmounts any MSC volume this instance mounted, stops the
client and daemon tasks, cancels and drains in-flight endpoint transfers,
deregisters the client, waits for the IDF `ALL_FREE` handshake, and uninstalls
the USB Host Library. After it returns, the same `EspUsbHost` object can be
started again with `begin()`. Call `end()` from the application task, not from a
USB event/data callback.

`EspUsbHostConfig` lets you adjust the background task stack size, priority, and core affinity:

```cpp
struct EspUsbHostConfig {
  uint32_t    taskStackSize = 8192;
  UBaseType_t taskPriority  = 5;
  BaseType_t  taskCore      = tskNO_AFFINITY;
  EspUsbHostPort port       = ESP_USB_HOST_PORT_DEFAULT;
  EspUsbHostFifoConfig fifo = {};
};
```

On ESP32-P4, set `port` to `ESP_USB_HOST_PORT_FULL_SPEED` or `ESP_USB_HOST_PORT_HIGH_SPEED` when you need to choose a specific OTG peripheral. Other chips ignore this setting.

#### Endpoint size limits (`fifo`)

The host controller stages packets in a hardware FIFO that it splits three ways,
and the split caps how large an endpoint the host can open:

| Endpoint | Limit | High-speed port default |
| --- | --- | --- |
| IN (any transfer type) | `(rxFifoLines - 2) * 4` | ~2400 bytes |
| Control / bulk OUT | `nptxFifoLines * 4` | 1024 bytes |
| Interrupt / isochronous OUT | `ptxFifoLines * 4` | **512 bytes** |

A device with an interrupt OUT endpoint larger than 512 bytes — high-speed vendor
HID panels such as Stream Deck style macro pads use 1024 — fails to claim with
`ESP_ERR_NOT_SUPPORTED`, and the host driver logs
`HCD DWC: EP MPS (1024) exceeds supported limit (512)`. Repartition the FIFO to
make room:

```cpp
EspUsbHostConfig config;
config.port = ESP_USB_HOST_PORT_HIGH_SPEED;
config.fifo = ESP_USB_HOST_FIFO_LARGE_PERIODIC_OUT;  // {260, 128, 280} lines
usb.begin(config);
```

`ESP_USB_HOST_FIFO_LARGE_PERIODIC_OUT` allows a 1024-byte interrupt OUT endpoint
while keeping 512-byte bulk transfers usable. Set the three fields yourself for a
different balance; sizes are in lines of 4 bytes, `rxFifoLines` and
`nptxFifoLines` must be non-zero, and the total must fit the port: 1024 lines
(4 kB) on the ESP32-P4 high-speed port, 256 lines (1 kB) on any full-speed port —
which is why a 1024-byte endpoint can only be opened on the high-speed port.
Leave `fifo` at its default to keep the driver's own split. Requires
arduino-esp32 3.3.0 or newer; older cores log a warning and use the default.

### Device events

```cpp
void onDeviceConnected(DeviceCallback callback);
void onDeviceDisconnected(DeviceCallback callback);
EspUsbHostListenerId addDeviceConnectedListener(DeviceCallback callback);
EspUsbHostListenerId addDeviceDisconnectedListener(DeviceCallback callback);
bool removeListener(EspUsbHostListenerId listenerId);
void espUsbHostPrint(const EspUsbHostDeviceInfo &device, Print &out = Serial);
```

Callbacks receive `const EspUsbHostDeviceInfo &device`. Key fields: `address`, `vid`, `pid`, `product`, `manufacturer`, `serial`, `speed`, `parentAddress`, `portId`.
Use `espUsbHostPrint(device)` for a one-line summary. Add event context such as `connected:` or `disconnected:` in your callback.

`portId` identifies where the device is attached. `0x01` means the root port. For hub-attached devices, the upper nibble is the hub index assigned in detection order and the lower nibble is the hub port number, for example `0x12` means hub #1 port 2.

The listener functions follow the same contract as the [HID input](#hid-input)
listeners described below. Lifecycle has its own capacity, eight by default
(`EspUsbHost::MaxLifecycleListeners`, configurable with
`ESP_USB_HOST_MAX_LIFECYCLE_LISTENERS`), because every subsystem that tracks
devices subscribes to it, so the number needed grows with how many subsystems
are built on the stack rather than plateauing the way a single input event does.
The connect event fires for unsupported devices too, so a listener must check
`device.supported` before assuming it can talk to the device.

### HID input

```cpp
void onKeyboard(KeyboardCallback callback);
void onKeyboardState(KeyboardStateCallback callback);
void onMouse(MouseCallback callback);
void onConsumerControl(ConsumerControlCallback callback);
void onSystemControl(SystemControlCallback callback);
void onGamepad(GamepadCallback callback);
void onHIDInput(HIDInputCallback callback);    // raw — fires for all HID interfaces
void onHIDVendorInput(HIDVendorInputCallback callback);
EspUsbHostListenerId addKeyboardListener(KeyboardCallback callback);
EspUsbHostListenerId addKeyboardStateListener(KeyboardStateCallback callback);
EspUsbHostListenerId addMouseListener(MouseCallback callback);
EspUsbHostListenerId addConsumerControlListener(ConsumerControlCallback callback);
EspUsbHostListenerId addSystemControlListener(SystemControlCallback callback);
EspUsbHostListenerId addGamepadListener(GamepadCallback callback);
bool removeListener(EspUsbHostListenerId listenerId);
void espUsbHostPrint(const EspUsbHostHIDInput &input, Print &out = Serial);
void espUsbHostPrint(const EspUsbHostKeyboardEvent &event, Print &out = Serial);
const char *espUsbHostConsumerControlUsageName(uint16_t usage);
const char *espUsbHostSystemControlUsageName(uint8_t usage);
```

Each `on*()` function keeps one callback for compatibility. The matching
`add*Listener()` functions allow adapters and the sketch to receive the same
parsed HID event without replacing one another. A successful registration
returns a nonzero `EspUsbHostListenerId`; zero
(`ESP_USB_HOST_INVALID_LISTENER_ID`) means that the callback was empty or the
event's listener capacity was reached. `removeListener()` accepts an ID from any
listener type — HID input, [device lifecycle](#device-events) or
[MIDI](#midi-1) — and returns whether it removed one.

There are four listeners per event by default (`EspUsbHost::MaxListenersPerEvent`),
configurable at compile time with `ESP_USB_HOST_MAX_LISTENERS_PER_EVENT`. The
single `on*()` callback runs first, followed by listeners in registration order.
The callback set is snapshotted for each event, so adding or removing a listener
inside a callback affects the next event. Registration, replacement, and removal
are protected across the sketch and USB tasks; callbacks run without holding the
registry mutex.

```cpp
EspUsbHostListenerId adapterListener =
    usb.addKeyboardListener([](const EspUsbHostKeyboardEvent &event) {
      // adapter input path
    });

usb.onKeyboard([](const EspUsbHostKeyboardEvent &event) {
  // sketch input path; both callbacks receive the event
});

// Later, from task context:
usb.removeListener(adapterListener);
```

Both 6-key boot keyboards and N-key rollover (NKRO) keyboards are supported. NKRO
keyboards report keys as a bitmap (report protocol) so any number of keys can be
held at once; the host learns the report layout from the HID report descriptor and
decodes it automatically, so `onKeyboard` delivers the same press/release events
either way. `keyboardUsesBitmapReport(address)` reports which format was detected
(diagnostic only). See the [KeyboardNKRO](examples/HID/EspUsbHostKeyboardNKRO/) example.

`onKeyboardState` provides a single format-independent snapshot whenever a keyboard
report changes. `bitmap` contains the current state of Keyboard/Keypad usages
`0x00-0xFF`, and `changedBitmap` identifies the usages changed by that report. Use
`isDown(keycode)`, `wasPressed(keycode)`, and `wasReleased(keycode)` to query them.
Modifier keys are ordinary usages `0xE0-0xE7`, so modifier-only changes are included.
Boot, Report-ID boot, and NKRO keyboards all use this same API. Reports with no state
change do not invoke the callback.

```cpp
usb.onKeyboardState([](const EspUsbHostKeyboardState &state) {
  for (uint16_t usage = 0; usage <= 0xff; usage++) {
    uint8_t keycode = static_cast<uint8_t>(usage);
    if (state.wasPressed(keycode)) {
      // keycode includes modifiers such as Left Ctrl (0xE0)
    }
    if (state.wasReleased(keycode)) {
      // released in this report
    }
  }
});
```

Notable event fields:

Parsed HID callbacks (`onKeyboard`, `onKeyboardState`, `onMouse`, `onConsumerControl`, `onSystemControl`, `onGamepad`, `onHIDVendorInput`) all include `vid`, `pid`, `manufacturer`, `product`, `serial`, `rawData` / `rawLength` for the full input report, and `reportData` / `reportLength` for the report bytes after removing the Report ID when one is present.

| Callback | Key fields |
|----------|-----------|
| `onKeyboard` | `pressed`, `keycode`, `ascii`, `modifiers`, `address` |
| `onKeyboardState` | `bitmap`, `changedBitmap`, `modifiers`, `isDown()`, `wasPressed()`, `wasReleased()`, `address` |
| `onMouse` | `x`, `y`, `wheel`, `pan`, `buttons`, `previousButtons`, `buttonMask`, `previousButtonMask`, `buttonCount`, `moved`, `buttonsChanged`, `address` |
| `onConsumerControl` | `pressed`, `usage` (16-bit HID usage code), `address` |
| `onSystemControl` | `pressed`, `usage` (8-bit), `address` |
| `onGamepad` | `fields`, `fieldCount`, `rawData`, `reportData`, `vid`, `pid`, `address` |
| `onHIDInput` | `address`, `vid`, `pid`, `interfaceNumber`, `subclass`, `protocol`, `data`, `length` |

Common Consumer Control constants include `ESP_USB_HOST_CONSUMER_CONTROL_PLAY_PAUSE`, `ESP_USB_HOST_CONSUMER_CONTROL_MUTE`, `ESP_USB_HOST_CONSUMER_CONTROL_VOLUME_UP`, `ESP_USB_HOST_CONSUMER_CONTROL_VOLUME_DOWN`, `ESP_USB_HOST_CONSUMER_CONTROL_NEXT_TRACK`, and `ESP_USB_HOST_CONSUMER_CONTROL_PREVIOUS_TRACK`.

### HID output

```cpp
void setKeyboardLayout(EspUsbHostKeyboardLayout layout);
bool setKeyboardLeds(bool numLock, bool capsLock, bool scrollLock,
                     uint8_t address = ESP_USB_HOST_ANY_ADDRESS);
bool getKeyboardNumLock(uint8_t address = ESP_USB_HOST_ANY_ADDRESS) const;
bool getKeyboardCapsLock(uint8_t address = ESP_USB_HOST_ANY_ADDRESS) const;
bool getKeyboardScrollLock(uint8_t address = ESP_USB_HOST_ANY_ADDRESS) const;
bool sendHIDReport(uint8_t interfaceNumber, uint8_t reportType, uint8_t reportId,
                   const uint8_t *data, size_t length,
                   uint8_t address = ESP_USB_HOST_ANY_ADDRESS);
bool sendHIDVendorOutput(const uint8_t *data, size_t length,
                      uint8_t address = ESP_USB_HOST_ANY_ADDRESS);
bool sendHIDVendorFeature(const uint8_t *data, size_t length,
                       uint8_t address = ESP_USB_HOST_ANY_ADDRESS);
```

`setKeyboardLeds()` works with boot keyboards and with keyboards that only expose a
report protocol (report-ID composite HID devices, NKRO keyboards): when no boot
interface is declared, the LED output report found in the HID report descriptor is
used and the Set_Report carries the keyboard's report ID. The host also pushes its
current lock state to the keyboard once at connect.

The library owns the lock state: it toggles it on each Lock keypress and resends the
LED report, so `getKeyboardNumLock()`, `getKeyboardCapsLock()` and
`getKeyboardScrollLock()` return the current state at any time without waiting for a
callback. Lock state is per keyboard; pass an `address` when more than one is
attached. The same values are also carried on every `onKeyboard` and
`onKeyboardState` notification. All three read false when no keyboard is attached.

`sendHIDVendorOutput()` and `sendHIDVendorFeature()` are HID vendor-report helpers. For non-HID vendor-specific interfaces, use the Vendor bulk/control APIs below.

The default layout is `ESP_USB_HOST_KEYBOARD_LAYOUT_EN_US`. Pass any of the following constants to `setKeyboardLayout()`:

| Constant | Locale | Notes |
|----------|--------|-------|
| `ESP_USB_HOST_KEYBOARD_LAYOUT_ZH_TW` | zh_TW | Traditional Chinese — US QWERTY symbols |
| `ESP_USB_HOST_KEYBOARD_LAYOUT_DA_DK` | da_DK | Danish |
| `ESP_USB_HOST_KEYBOARD_LAYOUT_DE_DE` | de_DE | German QWERTZ |
| `ESP_USB_HOST_KEYBOARD_LAYOUT_EN_US` | en_US | English US (**default**) |
| `ESP_USB_HOST_KEYBOARD_LAYOUT_FI_FI` | fi_FI | Finnish |
| `ESP_USB_HOST_KEYBOARD_LAYOUT_FR_FR` | fr_FR | French AZERTY |
| `ESP_USB_HOST_KEYBOARD_LAYOUT_HU_HU` | hu_HU | Hungarian QWERTZ |
| `ESP_USB_HOST_KEYBOARD_LAYOUT_IT_IT` | it_IT | Italian |
| `ESP_USB_HOST_KEYBOARD_LAYOUT_JA_JP` | ja_JP | Japanese |
| `ESP_USB_HOST_KEYBOARD_LAYOUT_KO_KR` | ko_KR | Korean — US QWERTY symbols |
| `ESP_USB_HOST_KEYBOARD_LAYOUT_NL_NL` | nl_NL | Dutch |
| `ESP_USB_HOST_KEYBOARD_LAYOUT_NB_NO` | nb_NO | Norwegian Bokmål |
| `ESP_USB_HOST_KEYBOARD_LAYOUT_PT_BR` | pt_BR | Brazilian Portuguese ABNT2 |
| `ESP_USB_HOST_KEYBOARD_LAYOUT_SV_SE` | sv_SE | Swedish |
| `ESP_USB_HOST_KEYBOARD_LAYOUT_ZH_CN` | zh_CN | Simplified Chinese — US QWERTY symbols |
| `ESP_USB_HOST_KEYBOARD_LAYOUT_EN_GB` | en_GB | English UK |
| `ESP_USB_HOST_KEYBOARD_LAYOUT_PT_PT` | pt_PT | Portuguese (Portugal) |
| `ESP_USB_HOST_KEYBOARD_LAYOUT_ES_ES` | es_ES | Spanish |
| `ESP_USB_HOST_KEYBOARD_LAYOUT_FR_CH` | fr_CH | Swiss French QWERTZ |

`event.ascii` is a Latin-1 encoded `uint8_t` (0x00–0xFF). Dead keys (´, \`, ^, ~, ¨) and characters outside Latin-1 produce `ascii = 0`. For `ZH_TW`, `KO_KR`, and `ZH_CN`, the symbol layout is identical to `EN_US`; actual CJK character input requires an IME on the host side.

### USB serial (CDC ACM and VCP)

Low-level send API on `EspUsbHost`:

```cpp
bool sendSerial(const uint8_t *data, size_t length,
                uint8_t address = ESP_USB_HOST_ANY_ADDRESS);
bool sendSerial(const char *text,
                uint8_t address = ESP_USB_HOST_ANY_ADDRESS);
bool serialReady(uint8_t address = ESP_USB_HOST_ANY_ADDRESS) const;
bool setSerialBaudRate(uint32_t baud,
                       uint8_t address = ESP_USB_HOST_ANY_ADDRESS);
bool setSerialConfig(const EspUsbHostSerialConfig &config,
                     uint8_t address = ESP_USB_HOST_ANY_ADDRESS);
uint16_t serialOutPacketSize(uint8_t address = ESP_USB_HOST_ANY_ADDRESS) const;
```

`sendSerial()` does not wait for completion: it allocates a transfer per call and hands it to the driver. That is fine for terminal-rate traffic, but a writer that outruns the endpoint keeps growing the in-flight set until DMA memory runs out. The asynchronous CDC OUT queue is the bounded form, and it has the same shape as the vendor bulk one:

```cpp
bool serialWriteQueueBegin(size_t depth, size_t bufferBytes,
                           uint8_t address = ESP_USB_HOST_ANY_ADDRESS);
void serialWriteQueueEnd(uint8_t address = ESP_USB_HOST_ANY_ADDRESS);
bool serialWriteQueueReady(uint8_t address = ESP_USB_HOST_ANY_ADDRESS) const;

uint8_t *serialWriteAcquire(size_t *capacity, uint32_t timeoutMs = 0,
                            uint8_t address = ESP_USB_HOST_ANY_ADDRESS);
bool serialWriteSubmit(uint8_t *buffer, size_t length,
                       uint8_t address = ESP_USB_HOST_ANY_ADDRESS);
void serialWriteRelease(uint8_t *buffer, uint8_t address = ESP_USB_HOST_ANY_ADDRESS);
bool serialWriteAsync(const uint8_t *data, size_t length, uint32_t timeoutMs = 0,
                      uint8_t address = ESP_USB_HOST_ANY_ADDRESS);

size_t serialWritePending(uint8_t address = ESP_USB_HOST_ANY_ADDRESS) const;
size_t serialWriteQueueFree(uint8_t address = ESP_USB_HOST_ANY_ADDRESS) const;
bool serialWriteFlush(uint32_t timeoutMs, uint8_t address = ESP_USB_HOST_ANY_ADDRESS);
EspUsbHostSerialWriteStats serialWriteStats(uint8_t address = ESP_USB_HOST_ANY_ADDRESS) const;
void serialWriteStatsReset(uint8_t address = ESP_USB_HOST_ANY_ADDRESS);
```

`serialWriteQueueBegin()` preallocates `depth` reusable transfers of `bufferBytes` each (max depth `ESP_USB_HOST_SERIAL_WRITE_QUEUE_MAX_DEPTH`). Submits never wait, but `serialWriteAcquire()` blocks up to `timeoutMs` once the pool is busy, and that wait is the backpressure. While the queue is active `sendSerial()` and `EspUsbHostCdcSerial::write()` route through it, so existing code inherits that pacing unchanged; writes longer than the slot size still take the one-shot path. `EspUsbHostCdcSerial::flush()` waits for the queue to drain, and does nothing without it. `serialWriteFlush()` cannot be called from the USB client task, because that is where completion callbacks run.

[`examples/Serial/EspUsbHostDisplayTuring`](examples/Serial/EspUsbHostDisplayTuring/) is the motivating case: a USB display over CDC, where a frame is 300 KB and nothing but the queue keeps the writer in step with the bus.

`EspUsbHostCdcSerial` wraps the above as a standard Arduino `Stream` / `Print`:

```cpp
EspUsbHostCdcSerial CdcSerial(usb);

bool    setRxBufferSize(size_t size);
size_t  rxBufferSize() const;
bool    begin(uint32_t baud = 115200);
void    end();
bool    connected() const;
int     available();
int     read();
int     peek();
void    flush();
size_t  write(uint8_t data);
size_t  write(const uint8_t *buffer, size_t size);
bool    setBaudRate(uint32_t baud);
bool    setConfig(const EspUsbHostSerialConfig &config);
bool    setDtr(bool enable);
bool    setRts(bool enable);
void    setAddress(uint8_t address);
uint8_t address() const;
void    clearAddress();
```

Received bytes land in a ring buffer filled from the USB client task and drained by `read()`. The ring defaults to 512 bytes, and when it overflows the oldest byte is dropped silently, so anything that keeps `read()` waiting longer than the ring holds loses data — at 921600 baud 512 bytes is about 5.5 ms of traffic, and devices that burst (a GPS emitting a second of NMEA at once, a boot-time log dump) can exceed it even at a low average rate. `setRxBufferSize()` sizes the ring per instance and must be called before `begin()` (or after `end()`), because the USB client task writes into it while attached; it returns `false` if the instance is attached, if `size` is below 2, or if the allocation fails:

```cpp
void setup() {
  CdcSerial.setRxBufferSize(8192);
  CdcSerial.begin(115200);
}
```

`setRxBufferSize()` is the supported way to change this, and changing the compile-time default is **not recommended** — it needs build configuration that differs per environment (Arduino IDE, arduino-cli, PlatformIO), it applies to every instance at once, and a stale build cache makes it look like it did nothing. Use it only in sketches that cannot call `setRxBufferSize()`.

If you do need it, put the flag in a file named `build_opt.h` in the sketch folder, alongside the `.ino`. The Arduino build passes that file to every translation unit, which is what a `#define` in the `.ino` cannot do: the sketch and the library are compiled separately, so a sketch-level define would reach only one side and change the class layout there (an ODR violation that links cleanly and misbehaves at runtime).

```
-DESP_USB_HOST_CDC_RX_BUFFER_SIZE=8192
-DESP_USB_HOST_VENDOR_RX_BUFFER_SIZE=2048
```

Despite the `.h` name the file is not C source — it is handed to the compiler as a response file, so it may contain nothing but options. A `//` or `#` comment line breaks the build with `cannot specify '-o' with '-c' ... with multiple files`, because every word in it is taken as an input file name.

> **Note**: editing `build_opt.h` does not always invalidate the build cache, so the new value can appear to do nothing. Force a clean build after every change — arduino-cli: `arduino-cli compile --clean`; PlatformIO: `pio run -t clean`; Arduino IDE: restart the IDE, or delete the temporary build folder shown in the verbose compile output. Arduino IDE also only picks the file up if it exists when the compile starts, so create it before building.

`ESP_USB_HOST_VENDOR_RX_BUFFER_SIZE` (512 by default) is the vendor bulk IN ring, which has no runtime equivalent because it lives inside a library-private per-device struct. PlatformIO can pass the same flags through `build_flags` in `platformio.ini` instead — that per-environment difference is exactly why this route is discouraged.

`EspUsbHostSerialConfig` defaults to 115200 8N1. `dataBits` supports 5 to 8 bits. `parity` accepts `ESP_USB_HOST_SERIAL_PARITY_NONE`, `ODD`, `EVEN`, `MARK`, or `SPACE`. `stopBits` accepts `ESP_USB_HOST_SERIAL_STOP_BITS_1`, `1_5`, or `2`.

Use `setAddress()` inside `onDeviceConnected` to bind a specific device when multiple USB serial devices are connected.

### Vendor bulk/control

For non-HID vendor-specific interfaces (`bInterfaceClass == 0xff`) with bulk endpoints:

```cpp
void onVendorData(VendorDataCallback callback);

bool vendorOpen(uint8_t address = ESP_USB_HOST_ANY_ADDRESS,
                uint8_t interfaceNumber = 0xff,
                EspUsbHostVendorReadMode readMode = ESP_USB_HOST_VENDOR_READ_CONTINUOUS);
bool vendorWrite(const uint8_t *data, size_t length,
                 uint8_t address = ESP_USB_HOST_ANY_ADDRESS);
size_t vendorRead(uint8_t *buffer, size_t length,
                  uint8_t address = ESP_USB_HOST_ANY_ADDRESS);
bool vendorReadSync(uint8_t *buffer, size_t length,
                    size_t *actualLength = nullptr,
                    uint32_t timeoutMs = ESP_USB_HOST_VENDOR_READ_DEFAULT_TIMEOUT_MS,
                    uint8_t address = ESP_USB_HOST_ANY_ADDRESS);
uint16_t vendorOutPacketSize(uint8_t address = ESP_USB_HOST_ANY_ADDRESS) const;
uint16_t vendorInPacketSize(uint8_t address = ESP_USB_HOST_ANY_ADDRESS) const;
uint8_t vendorOutEndpoint(uint8_t address = ESP_USB_HOST_ANY_ADDRESS) const;
uint8_t vendorInEndpoint(uint8_t address = ESP_USB_HOST_ANY_ADDRESS) const;

bool vendorControlIn(uint8_t request, uint16_t value, uint16_t index,
                     uint8_t *data, size_t length,
                     size_t *actualLength = nullptr,
                     uint8_t address = ESP_USB_HOST_ANY_ADDRESS,
                     uint32_t timeoutMs = ESP_USB_HOST_VENDOR_CONTROL_DEFAULT_TIMEOUT_MS);
bool vendorControlOut(uint8_t request, uint16_t value, uint16_t index,
                      const uint8_t *data = nullptr, size_t length = 0,
                      uint8_t address = ESP_USB_HOST_ANY_ADDRESS,
                      uint32_t timeoutMs = ESP_USB_HOST_VENDOR_CONTROL_DEFAULT_TIMEOUT_MS);
bool vendorControlTransfer(uint8_t requestType, uint8_t request,
                          uint16_t value, uint16_t index,
                          uint8_t *data = nullptr, size_t length = 0,
                          size_t *actualLength = nullptr,
                          uint8_t address = ESP_USB_HOST_ANY_ADDRESS,
                          uint32_t timeoutMs = ESP_USB_HOST_VENDOR_CONTROL_DEFAULT_TIMEOUT_MS);
```

`vendorOpen()` explicitly claims an interface and starts bulk IN reception. With the default `interfaceNumber` the first vendor-specific (class 0xFF) interface is chosen; naming an interface explicitly claims it whatever its class, for devices whose bulk protocol sits behind some other class code (an AX206 USB display declares 0xDC/0xA0/0xB0, for instance). An interface already claimed by another part of the library is still refused.

The third argument selects how the bulk IN endpoint is driven. `ESP_USB_HOST_VENDOR_READ_CONTINUOUS` (the default, and the previous behaviour) keeps an IN transfer permanently outstanding and buffers what arrives for `vendorRead()`. `ESP_USB_HOST_VENDOR_READ_ON_DEMAND` starts no transfer at all and leaves the endpoint idle until `vendorReadSync()` asks, which is what a request/response protocol needs — a Bulk-Only Transport device answers only inside a transaction, and polling it outside one is a transfer error. The mode is fixed when the interface is opened; reopening it in the other mode fails rather than leaving a continuous transfer swallowing the answers `vendorReadSync()` waits for.

```cpp
usb.vendorOpen(address, interfaceNumber, ESP_USB_HOST_VENDOR_READ_ON_DEMAND);
usb.vendorWrite(request, sizeof(request), address);
size_t length = 0;
usb.vendorReadSync(response, sizeof(response), &length, 1000, address);
```

`vendorReadSync()` submits one bulk IN transfer and waits for it, so like `vendorWrite()` it cannot be called from a USB callback. The request is rounded up to a whole number of max-size packets, as the USB host requires, and only what the caller asked for is copied back. An interface with both a bulk IN and a bulk OUT endpoint is preferred; an interface that only exposes a bulk OUT endpoint is also accepted, in which case no IN transfer is started and `vendorRead()` / `onVendorData()` never produce data. USB graphics adapters, for example, pair their bulk OUT with an interrupt IN that this API does not use.

When an interface exposes several bulk endpoints in the same direction, the first one in descriptor order is selected. `vendorOutPacketSize()` / `vendorInPacketSize()` return the max packet size of the opened endpoints and `vendorOutEndpoint()` / `vendorInEndpoint()` their addresses (0 when not open). Callers need the packet size when a transfer must be terminated on a packet boundary, and the address to confirm which endpoint was chosen.

`vendorWrite()` waits for transfer completion and therefore cannot be called from USB callbacks such as `onDeviceConnected()` or `onVendorData()`; record the send request in the callback and perform it from `loop()`. `vendorRead()` is non-blocking and reads from a 512-byte per-device receive buffer. `onVendorData()` receives the same bulk IN payload as a callback; its data pointer is valid only during the callback.

`vendorControlIn()` uses `bmRequestType = 0xc0`; `vendorControlOut()` uses `bmRequestType = 0x40`. Both are vendor-type requests addressed to the device. `vendorControlTransfer()` takes the `bmRequestType` as its first argument instead, which is what a protocol layered on a non-vendor class needs: USBTMC sends its class requests as `0xa1` / `0x21` with `wIndex` set to the interface, and a standard request such as `CLEAR_FEATURE(ENDPOINT_HALT)` is `0x02` with `wIndex` set to an endpoint. The direction comes from bit 7 of `requestType`, and `actualLength` receives the bytes received on an IN transfer. Like the two typed calls it waits for completion, so it cannot be called from a USB callback, and it works before `vendorOpen()` because EP0 belongs to the device rather than to the interface. See [`examples/Vendor/EspUsbHostUsbtmcScpi`](examples/Vendor/EspUsbHostUsbtmcScpi/) for a full protocol built on it.

The asynchronous bulk OUT queue keeps several transfers in flight, so the bus does not go idle between them:

```cpp
bool vendorWriteQueueBegin(size_t depth, size_t bufferBytes,
                           uint8_t address = ESP_USB_HOST_ANY_ADDRESS);
void vendorWriteQueueEnd(uint8_t address = ESP_USB_HOST_ANY_ADDRESS);
bool vendorWriteQueueReady(uint8_t address = ESP_USB_HOST_ANY_ADDRESS) const;

uint8_t *vendorWriteAcquire(size_t *capacity, uint32_t timeoutMs = 0,
                            uint8_t address = ESP_USB_HOST_ANY_ADDRESS);
bool vendorWriteSubmit(uint8_t *buffer, size_t length,
                       uint8_t address = ESP_USB_HOST_ANY_ADDRESS);
void vendorWriteRelease(uint8_t *buffer, uint8_t address = ESP_USB_HOST_ANY_ADDRESS);
bool vendorWriteAsync(const uint8_t *data, size_t length,
                      uint8_t address = ESP_USB_HOST_ANY_ADDRESS);

size_t vendorWritePending(uint8_t address = ESP_USB_HOST_ANY_ADDRESS) const;
size_t vendorWriteQueueFree(uint8_t address = ESP_USB_HOST_ANY_ADDRESS) const;
bool vendorWriteFlush(uint32_t timeoutMs, uint8_t address = ESP_USB_HOST_ANY_ADDRESS);
EspUsbHostVendorWriteStats vendorWriteStats(uint8_t address = ESP_USB_HOST_ANY_ADDRESS) const;
void vendorWriteStatsReset(uint8_t address = ESP_USB_HOST_ANY_ADDRESS);

bool vendorWriteZlp(uint8_t address = ESP_USB_HOST_ANY_ADDRESS);
void vendorSetAutoZlp(bool enable, uint8_t address = ESP_USB_HOST_ANY_ADDRESS);
bool vendorAutoZlp(uint8_t address = ESP_USB_HOST_ANY_ADDRESS) const;
```

`vendorWriteQueueBegin()` preallocates `depth` reusable transfers of `bufferBytes` each (max depth `ESP_USB_HOST_VENDOR_WRITE_QUEUE_MAX_DEPTH`). The preferred sequence is zero-copy: `vendorWriteAcquire()` lends you a pooled DMA buffer, you write the payload into it, and `vendorWriteSubmit()` sends it. `vendorWriteAsync()` is the copying convenience form and fails if `length` exceeds the slot size rather than splitting. `vendorWriteRelease()` returns a slot you acquired but decided not to send.

None of these wait for completion, so unlike `vendorWrite()` they may be called from USB callbacks. Completion state is observed through `vendorWritePending()` / `vendorWriteFlush()` / `vendorWriteStats()`. `vendorWriteFlush()` cannot be called from the USB client task, because that is where completion callbacks run.

Measured on an ESP32-S3 (full-speed OTG) with `tests/manual/vendor_bulk_throughput`: the queue reaches 1.098 MB/s, about 90% of the 1.216 MB/s full-speed bulk ceiling, and a depth of 2 is enough to stay there at any transfer size. Synchronous `vendorWrite()` reaches the same figure only with large transfers and drops to 0.88 MB/s at 512 bytes, where per-transfer latency dominates. Depths beyond 2 did not help on full speed.

A bulk OUT transfer whose length is a multiple of the endpoint max packet size does not terminate the USB transfer by itself. `vendorSetAutoZlp(true)` makes the library append the required zero-length packet; `vendorWriteZlp()` sends one explicitly. Auto ZLP is off by default and consumes a second queue slot, so use a depth of at least 2 with it.

### CCID smart card reader

For CCID readers (`bInterfaceClass == 0x0b`, subclass `0x00`, protocol `0x00`):

```cpp
bool ccidOpen(uint8_t address = ESP_USB_HOST_ANY_ADDRESS,
              uint8_t interfaceNumber = 0xff);
void ccidClose(uint8_t address = ESP_USB_HOST_ANY_ADDRESS);
bool ccidReady(uint8_t address = ESP_USB_HOST_ANY_ADDRESS) const;
bool ccidGetInterface(EspUsbHostCcidInterface &info,
                      uint8_t address = ESP_USB_HOST_ANY_ADDRESS) const;
uint8_t ccidSlotCount(uint8_t address = ESP_USB_HOST_ANY_ADDRESS) const;

bool ccidGetStatus(EspUsbHostCcidStatus &status, uint8_t slot = 0,
                   uint8_t address = ESP_USB_HOST_ANY_ADDRESS,
                   uint32_t timeoutMs = 1000);
bool ccidCardPresent(uint8_t slot = 0, uint8_t address = ESP_USB_HOST_ANY_ADDRESS);

bool ccidPowerOn(uint8_t *atr = nullptr, size_t atrCapacity = 0,
                 size_t *atrLength = nullptr,
                 EspUsbHostCcidVoltage voltage = ESP_USB_HOST_CCID_VOLTAGE_AUTO,
                 uint8_t slot = 0, uint8_t address = ESP_USB_HOST_ANY_ADDRESS,
                 uint32_t timeoutMs = ESP_USB_HOST_CCID_DEFAULT_TIMEOUT_MS);
bool ccidPowerOff(uint8_t slot = 0, uint8_t address = ESP_USB_HOST_ANY_ADDRESS,
                  uint32_t timeoutMs = 2000);
size_t ccidGetAtr(uint8_t *buffer, size_t capacity, uint8_t slot = 0,
                  uint8_t address = ESP_USB_HOST_ANY_ADDRESS) const;
bool ccidGetCardInfo(EspUsbHostCcidCardInfo &info, uint8_t slot = 0,
                     uint8_t address = ESP_USB_HOST_ANY_ADDRESS) const;

bool ccidTransfer(const uint8_t *tx, size_t txLength,
                  uint8_t *rx, size_t rxCapacity, size_t *rxLength,
                  uint8_t slot = 0, uint8_t address = ESP_USB_HOST_ANY_ADDRESS,
                  uint32_t timeoutMs = ESP_USB_HOST_CCID_DEFAULT_TIMEOUT_MS);
bool ccidApdu(const uint8_t *apdu, size_t apduLength,
              uint8_t *response, size_t responseCapacity, size_t *responseLength,
              uint16_t *statusWord = nullptr,
              uint8_t slot = 0, uint8_t address = ESP_USB_HOST_ANY_ADDRESS,
              uint32_t timeoutMs = ESP_USB_HOST_CCID_DEFAULT_TIMEOUT_MS);
bool ccidEscape(const uint8_t *tx, size_t txLength,
                uint8_t *rx, size_t rxCapacity, size_t *rxLength,
                uint8_t slot = 0, uint8_t address = ESP_USB_HOST_ANY_ADDRESS,
                uint32_t timeoutMs = ESP_USB_HOST_CCID_DEFAULT_TIMEOUT_MS);
bool ccidMessage(uint8_t messageType, const uint8_t *messageSpecific,
                 const uint8_t *data, size_t length,
                 EspUsbHostCcidResponse &response,
                 uint8_t slot = 0, uint8_t address = ESP_USB_HOST_ANY_ADDRESS,
                 uint32_t timeoutMs = ESP_USB_HOST_CCID_DEFAULT_TIMEOUT_MS);

bool ccidAbort(uint8_t slot = 0, uint8_t address = ESP_USB_HOST_ANY_ADDRESS,
               uint32_t timeoutMs = 1000);
uint8_t ccidLastError(uint8_t address = ESP_USB_HOST_ANY_ADDRESS) const;
void onCcidCardInserted(CcidSlotChangeCallback callback);
void onCcidCardRemoved(CcidSlotChangeCallback callback);
```

`ccidOpen()` claims the interface — it is not claimed during enumeration — reads the CCID class descriptor (slot count, `dwProtocols`, `dwFeatures`, `dwMaxCCIDMessageLength`, exchange level) into `ccidGetInterface()`, allocates the per-device message buffer, and starts the interrupt IN endpoint when the reader has one. `ccidClose()` stops CCID activity and frees the buffer; the interface stays claimed until the device disconnects, so a later `ccidOpen()` reuses it.

Every call that talks to the reader is synchronous and returns `false` when called from a USB callback. The slot-change callbacks run on the USB task, so they must only record what happened and let `loop()` issue the commands.

`bSeq` is managed by the library: responses whose sequence number belongs to another command are dropped, and a response with the "time extension requested" command status is not treated as final — the library keeps waiting. Commands to one reader are serialized with a per-device mutex.

`ccidGetCardInfo()` decodes the ATR that `ccidPowerOn()` cached into the card standard (`ISO 14443 A`, `ISO 14443 B`, `ISO 15693`, `FeliCa`, low-frequency contactless, ISO 7816-10 memory card), its level, and the card name. A contactless storage card has no ATR of its own, so a PC/SC compliant reader synthesizes one whose historical bytes carry the PC/SC RID `A0 00 00 03 06` followed by a standard byte and a card name -- that is what this reads. A card that answers with an ATR of its own (a contact card, or a contactless card speaking ISO 14443-4) carries no such identification and is reported as `ISO 7816 card (own ATR)` with `pcscStorageAtr == false`. A card whose ATR has no historical bytes at all -- what a CCID reader answers for a card it did not identify -- stays `unknown` rather than being guessed at. Card names are resolved for the well-known PC/SC values; `cardName` always holds the raw code. The parser is `src/EspUsbHostCcidAtr.h`, free of Arduino and USB dependencies and covered by the `tests/unit/ccid_atr` host test.

Cards the ATR does not identify get a second chance from `ccidIdentifyCard()`, which sends Get UID and infers the standard from the identifier's shape (8 bytes = FeliCa IDm unless it starts with `0xe0`, which is an ISO 15693 UID; 7 or 10 bytes = ISO 14443 A NFCID1; 4 bytes starting with `0x08` = the random NFCID1 ISO 14443-3 reserves that prefix for; other 4-byte identifiers leave the ISO 14443 type open, since an NFCID1 and a PUPI are the same length). This is a heuristic on the identifier, not a statement by the card, and `info.fromUid` records that it was used.

Measured with a FeliCa card on the RC-S300: the reader identifies it through the PC/SC ATR (`PIX.SS = 0x11`, `PIX.Name = 0x003b`), so `ccidGetCardInfo()` alone reports `FeliCa` and Get UID returns the 8-byte IDm. An iPhone in Apple Pay mode on the same reader answers as ISO 14443 A instead, with an ATR carrying no identification and a random 4-byte NFCID1 -- that is the phone's choice, not a reader limitation.

`ccidApdu()` splits SW1SW2 off the response, so the caller's buffer only has to hold the data part. `61 xx` and `6C xx` are returned as-is rather than being followed automatically. `ccidTransfer()` is the same exchange without the split, and `ccidEscape()` / `ccidMessage()` cover reader-specific commands and any CCID message this API does not wrap.

Chained responses (`bChainParameter != 0`, extended APDU level) are reported as a failure rather than returned as a fragment. ICCD variants (interface protocol `0x01` / `0x02`) are not supported.

The message buffer is `ESP_USB_HOST_CCID_BUFFER_SIZE` (512) bytes, or `dwMaxCCIDMessageLength` when the reader reports more, up to 4096. Override the default with `-DESP_USB_HOST_CCID_BUFFER_SIZE=...`.

Verified with a Sony RC-S300 (`FeliCa Port/PaSoRi 4.0`): the reader reports one slot, T=1, extended APDU exchange level and `dwMaxCCIDMessageLength = 522`; an ISO 14443 card on it is identified as `ISO 14443 A` level 3, `MIFARE Classic 1K`, and answers the PC/SC Get UID pseudo APDU `FF CA 00 00 00` with its UID and `9000`. FeliCa's own protocol is not ISO 7816 APDU, so reading FeliCa blocks (Read Without Encryption and friends) needs reader-specific commands through `ccidEscape()`; the card type and the IDm are available through the standard path.

### MIDI

```cpp
void onMidiMessage(MidiCallback callback);   // receive
EspUsbHostListenerId addMidiMessageListener(MidiCallback callback);

bool midiReady(uint8_t address = ESP_USB_HOST_ANY_ADDRESS) const;
bool getMidiPortInfo(EspUsbHostMidiPortInfo &info,
                     uint8_t address = ESP_USB_HOST_ANY_ADDRESS) const;
bool midiSend(const uint8_t *data, size_t length,
              uint8_t address = ESP_USB_HOST_ANY_ADDRESS);
bool midiSendNoteOn(uint8_t channel, uint8_t note, uint8_t velocity,
                    uint8_t address = ESP_USB_HOST_ANY_ADDRESS);
bool midiSendNoteOff(uint8_t channel, uint8_t note, uint8_t velocity,
                     uint8_t address = ESP_USB_HOST_ANY_ADDRESS);
bool midiSendControlChange(uint8_t channel, uint8_t control, uint8_t value,
                           uint8_t address = ESP_USB_HOST_ANY_ADDRESS);
bool midiSendProgramChange(uint8_t channel, uint8_t program,
                           uint8_t address = ESP_USB_HOST_ANY_ADDRESS);
bool midiSendPolyPressure(uint8_t channel, uint8_t note, uint8_t pressure,
                          uint8_t address = ESP_USB_HOST_ANY_ADDRESS);
bool midiSendChannelPressure(uint8_t channel, uint8_t pressure,
                             uint8_t address = ESP_USB_HOST_ANY_ADDRESS);
bool midiSendPitchBend(uint8_t channel, uint16_t value,
                       uint8_t address = ESP_USB_HOST_ANY_ADDRESS);
bool midiSendPitchBendSigned(uint8_t channel, int16_t value,
                             uint8_t address = ESP_USB_HOST_ANY_ADDRESS);
bool midiSendSysEx(const uint8_t *data, size_t length,
                   uint8_t address = ESP_USB_HOST_ANY_ADDRESS);
```

`onMidiMessage` callback receives `const EspUsbHostMidiMessage &message` with fields `cable`, `codeIndex`, `status`, `data1`, `data2`.

`getMidiPortInfo()` reports how many cables (virtual MIDI ports) the device
carries, read from the descriptors at enumeration, so the count is known before
any message arrives rather than being inferred from the `cable` of a received
one.

```cpp
struct EspUsbHostMidiPortInfo
{
  uint8_t address;
  uint8_t interfaceNumber;
  uint8_t inCableCount;   // device to host
  uint8_t outCableCount;  // host to device
};
```

The counts are directions as the host sees them, and each is decoded from the
class-specific descriptor on its own bulk endpoint, so a device may report
different numbers for the two. Cable numbers run `0 .. count - 1` in each
direction and match `EspUsbHostMidiMessage::cable`; `midiSend()` takes raw bytes,
so the cable of an outgoing packet goes in the header nibble the caller writes.
A count of 0 means the descriptor for that direction is missing or unusable.

Only the first MIDI Streaming interface of a device is tracked, and within it one
bulk endpoint per direction — the same interface the send helpers and message
callbacks use. Cable names (the `iJack` strings) are not read.

`addMidiMessageListener()` registers an additional receiver under the same
contract as the [HID input](#hid-input) listeners, sharing
`ESP_USB_HOST_MAX_LISTENERS_PER_EVENT`. One bulk transfer can carry several
4-byte packets; every registered callback is invoked for each packet in turn, and
`message.raw` points into the transfer buffer, so a callback must copy anything it
keeps past its return.

### USB audio

```cpp
void onAudioData(AudioDataCallback callback);
void onAudioOutputRequest(AudioOutputCallback callback);
bool audioInputReady(uint8_t address = ESP_USB_HOST_ANY_ADDRESS) const;
bool audioInputStart(uint8_t channels,
                     uint8_t bitsPerSample,
                     uint32_t sampleRate,
                     uint8_t address = ESP_USB_HOST_ANY_ADDRESS);
bool audioInputStart(const EspUsbHostAudioStreamInfo &stream,
                     uint32_t sampleRate = 0,
                     uint8_t address = ESP_USB_HOST_ANY_ADDRESS);
bool audioOutputReady(uint8_t address = ESP_USB_HOST_ANY_ADDRESS) const;
bool setAudioSampleRate(uint32_t sampleRate,
                        uint8_t address = ESP_USB_HOST_ANY_ADDRESS);
bool audioOutputStart(uint8_t channels,
                      uint8_t bitsPerSample,
                      uint32_t sampleRate,
                      uint8_t address = ESP_USB_HOST_ANY_ADDRESS);
bool audioOutputStart(const EspUsbHostAudioStreamInfo &stream,
                      uint32_t sampleRate = 0,
                      uint8_t address = ESP_USB_HOST_ANY_ADDRESS);
void audioOutputStop(uint8_t address = ESP_USB_HOST_ANY_ADDRESS);
bool audioOutputRunning(uint8_t address = ESP_USB_HOST_ANY_ADDRESS) const;
uint32_t audioOutputUnderruns(uint8_t address = ESP_USB_HOST_ANY_ADDRESS) const;
bool audioOutputHasFeedback(uint8_t address = ESP_USB_HOST_ANY_ADDRESS) const;
uint32_t audioOutputFeedbackRate(uint8_t address = ESP_USB_HOST_ANY_ADDRESS) const;
uint32_t audioOutputFeedbackUpdates(uint8_t address = ESP_USB_HOST_ANY_ADDRESS) const;
uint32_t audioOutputFeedbackRejects(uint8_t address = ESP_USB_HOST_ANY_ADDRESS) const;
uint32_t audioOutputRate(uint8_t address = ESP_USB_HOST_ANY_ADDRESS) const;
bool audioSend(const uint8_t *data, size_t length,
               uint8_t address = ESP_USB_HOST_ANY_ADDRESS);
size_t getAudioStreams(uint8_t address, EspUsbHostAudioStreamInfo *streams,
                       size_t maxStreams) const;
size_t getAudioFeatureUnits(uint8_t address,
                            EspUsbHostAudioFeatureUnitInfo *units,
                            size_t maxUnits) const;
bool audioHasMute(uint8_t address = ESP_USB_HOST_ANY_ADDRESS,
                  uint8_t unitId = 0,
                  uint8_t channel = 0) const;
bool audioHasVolume(uint8_t address = ESP_USB_HOST_ANY_ADDRESS,
                    uint8_t unitId = 0,
                    uint8_t channel = 0) const;
bool audioGetMute(bool &mute, uint8_t address = ESP_USB_HOST_ANY_ADDRESS,
                  uint8_t unitId = 0, uint8_t channel = 0);
bool audioSetMute(bool mute, uint8_t address = ESP_USB_HOST_ANY_ADDRESS,
                  uint8_t unitId = 0, uint8_t channel = 0);
bool audioGetVolume(int16_t &volume, uint8_t address = ESP_USB_HOST_ANY_ADDRESS,
                    uint8_t unitId = 0, uint8_t channel = 0);
bool audioSetVolume(int16_t volume, uint8_t address = ESP_USB_HOST_ANY_ADDRESS,
                    uint8_t unitId = 0, uint8_t channel = 0);
bool audioGetVolumeRange(EspUsbHostAudioVolumeRange &range,
                         uint8_t address = ESP_USB_HOST_ANY_ADDRESS,
                         uint8_t unitId = 0,
                         uint8_t channel = 0);
bool audioGetVolumeDb(float &db, uint8_t address = ESP_USB_HOST_ANY_ADDRESS,
                      uint8_t unitId = 0, uint8_t channel = 0);
bool audioSetVolumeDb(float db, uint8_t address = ESP_USB_HOST_ANY_ADDRESS,
                      uint8_t unitId = 0, uint8_t channel = 0);
bool audioSetVolumeDbClamped(float db, uint8_t address = ESP_USB_HOST_ANY_ADDRESS,
                             uint8_t unitId = 0, uint8_t channel = 0);
bool audioConfigureVolume(float db, bool mute = false,
                          uint8_t address = ESP_USB_HOST_ANY_ADDRESS,
                          uint8_t unitId = 0, uint8_t channel = 0);
bool audioSetVolumePercent(uint8_t percent,
                           uint8_t address = ESP_USB_HOST_ANY_ADDRESS,
                           uint8_t unitId = 0, uint8_t channel = 0);
bool audioConfigureVolumePercent(uint8_t percent,
                                 uint8_t address = ESP_USB_HOST_ANY_ADDRESS,
                                 uint8_t unitId = 0, uint8_t channel = 0);
void espUsbHostPrint(const EspUsbHostAudioStreamInfo &stream,
                     Print &out = Serial);
bool espUsbHostAudioStreamSupportsSampleRate(const EspUsbHostAudioStreamInfo &stream,
                                             uint32_t sampleRate);
uint32_t espUsbHostAudioStreamPreferredSampleRate(const EspUsbHostAudioStreamInfo &stream,
                                                  uint32_t preferredSampleRate);
bool espUsbHostAudioStreamMatchesPcm(const EspUsbHostAudioStreamInfo &stream,
                                     uint8_t channels,
                                     uint8_t bytesPerSample,
                                     uint8_t bitsPerSample,
                                     uint32_t sampleRate);
using EspUsbHostAudioStreamFilter = bool (*)(uint32_t sampleRate,
                                             uint8_t channels,
                                             uint8_t bitsPerSample);
EspUsbHostAudioStreamSelection espUsbHostSelectAudioInputStream(
    const EspUsbHostAudioStreamInfo *streams,
    size_t count,
    EspUsbHostAudioStreamFilter filter = nullptr);
EspUsbHostAudioStreamSelection espUsbHostSelectAudioOutputStream(
    const EspUsbHostAudioStreamInfo *streams,
    size_t count,
    EspUsbHostAudioStreamFilter filter = nullptr);
```

`onAudioData` receives raw payload bytes from USB Audio streaming isochronous IN endpoints. The callback receives `const EspUsbHostAudioData &audio` with fields `address`, `interfaceNumber`, `data`, and `length`.

`onAudioOutputRequest` is the preferred USB Audio OUT API. After `audioOutputStart()`, the library drives isochronous OUT transfers and calls the callback when it needs the next PCM frames. Fill `request.data` with up to `request.frameCount` interleaved frames and set `request.writtenFrames`; any unwritten frames are sent as silence and counted as underruns. The callback runs on the USB client task, so keep it short and non-blocking; copy from an existing PCM buffer rather than doing heavy decoding in the callback.

An asynchronous playback interface runs on the device's own clock and declares an explicit feedback IN endpoint next to its data OUT endpoint to report the rate it wants to be fed at. While playback runs the library polls that endpoint and paces the OUT packets from the reported rate, so the number of frames per packet follows the device instead of drifting against it. `audioOutputHasFeedback` says whether the running stream has one, `audioOutputFeedbackRate` returns the last accepted rate (`0` when there is no feedback endpoint), and `audioOutputRate` returns the rate playback is actually paced at — the feedback rate when available, the negotiated rate otherwise. Values outside ±12.5% of the negotiated rate are ignored and counted in `audioOutputFeedbackRejects`; devices commonly report one before their buffer is primed, so a small count is normal while a rising one means the reported rates are unusable. `audioOutputFeedbackUpdates` counts the accepted ones. The callback still receives `request.sampleRate` as the negotiated rate: feedback changes how many frames a packet carries, not the format.

`getAudioStreams` reports the streaming endpoint direction, endpoint packet size, and Type I format fields when available, including discrete sample rates or a continuous sample-rate range. `protocol` is `ESP_USB_HOST_AUDIO_PROTOCOL_UAC1` or `ESP_USB_HOST_AUDIO_PROTOCOL_UAC2`; on UAC2 `terminalLink` and `clockSourceId` name the Clock Source the rates were read from. `startable` is false for a format the device advertises on an alternate setting that was not claimed — only one alternate per interface is claimed during enumeration, so a device that splits 16-bit and 24-bit (or different sample rates) across alternates reports the others as format information only. Those streams are skipped by the selection helpers and rejected by `audioInputStart()` / `audioOutputStart()`. `espUsbHostSelectAudioInputStream` and `espUsbHostSelectAudioOutputStream` apply an optional `(sampleRate, channels, bitsPerSample)` filter, then score the remaining candidates. The default scoring prefers 48 kHz, then 44.1 kHz, 16-bit PCM, and stereo when available. `audioInputStart(channels, bitsPerSample, sampleRate)` and `audioOutputStart(...)` take `0` for any argument that has no preference: `(0, 0, 0)` starts the best format the device offers, `(2, 0, 48000)` means 48 kHz stereo at whatever sample width it has. Specified arguments must match exactly, and among equally matching alternates the highest-scoring one is chosen rather than the first in descriptor order. `setAudioSampleRate` sets the sampling frequency: the endpoint request on UAC1, the Clock Source `SAM_FREQ` control on UAC2. `audioSend` remains as a low-level API for manually submitting raw PCM payload bytes to a USB Audio streaming isochronous OUT endpoint.

`getAudioFeatureUnits` reports parsed Audio Control Feature Units, with `protocol` and `controlSize` telling which layout they came from (UAC1 packs one bit per control at the stride in `bControlSize`, UAC2 two bits per control at a fixed 4-byte stride). `audioGetMute`, `audioSetMute`, `audioGetVolume`, `audioSetVolume`, and the dB/range helpers use class-specific Feature Unit requests, picking the request codes for the device's class revision (UAC1 `GET_CUR` / `GET_MIN` / `GET_MAX` / `GET_RES`, UAC2 `CUR` / `RANGE`). `audioSetVolumeDbClamped` applies the device min/max/resolution when the range is available. `audioConfigureVolume` is the simple playback helper: it unmutes/mutes when mute is supported and sets clamped dB volume when volume is supported. The percent helpers treat `1..100` as a PCM amplitude ratio (`20 * log10(percent / 100)`) and round to the device step after clamping to min/max; `0` mutes when mute is supported, or falls back to minimum volume. `unitId=0` selects the first Feature Unit that exposes the requested control. `channel=0` means master; channel values starting at 1 address per-channel controls. Raw volume values are signed 1/256 dB units.

#### Audio scope

The audio support targets **Type I PCM** streaming on **UAC1 (Audio Class 1.0)** and **UAC2 (Audio Class 2.0)**:

- **Supported:** isochronous IN/OUT streaming, Type I format parsing and sample-rate selection, and the **Feature Unit** Mute / Volume controls (get/set, range, dB and percent helpers). On UAC2 this includes the **Clock Source** entity behind an interface's `bTerminalLink` — the supported rates come from a `SAM_FREQ` `RANGE` request because UAC2 descriptors do not carry them — the 4-byte / 2-bit `bmaControls` layout, and the single `RANGE` volume request. The explicit feedback endpoint of an asynchronous playback interface is kept out of the stream list and instead polled while playback runs, so the OUT packets are paced at the rate the device asks for.
- **Not supported:** **Clock Selector / Clock Multiplier** entities; other Audio Control units — **Mixer / Selector / Processing Unit** — and Feature Unit controls beyond Mute/Volume (Bass, Mid, Treble, Automatic Gain, Delay, etc.). Devices that require these to start streaming may enumerate but not stream.
- **Bus speed:** the ESP32-S3 / ESP32-S2 host is full speed only. Many UAC2 devices are high-speed designs; one that has no full-speed configuration cannot be enumerated at all, and a full-speed isochronous endpoint is capped at 1023 bytes per frame (48 kHz and 96 kHz stereo fit, 192 kHz stereo does not).

UAC1 audio OUT and IN are peer-verified with the standard Arduino `USBAudioCard`, and UAC2 with an `EspUsbDevice` peer (`tests/peer/usb_audio_uac2`). Validation against real USB microphones and audio interfaces is still limited.

### USB Mass Storage

```cpp
bool mscReady(uint8_t address = ESP_USB_HOST_ANY_ADDRESS) const;
bool mscInquiry(EspUsbHostMscInquiry &inquiry,
                uint8_t address = ESP_USB_HOST_ANY_ADDRESS,
                uint32_t timeoutMs = ESP_USB_HOST_MSC_DEFAULT_TIMEOUT_MS);
bool mscRequestSense(EspUsbHostMscSense &sense,
                     uint8_t address = ESP_USB_HOST_ANY_ADDRESS,
                     uint32_t timeoutMs = ESP_USB_HOST_MSC_DEFAULT_TIMEOUT_MS);
bool mscLastSense(EspUsbHostMscSense &sense,
                  uint8_t address = ESP_USB_HOST_ANY_ADDRESS);
bool mscMaxLun(uint8_t &maxLun,
               uint8_t address = ESP_USB_HOST_ANY_ADDRESS,
               uint32_t timeoutMs = ESP_USB_HOST_MSC_DEFAULT_TIMEOUT_MS);
bool mscSelectLun(uint8_t lun,
                  uint8_t address = ESP_USB_HOST_ANY_ADDRESS,
                  uint32_t timeoutMs = ESP_USB_HOST_MSC_DEFAULT_TIMEOUT_MS);
bool mscGetBlockDeviceInfo(EspUsbHostMscBlockDeviceInfo &info,
                           uint8_t address = ESP_USB_HOST_ANY_ADDRESS,
                           uint32_t timeoutMs = ESP_USB_HOST_MSC_DEFAULT_TIMEOUT_MS);
bool mscTestUnitReady(uint8_t address = ESP_USB_HOST_ANY_ADDRESS,
                      uint32_t timeoutMs = ESP_USB_HOST_MSC_DEFAULT_TIMEOUT_MS);
bool mscWaitReady(uint8_t address = ESP_USB_HOST_ANY_ADDRESS,
                  uint32_t readyTimeoutMs = ESP_USB_HOST_MSC_DEFAULT_TIMEOUT_MS,
                  uint32_t commandTimeoutMs = ESP_USB_HOST_MSC_DEFAULT_TIMEOUT_MS);
bool mscCapacity64(uint64_t &blockCount, uint32_t &blockSize,
                   uint8_t address = ESP_USB_HOST_ANY_ADDRESS,
                   uint32_t timeoutMs = ESP_USB_HOST_MSC_DEFAULT_TIMEOUT_MS);
bool mscCapacity(uint32_t &blockCount, uint32_t &blockSize,
                 uint8_t address = ESP_USB_HOST_ANY_ADDRESS,
                 uint32_t timeoutMs = ESP_USB_HOST_MSC_DEFAULT_TIMEOUT_MS);
bool mscReadBlocks(uint32_t lba, uint8_t *data, uint32_t blockCount,
                   uint8_t address = ESP_USB_HOST_ANY_ADDRESS,
                   uint32_t timeoutMs = ESP_USB_HOST_MSC_DEFAULT_TIMEOUT_MS);
bool mscWriteBlocks(uint32_t lba, const uint8_t *data, uint32_t blockCount,
                    uint8_t address = ESP_USB_HOST_ANY_ADDRESS,
                    uint32_t timeoutMs = ESP_USB_HOST_MSC_DEFAULT_TIMEOUT_MS);
bool mscReadBlocks64(uint64_t lba, uint8_t *data, uint32_t blockCount,
                     uint8_t address = ESP_USB_HOST_ANY_ADDRESS,
                     uint32_t timeoutMs = ESP_USB_HOST_MSC_DEFAULT_TIMEOUT_MS);
bool mscWriteBlocks64(uint64_t lba, const uint8_t *data, uint32_t blockCount,
                      uint8_t address = ESP_USB_HOST_ANY_ADDRESS,
                      uint32_t timeoutMs = ESP_USB_HOST_MSC_DEFAULT_TIMEOUT_MS);
bool mscSynchronizeCache(uint8_t address = ESP_USB_HOST_ANY_ADDRESS,
                         uint32_t timeoutMs = ESP_USB_HOST_MSC_DEFAULT_TIMEOUT_MS);
bool mscMount(const char *basePath = "/usb",
              uint8_t address = ESP_USB_HOST_ANY_ADDRESS,
              uint8_t lun = 0,
              uint8_t maxFiles = 4,
              uint32_t timeoutMs = ESP_USB_HOST_MSC_DEFAULT_TIMEOUT_MS,
              bool skipSyncCache = false);
bool mscUnmount(const char *basePath = "/usb");
bool mscMounted(const char *basePath = "/usb") const;

class EspUsbHostMscFS : public fs::FS {
public:
  bool begin(EspUsbHost &host,
             const char *basePath = "/usb",
             uint8_t address = ESP_USB_HOST_ANY_ADDRESS,
             uint8_t lun = 0,
             uint8_t maxFiles = 4,
             uint32_t timeoutMs = ESP_USB_HOST_MSC_DEFAULT_TIMEOUT_MS,
             bool skipSyncCache = false);
  void end();
  bool mounted() const;
  const char *basePath() const;
  void setSkipSyncCache(bool skip);
  bool skipSyncCache() const;
};
```

MSC support covers SCSI transparent / Bulk-Only Transport block I/O, ESP-IDF FatFs/VFS mounting, and an Arduino `fs::FS` / `File` compatible wrapper.

For normal file access, use `EspUsbHostMscFS`. It derives from `fs::FS`, so it can be passed to Arduino libraries such as WebServer or Update that accept `fs::FS &`. `EspUsbHostMscFS::begin()` returns `false` while no MSC device is connected or the media is not ready, so retry it at a low rate from `loop()`. Use `mounted()` to check whether the filesystem is already mounted.

The low-level APIs such as `mscReadBlocks64()`, `mscWriteBlocks64()`, `mscInquiry()`, and `mscRequestSense()` are intended for capacity reporting, block dumps, diagnostics, and custom filesystem implementations. For normal FAT file access, sketches do not need to call `mscReady()`, `mscWaitReady()`, or `mscGetBlockDeviceInfo()` directly.

The block APIs support 64-bit LBA, but the current FatFs/VFS mount path is limited by the ESP-IDF FatFs build to 32-bit sectors. Multiple MSC devices and multiple LUNs can be addressed by API parameters, but ESP32-S3 has tight HCD channel limits, so assume a single MSC device for practical use. Multiple MSC devices remain an ESP32-P4-oriented validation item.

Do not call these MSC APIs from USB callbacks, because they wait for USB transfer completion. Removing a USB drive while files are open or writes are in progress may lose unwritten data. After reconnecting media, call `begin()` again.

A mount owns a FatFs drive slot and a registered VFS path, and there are only `CONFIG_FATFS_VOLUME_COUNT` of them, so it has to be released as well as the USB side. `EspUsbHost::end()` unmounts whatever this instance still has mounted, and a disconnect unmounts that device's volumes, so neither leaves a mount behind that a later `mscMount()` of the same `basePath` would be refused for. An `EspUsbHostMscFS` whose volume was dropped that way reports `mounted() == false` and remounts on the next `begin()`.

Some non-compliant MSC devices stall or disconnect when they receive SCSI `SYNCHRONIZE CACHE(10)` during FatFs sync. Whenever `SYNCHRONIZE CACHE(10)` fails — through FatFs `CTRL_SYNC` or a direct `mscSynchronizeCache()` call — the library clears the resulting bulk-pipe halt, remembers the failure for that device, and skips the command for the rest of that mount and for later `mscMount()` calls until the device is reconnected. For known-problem devices, call `usbMassStorage.setSkipSyncCache(true)` before `begin()`, or pass `skipSyncCache = true` to `begin()` / `mscMount()` to skip it from the start. This improves compatibility but relies on normal write completion instead of an explicit media flush. The flush `mscUnmount()` issues is best effort: a device that rejects it is logged and marked, but the unmount still succeeds and `lastError()` is left as it was, since the volume is being dropped either way.

### USB Hub

```cpp
bool getHubInfo(uint8_t hubAddress, EspUsbHostHubInfo &hub);
bool getHubPortStatus(uint8_t hubAddress, uint8_t port,
                      uint16_t &status, uint16_t &change);
bool setHubPortPower(uint8_t hubAddress, uint8_t port, bool enable);
```

USB Hub support covers detection, simple topology reporting, hub descriptor queries, port status queries, and port power on/off for PPPS-capable hubs. Use `EspUsbHostDeviceInfo::isHub` to identify hub devices. For devices behind a hub, `parentAddress` and `portId` report the hub/port path used for display.

`getHubInfo()` fetches the hub descriptor and fills `EspUsbHostHubInfo` with port count, PPPS/ganged/no power switching, over-current mode, power-on-to-power-good time, and related fields. `getHubPortStatus()` returns current status and change bits for a downstream port. `setHubPortPower()` sends the hub class request to enable or disable port power.

`setHubTrackingEnabled(false)` makes the library leave external hubs completely alone. Tracking a hub means opening it as a client device and keeping the handle, which is what the three calls above and the hub's own connect/disconnect events are built on; with tracking off, devices behind a hub still enumerate and work but the hub does not appear in `getDevices()` and those calls stop working. Default is on.

It is not a fix for a misbehaving hub, and one case is worth knowing about: a CH335F hub (`1a86:8094`) with an ALIENTEK DP100 behind it crashes ESP-IDF v5.5.5's own hub driver with tracking off as well, before the hub reaches the host stack's address list. The combinations tested, and what `tests/probe/hub_enum` established, are recorded in [tests/manual/README.md](tests/manual/README.md#hub-combinations-that-crash-esp-idf).

Per-port power control is only safe when the hub reports PPPS (Per-Port Power Switching). On ganged-power hubs, a port power request may affect multiple ports or the whole hub. USB 3.x hubs and products implemented internally as cascaded hubs can be more complex, so a self-powered USB 2.0 hub is recommended for validation.

This is not a complete hub class driver. It exposes user-facing information and explicit port power control. Clearing port change bits, cascaded hub behavior, USB 3.x hub compatibility, and ESP32-P4 FS/HS differences remain validation items. Do not call these APIs from USB callbacks, because they wait for USB transfer completion.

### USB network (CDC-NCM / CDC-ECM)

```cpp
size_t getNetworkInterfaces(uint8_t address,
                            EspUsbHostNetworkInterfaceInfo *interfaces,
                            size_t maxInterfaces);
bool networkOpen(uint8_t address = ESP_USB_HOST_ANY_ADDRESS);
bool networkOpen(const EspUsbHostNetworkInterfaceInfo &network);
void networkClose(uint8_t address = ESP_USB_HOST_ANY_ADDRESS);
bool networkReady(uint8_t address = ESP_USB_HOST_ANY_ADDRESS) const;
bool networkLinkUp(uint8_t address = ESP_USB_HOST_ANY_ADDRESS) const;

// Raw Ethernet frames
void onNetworkFrame(NetworkFrameCallback callback);
bool   networkWriteFrame(const uint8_t *frame, size_t length, uint8_t address = ESP_USB_HOST_ANY_ADDRESS);
size_t networkReadFrame(uint8_t *buffer, size_t length, uint8_t address = ESP_USB_HOST_ANY_ADDRESS);

// lwIP (esp_netif) integration
bool      networkAttachNetif(const EspUsbHostNetworkConfig &config = EspUsbHostNetworkConfig(),
                             uint8_t address = ESP_USB_HOST_ANY_ADDRESS);
bool      networkDetachNetif(uint8_t address = ESP_USB_HOST_ANY_ADDRESS);
IPAddress networkLocalIP(uint8_t address = ESP_USB_HOST_ANY_ADDRESS) const;
bool      networkStats(EspUsbHostNetworkStats &stats, uint8_t address = ESP_USB_HOST_ANY_ADDRESS) const;
```

`networkAttachNetif()` opens the CDC-NCM/ECM interface (if not already open) and
registers it as an `esp_netif` interface, so standard Arduino networking
(`NetworkClient`, `HTTPClient`) runs over the USB NIC. `EspUsbHostNetworkConfig`
defaults to a DHCP client; set `dhcpClient=false` and fill
`ip`/`gateway`/`subnet`(`/dns1`/`dns2`) for a static address. For raw Ethernet
frames instead of an IP stack, use `onNetworkFrame()` / `networkWriteFrame()` /
`networkReadFrame()` and do not attach a netif. CDC-NCM is preferred over
CDC-ECM when a device offers both.

`getNetworkInterfaces()` walks *every* configuration (`usb_host_get_config_desc()`)
and reports each candidate with its `configurationValue`, while `networkOpen()`
only accepts a candidate in the **active** configuration. An adapter whose
network function is not in its default configuration therefore needs
`setConfigurationSelector()` and two enumeration passes — see
[examples/UsbNetwork/](examples/UsbNetwork/) for the details and for how to
automate the second pass.

lwIP integration requires `esp_netif` in the build (it is present in the standard
Arduino-ESP32 core). Without it, `networkAttachNetif()` returns `false` and the
raw frame API can still be used. Call these APIs from the application task, not
from a USB callback.

**NTB size negotiation (CDC-NCM).** An NCM device packs Ethernet datagrams into
NTBs and decides for itself how large those get, up to the `dwNtbInMaxSize` it
advertises — unless the host lowers it. Since devices only start batching several
datagrams per NTB once traffic picks up, a receive buffer that is merely "big
enough for one frame" works in light testing and then loses whole NTBs under
load, which looks like severe packet loss on an otherwise healthy link. So at
open time the library reads `GET_NTB_PARAMETERS`, and either caps the device with
`SET_NTB_INPUT_SIZE` (when `bmNetworkCapabilities` bit 3 says it is supported) or
sizes its own buffer after the device's maximum, up to
`ESP_USB_HOST_NETWORK_NTB_IN_LIMIT` (16 KB, overridable with `-D`). The result is
reported as `EspUsbHostNetworkStats::ntbInSize`; it is always a multiple of the
bulk IN endpoint's max packet size, because ESP-IDF specifies IN transfer lengths
as integer multiples of MPS (which is why a high-speed link uses 3072 rather than
3200). `rxOversized` counts NTBs dropped for exceeding that size: it stays 0
unless a device sends more than it advertised, and a non-zero value is the thing
to look at when throughput is poor while `linkUp` and `txFails` look fine.

### Device discovery

```cpp
size_t deviceCount() const;
size_t getDevices(EspUsbHostDeviceInfo *devices, size_t maxDevices) const;
bool   getDevice(uint8_t address, EspUsbHostDeviceInfo &device) const;
size_t getInterfaces(uint8_t address, EspUsbHostInterfaceInfo *interfaces,
                     size_t maxInterfaces) const;
size_t getEndpoints(uint8_t address, EspUsbHostEndpointInfo *endpoints,
                    size_t maxEndpoints) const;
size_t endpointChannelCount(uint8_t address = ESP_USB_HOST_ANY_ADDRESS) const;
size_t managedEndpointCount(uint8_t address = ESP_USB_HOST_ANY_ADDRESS) const;
size_t ep0ChannelCount(uint8_t address = ESP_USB_HOST_ANY_ADDRESS) const;
size_t hubEndpointChannelCount(uint8_t address = ESP_USB_HOST_ANY_ADDRESS) const;
size_t estimatedHcdChannelCount(uint8_t address = ESP_USB_HOST_ANY_ADDRESS) const;
size_t maxEndpointChannelCount() const;
void   espUsbHostPrint(const EspUsbHostInterfaceInfo &interface,
                       Print &out = Serial);
void   espUsbHostPrint(const EspUsbHostEndpointInfo &endpoint,
                       Print &out = Serial);
void   printDeviceInfo(uint8_t address, bool includeHubInfo = false,
                       Print &out = Serial);
void   printAllDeviceInfo(Print &out = Serial);
```

Array size constants: `ESP_USB_HOST_MAX_DEVICES`, `ESP_USB_HOST_MAX_INTERFACES`, `ESP_USB_HOST_MAX_ENDPOINTS`.
`endpointChannelCount()` is based on endpoints in successfully claimed interfaces. `managedEndpointCount()` counts endpoints with persistent receive transfers managed by this library. `estimatedHcdChannelCount()` is an experimental estimate: tracked devices as EP0/control pipes + claimed endpoints + hub descriptor endpoints.

### Error handling

```cpp
int         lastError() const;
const char *lastErrorName() const;
```

## Design

**Callbacks over inheritance.** Register lambdas or functions with `onKeyboard()`, `onMouse()`, etc. The old pattern of subclassing `EspUsbHost` and overriding virtual methods is not the primary API.

**Breaking changes are accepted in 2.x.** The library prioritises a clean Arduino-oriented API over backwards compatibility with its earlier inheritance-based interface. Even within the 2.x series, APIs may change when examples or real devices show a better shape.

**HID gamepad reports are exposed as mappable data.** `onGamepad()` reports descriptor-decoded fields plus raw/report bytes. It does not assign semantic names such as left stick X/Y or right stick X/Y because those fields vary by device and may be 8-bit, 12-bit, 16-bit, or packed. Use `vid` / `pid`, `fields`, `rawData`, and `reportData` to build the mapping that matches your controller.

**Non-goals:**
- Fully automatic interpretation of all HID report descriptors from the first implementation
- Implementing all USB classes in the first release
- ESP-IDF HID Host Driver API compatibility
- Exposing all USB spec internals directly as Arduino APIs

## Multiple devices

For multi-device use, a self-powered USB 2.0 hub is recommended. Cheap bus-powered hubs may fail to enumerate devices or become unstable when devices draw current. Many USB 3.x hubs also expose more complex internal topology or behavior that is not a good starting point for this library.

Prefer hubs with up to 4 downstream ports. Hubs with many ports are often implemented internally as cascaded hubs, which increases topology depth and resource use. ESP32-S3 has only 8 USB host channels, and some USB devices consume multiple channels for their interfaces/endpoints, so multi-device setups can hit the channel limit quickly.

All send APIs and `EspUsbHostCdcSerial` default to `ESP_USB_HOST_ANY_ADDRESS`, which targets the first available device of the appropriate class. Pass an explicit `address` to target the currently connected device.

Device identification fields have different stability:

| Field | Use |
|-------|-----|
| `address` | Current USB address. Use it for API calls such as `setAddress()`, `sendSerial()`, `midiSend()`, or `setKeyboardLeds()`. It can change after unplug/replug. |
| `portId` | Physical/topology location. Use it to remember "the device on this port" across reconnects. |
| `vid` / `pid` | Device type/model identification. Use it to select supported chip families or products. |
| `manufacturer` / `product` | Human-readable USB string descriptors. Useful for logs and UI, but not guaranteed unique. |
| `serial` | USB serial-number string descriptor when the device provides one. It may be empty. Use it for per-unit identity when available. |

```cpp
usb.onDeviceConnected([](const EspUsbHostDeviceInfo &device) {
  if (device.vid == 0x0403) {
    CdcSerial.setAddress(device.address);
  }
});
```

## Tests

- [`tests/peer/`](tests/peer/) — two-board USB tests using an ESP32-S3 as the device peer. The peer side mainly uses the Arduino-ESP32 standard USB Device implementation to check basic Host interoperability
- [`tests/loopback/`](tests/loopback/) — single-board loopback tests. Practical ESP32-P4 loopback coverage is currently being developed in the sibling `EspUsbDevice` library
- [`tests/manual/`](tests/manual/) — manual tests for special hardware and human verification

See [tests/README.md](tests/README.md) for setup instructions.

The manually triggered _Library Footprint Matrix_ workflow uses the Arduino core
pinned by a representative example's `sketch.yaml` and builds fixed Base, HID,
Serial, Audio, Storage, MIDI, Vendor, Network, and Info probes against each
selected library release. It overwrites one canonical normalized Flash/static-RAM
JSON and Markdown report, while retaining compiler logs, ELF, map, and application
bin files as short-lived workflow artifacts. This is separate from the core
compatibility matrix, which varies the Arduino core to test build compatibility
rather than library-version size trends.

## Release checklist

1. **Clean working tree** — confirm `git status` shows no uncommitted changes
2. **Update dependencies** — use the [vscode-arduino-cli-wrapper](https://marketplace.visualstudio.com/items?itemName=tanakamasayuki.vscode-arduino-cli-wrapper) _sketch.yaml Versions_ feature to check all `sketch.yaml` files for outdated board/library versions; update to the latest and re-run steps 3–5 if anything changed
3. **Build check** — use _Build Check_ in vscode-arduino-cli-wrapper, or run `python tools/build_check.py <profile>` to compile every example that declares that sketch.yaml profile (e.g. `python tools/build_check.py esp32s2`). Minimum: `examples/` with the `esp32s3` profile; add all profiles if the change touches ESP32-P4 support. Also build the `esp32s2` profile when a change grows static RAM use (the S2 has far less internal RAM; `ESP_USB_HOST_MAX_DEVICES` defaults lower there), so `dram0_0_seg overflowed` regressions are caught early. The `UsbNetwork` example is intentionally excluded from the S2 matrix.
4. **Automated tests** — all `peer/` or `loopback/` tests pass
5. **Manual tests** — run tests related to the change (check `tests/.pytest-results/state.json` for last-run timestamps); not mandatory but strongly recommended
6. **CHANGELOG** — verify the entry for this release is accurate and complete
7. **Documentation** — confirm that API reference, examples, and README reflect the change
8. **Release** — trigger the GitHub Actions release workflow (`workflow_dispatch`, default: patch bump). The workflow will:
   - Bump the version in `library.properties` (major / minor / patch selectable)
   - Commit and push the version bump to the default branch
   - Create a `release` branch with `tests/` removed
   - Build a `.zip` archive of the library
   - Extract release notes from `CHANGELOG.md`
   - Create a GitHub release with the archive and release notes

## License

MIT License. See [LICENSE](LICENSE).
