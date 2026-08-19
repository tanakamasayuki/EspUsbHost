# Tested Devices and Boards

> 日本語版: [tested-devices.ja.md](tested-devices.ja.md)

Every device this repository has verified on real hardware, in one place. Use it to decide what to buy, and to tell "my device is unusual" apart from "my setup is wrong".

The information comes from the README, the example READMEs, the `tests/manual` catalog, and the protocol notes under `docs/`. **A device missing from this list is not a device that fails** — the list only records units someone actually put on a bench.

## How to read it

| Symbol | Meaning |
|--------|---------|
| ✅ | Verified on real hardware |
| ⚠️ | Works with conditions (speed, connection, or configuration constraints) |
| ❌ | Confirmed not to work |

Notes:

- Unless stated otherwise, verification was done on an **ESP32-S3 (full speed)**.
- **The same VID:PID with different firmware can behave differently.** White-label products (the macro pads below) ship one design under many IDs.
- For what to check first with an unknown device, see the [USB Host Development Guide](usb-host-guide.md).

---

## Input devices (HID)

| Device | VID:PID | Status | What was verified | Reference |
|--------|---------|--------|-------------------|-----------|
| USB keyboards (generic) | — | ✅ | Boot protocol, NKRO (bitmap reports), LED control. The international layout conversion is covered by host unit tests | [`keyboard_leds`](../tests/manual/keyboard_leds/) / [`tests/unit/keymap`](../tests/unit/) / [HID](../examples/HID/) |
| USB mice (generic) | — | ✅ | Movement, buttons, wheel | [HID](../examples/HID/) |
| Keyboard and mouse together | — | ✅ | Both deliver events independently | [`multi_hid_keyboard_mouse`](../tests/manual/multi_hid_keyboard_mouse/) |
| Gamepads | — | ✅ | Axes, hat switch, buttons, decoded from the report descriptor | [`EspUsbHostGamepad`](../examples/HID/EspUsbHostGamepad/) |
| ALIENTEK DP100 power supply | `2e3c:af01` | ⚠️ | A private protocol inside HID reports. Reads verified; the setpoint write is implemented. The manual tests assume a **direct connection**; behind a hub it depends on the hub — a reboot loop with a CH335F, fine with an RTD5411 (see the hub section) | [`EspUsbHostDp100Power`](../examples/HID/EspUsbHostDp100Power/) / [dp100-spec.ja.md](dp100-spec.ja.md) |
| STREONOR S6 LCD macro pad | `1500:3006` | ⚠️ | Six key screens, brightness, key input. **ESP32-P4 high-speed port only** (1024-byte interrupt OUT) | [`EspUsbHostMacroPadN3`](../examples/HID/EspUsbHostMacroPadN3/) |

The macro pad is one design sold under many brands: Mirabox Stream Dock N3 (`6602:1000`, `6602:1002`, `6603:1002`, `6603:1003`), the Ajazz AKP03 family (`0300:300x`), and unbranded units (`1500:3001`). The example matches on **interface shape** rather than on VID:PID.

---

## USB serial (CDC / VCP)

Verified with TX and RX shorted for loopback.

| Chip | VID:PID | Status | Reference |
|------|---------|--------|-----------|
| Generic CDC-ACM | — | ✅ | [`EspUsbHostUSBSerial`](../examples/Serial/EspUsbHostUSBSerial/) |
| FTDI (FT232R etc.) | VID `0x0403` | ✅ | [`vcp_ftdi`](../tests/manual/vcp_ftdi/) |
| CP210x (CP2102 etc.) | VID `0x10C4` | ✅ | [`vcp_cp210x`](../tests/manual/vcp_cp210x/) |
| CH34x (CH340 etc.) | VID `0x1A86` | ✅ | [`vcp_ch34x`](../tests/manual/vcp_ch34x/) |
| PL2303 | `067B:2303` | ✅ | [`vcp_pl2303`](../tests/manual/vcp_pl2303/) |
| PL2303GS | `067B:23A3` | ✅ | [`vcp_pl2303gs`](../tests/manual/vcp_pl2303gs/) |
| An ESP32 board as the reset target | — | ✅ | [`esp32_autoreset`](../tests/manual/esp32_autoreset/) — DTR/RTS reset and ROM download mode |

Using several at once depends on the hub and the channel budget. Measured through a hub on an ESP32-S3: **FTDI + CP210x works, FTDI + CH34x fails** on channel exhaustion ([`multi_serial`](../tests/manual/multi_serial/)).

---

## Storage (MSC)

| Device | Status | What was verified | Reference |
|--------|--------|-------------------|-----------|
| USB flash drives (generic) | ⚠️ | Capacity, LBA reads, FatFs/VFS mounting, read/write/delete through POSIX and `fs::FS` | [`msc_block`](../tests/manual/msc_block/) |
| USB flash drives, hot unplug | ⚠️ | The same path mounts again after a reconnect | [`msc_hotplug_mount`](../tests/manual/msc_hotplug_mount/) |

The ⚠️ is because MSC as a whole is experimental. Multiple MSC devices, multiple LUNs, unusual block sizes and abnormal BOT recovery are not sufficiently validated. Non-compliant devices may need the `SYNCHRONIZE CACHE(10)` fallback.

On the ESP32-P4, CPU cache versus USB DMA coherency is a real hazard, so it has its own test ([`msc_cache_coherency`](../tests/manual/msc_cache_coherency/)). The library handles it; no application-side workaround is needed.

---

## Smart card readers (CCID)

| Device | VID:PID | Status | What was verified | Reference |
|--------|---------|--------|-------------------|-----------|
| Sony RC-S300 (FeliCa Port/PaSoRi 4.0) | `054c:0dc8` | ✅ | Open, class descriptor, slot status, power-on with ATR, APDU exchange, card insert/remove notifications | [`Ccid`](../examples/Ccid/) / [ccid-api-spec.ja.md](ccid-api-spec.ja.md) |
| Sony RC-S300 with a FeliCa card | as above | ✅ | Transparent session, Polling for a chosen System Code, IDm | [`EspUsbHostCcidFelicaIdm`](../examples/Ccid/EspUsbHostCcidFelicaIdm/) |

ICCD variants, chained (extended APDU) responses and PIN-pad features are out of scope.

---

## Audio and MIDI

| Device | Status | What was verified | Reference |
|--------|--------|-------------------|-----------|
| USB MIDI devices | ✅ | MIDI in and out, cable (virtual port) counts | [`EspUsbHostMIDI`](../examples/MIDI/EspUsbHostMIDI/) |
| Arduino `USBAudioCard` peer (UAC1) | ⚠️ | Isochronous IN and OUT streaming | [Audio](../examples/Audio/) |
| EspUsbDevice peer (UAC2) | ⚠️ | Descriptors, Clock Source sample rates, Feature Unit mute/volume, IN/OUT streaming | as above |

**Real USB microphones and audio interfaces are still not sufficiently validated.** Also, since the ESP32-S2/S3 host is full speed only, **a UAC2 device with no full-speed configuration cannot be used at all**.

---

## USB Ethernet (CDC-NCM / CDC-ECM)

| Device | Status | What was verified | Reference |
|--------|--------|-------------------|-----------|
| ASIX AX88179A adapter | ⚠️ | Its network function is not in the default configuration, so it needs `setConfigurationSelector()` and two enumeration passes (arduino-esp32 3.3.11+) | [UsbNetwork](../examples/UsbNetwork/) / [usb-network-spec.ja.md](usb-network-spec.ja.md) |
| EspUsbDevice NCM device | ✅ | Peer-tested transmit/receive and lwIP netif attach | as above |

Whether an arbitrary adapter works depends on which configuration hides its CDC-NCM/ECM function. [`usb_network_descriptor`](../tests/manual/usb_network_descriptor/) scans every configuration and prints the candidates.

---

## Displays

| Device | VID:PID | Status | What was verified | Reference |
|--------|---------|--------|-------------------|-----------|
| DisplayLink DL-1xx (USB to DVI-17) | `17e9:0360` | ⚠️ | EDID read, 1920x1080 mode set, 16 bpp drawing, usable as a LovyanGFX panel | [`EspUsbHostDisplayDl1xx`](../examples/Vendor/EspUsbHostDisplayDl1xx/) / [usb-display-spec.ja.md](usb-display-spec.ja.md) |
| AX206 USB photo frame | `1908:0102` | ⚠️ | 480x320. Whole-screen blits only, so each frame is one 307,200-byte transaction — **about 2 fps on an ESP32-S3** | [`EspUsbHostDisplayAx206`](../examples/Vendor/EspUsbHostDisplayAx206/) |
| 3.5-inch USB smart screen (`USB35INCHIPSV2`) | `1a86:5722` | ⚠️ | A private protocol over CDC serial, 16 bpp, partial updates, brightness | [`EspUsbHostDisplayTuring`](../examples/Serial/EspUsbHostDisplayTuring/) |

All three are best-effort examples: a reference implementation for one device family each. Frame rate is bounded by bus bandwidth (about 1.1 MB/s at FS, about 36 MB/s at HS — [advanced guide 4.4](usb-host-advanced.md#44-theory-versus-measurement)).

On an ESP32-P4 the DL-1xx adapter needs a self-powered hub and must be the only device on it.

---

## Instruments, printers and others

| Device | VID:PID | Status | What was verified | Reference |
|--------|---------|--------|-------------------|-----------|
| KIKUSUI PMX18-5A DC power supply (USBTMC) | `0b3e:1029` | ⚠️ | Class requests, the bulk message layer, SCPI queries, 20 back-to-back queries, CLEAR. The USB488 interrupt IN is unused | [`EspUsbHostUsbtmcScpi`](../examples/Vendor/EspUsbHostUsbtmcScpi/) / [usbtmc-spec.ja.md](usbtmc-spec.ja.md) |
| Xprinter XP-C58K receipt printer | `0483:070b` | ⚠️ | Three class requests, ESC/POS real-time status, a Japanese receipt with a barcode, a QR code and the cutter. **The class requests answer empty**, so `DLE EOT n` is the status path | [`EspUsbHostPrinterEscPos`](../examples/Vendor/EspUsbHostPrinterEscPos/) / [printer-spec.ja.md](printer-spec.ja.md) |
| Android phone (ADB) | — | ⚠️ | Needs USB debugging enabled and a data cable. RSA key authentication and a shell stream | [`EspUsbHostAdbConnect`](../examples/Vendor/EspUsbHostAdbConnect/) |

---

## USB hubs

| Device | VID:PID | Status | Notes |
|--------|---------|--------|-------|
| Self-powered USB 2.0 hubs (generic) | — | ✅ | Several devices at once, topology reporting |
| PPPS-capable hubs | — | ✅ | Per-port power off/on and device re-enumeration ([`hub_power`](../tests/manual/hub_power/)) |
| RTD5411 | `0bda:5411` | ✅ | Fine, including with a DP100 attached |
| CH335F | `1a86:8094` | ⚠️ | Fine with ordinary HID devices, flash drives and the PMX18-5A |
| **CH335F with an ALIENTEK DP100** | `1a86:8094` + `2e3c:af01` | ❌ | **The ESP-IDF hub driver crashes into a reboot loop** (an assert in `ext_hub.c`). It is not caused by this library and happens with hub tracking disabled too |

The known hub problems are detailed in [tests/manual/README.md](../tests/manual/README.md). To work out whether a hub is the cause, use [`tests/probe/hub_enum`](../tests/probe/).

**Hubs are effectively unusable on the ESP32-P4 high-speed port.** Use the full-speed port when you need one ([guide 3.2](usb-host-guide.md#32-choosing-the-fs-or-the-hs-port-p4)).

---

## Confirmed not to work

| Device | Reason |
|--------|--------|
| USB cameras in general (UVC, class `0x0e`) | Arduino-ESP32's precompiled host stack uses `CONFIG_USB_HOST_CONTROL_TRANSFER_MAX_SIZE=256`, so a device whose configuration descriptor exceeds 256 bytes fails during enumeration. **The Logitech C920's is 1,974 bytes.** No sketch or library option changes this |
| USB 3.x (SuperSpeed) only devices | The ESP32 is USB 2.0. A device that does not fall back cannot be used |
| HS-only devices with no FS configuration (ESP32-S2/S3) | The S2/S3 host is full speed only. They may work on an ESP32-P4 high-speed port |
| Devices with 1024-byte endpoints (on a full-speed port) | Full speed has no such size. Needs the ESP32-P4 HS port plus a FIFO repartition ([advanced guide 5.2](usb-host-advanced.md#52-the-fifo-split)) |

---

## Boards

Before using a board as a USB Host, confirm that **the connector actually supplies 5 V on VBUS**.

| Board | Supplies VBUS | Notes |
|-------|---------------|-------|
| Espressif ESP32-S3-DevKitC-1 | ❌ No | The USB OTG connector does not power an attached device. Needs an external supply or a self-powered hub |
| Freenove ESP32-S3-WROOM Board | ✅ Yes | Powers a device from the OTG Type-C connector. Recommended as a first board |
| Some M5Stack products | ⚠️ Product-dependent | Some can switch connector power in software. Check the schematic and procedure |
| Single-connector products such as the AtomS3 | ⚠️ Product-dependent | Fine for a finished product, but during development prefer a board with two connectors |

During development, a board that separates flashing/serial monitor (USB-UART) from the host port (OTG) is far easier to work with. **Which connector is which differs per board** — the positions are reversed between the ESP32-S3-DevKitC-1 and the Freenove board. Check the schematic, not the silkscreen.

The ESP32-P4 has three USB functions (USB Serial/JTAG, OTG FS, OTG HS) and boards vary widely; see the ESP32-P4 notes in [README.md](../README.md) and the port-identification sketches in [`tests/probe/`](../tests/probe/).

---

## Adding to this list

When you verify a new device, record the following. Aim for enough detail that a later reader can reproduce it:

- VID:PID and the product name (and the firmware version if you can read it)
- Which target (ESP32-S3 / P4) and which port (FS / HS)
- How it was connected (direct, through a hub, self-powered hub required)
- What you verified, and what you did not
- Where the code lives (example, manual test, protocol notes)

The procedure itself is in "Running the experiments" in the [USB Host Development Guide](usb-host-guide.md).
