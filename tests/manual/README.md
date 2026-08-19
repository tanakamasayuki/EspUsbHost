# Manual Tests

> 日本語版: [README.ja.md](README.ja.md)

This directory contains tests that cannot be automated.
Each test is manual because the environment cannot be fully controlled by software — not because automation was inconvenient.

See [../TEST_PLAN.md](../TEST_PLAN.md) for the overall test strategy and the rationale for each manual test category.

## Running a manual test

Manual test files do **not** use the `test_` prefix, so pytest does not collect them automatically.
Run explicitly after preparing the required hardware:

```sh
cd tests
uv run --env-file .env pytest manual/smoke/smoke.py -v -s
uv run --env-file .env pytest manual/vcp_ftdi/vcp_ftdi.py -v -s
uv run --env-file .env pytest manual/msc_block/msc_block.py -v -s
uv run --env-file .env pytest manual/msc_hotplug_mount/msc_hotplug_mount.py -v -s
uv run --env-file .env pytest manual/msc_cache_coherency/msc_cache_coherency.py -v -s
uv run --env-file .env pytest manual/adb_connect/adb_connect.py -v -s
uv run --env-file .env pytest manual/vendor_bulk_out_only/vendor_bulk_out_only.py -v -s
uv run --env-file .env pytest manual/vendor_bulk_throughput/vendor_bulk_throughput.py -v -s
uv run --env-file .env pytest manual/usb_display_dl1xx/usb_display_dl1xx.py -v -s
uv run --env-file .env pytest manual/usb_display_throughput/usb_display_throughput.py -v -s
uv run --env-file .env pytest manual/usb_display_turing/usb_display_turing.py -v -s
```

Always pass `-s` for manual tests so that serial output and operator prompts are visible.

The default board profile is `esp32s3`. To use a different board, pass `--profile`:

```sh
uv run --env-file .env pytest manual/smoke/smoke.py -v -s --profile esp32p4
```

Available profiles are defined in each test's `sketch.yaml`.

## Test catalog

| Test | Description | Hardware required | Status |
|------|-------------|-------------------|--------|
| [`smoke/`](smoke/) | Procedure check — verifies that the manual test workflow itself works (build, flash, serial, operator prompt). Not a feature test. Run this first on a new machine. | ESP32-S3 or ESP32-P4 | ✅ |
| [`vcp_ftdi/`](vcp_ftdi/) | TX/RX loopback via FTDI VCP (VID 0x0403) | FTDI device (FT232R etc.) with TX/RX shorted | ✅ |
| [`vcp_cp210x/`](vcp_cp210x/) | TX/RX loopback via CP210x VCP (VID 0x10C4) | CP210x device (CP2102 etc.) with TX/RX shorted | ✅ |
| [`vcp_ch34x/`](vcp_ch34x/) | TX/RX loopback via CH34x VCP (VID 0x1A86) | CH34x device (CH340 etc.) with TX/RX shorted | ✅ |
| [`vcp_pl2303/`](vcp_pl2303/) | TX/RX loopback via PL2303 VCP (VID 0x067B PID 0x2303) | PL2303 device with TX/RX shorted | ✅ |
| [`vcp_pl2303gs/`](vcp_pl2303gs/) | TX/RX loopback via PL2303GS VCP (VID 0x067B PID 0x23A3) | PL2303GS device with TX/RX shorted | ✅ |
| [`esp32_autoreset/`](esp32_autoreset/) | DTR/RTS reset and ROM download-mode check with a target ESP32 | USB serial adapter wired to a target ESP32 auto-reset circuit | ✅ |
| [`keyboard_leds/`](keyboard_leds/) | NumLock/CapsLock LED visual confirmation | USB keyboard with indicator LEDs | ✅ |
| [`multi_hid_keyboard_mouse/`](multi_hid_keyboard_mouse/) | Keyboard and mouse deliver events independently when connected simultaneously | USB keyboard + USB mouse | ✅ |
| [`multi_serial/`](multi_serial/) | Two serial devices work independently via `setAddress()` | Two USB serial devices with TX/RX shorted | ✅ |
| [`hotplug/`](hotplug/) | Connect/disconnect events and no crash after repeated cycles | Any USB device | ✅ |
| [`hub_info/`](hub_info/) | Displays hub topology info for devices connected through a USB hub | USB hub + two USB devices | ✅ |
| [`hub_power/`](hub_power/) | Per-port power control — turn a hub port off/on and verify device disconnect/reconnect | USB hub with per-port power switching + any USB device | ✅ |
| [`usb_network_descriptor/`](usb_network_descriptor/) | Detects generic CDC-ECM/CDC-NCM USB Ethernet descriptor candidates across configurations | USB Ethernet adapter with CDC-ECM or CDC-NCM support | ✅ |
| [`msc_block/`](msc_block/) | Query real USB flash-drive MSC capacity, read LBA 0, mount FatFs/VFS, and write/read/delete temporary probe files through POSIX and `fs::FS` APIs | USB flash drive | ✅ |
| [`msc_hotplug_mount/`](msc_hotplug_mount/) | Unplug a USB flash drive while mounted and verify the same FatFs/VFS path can mount again after reconnect | USB flash drive | ✅ |
| [`msc_cache_coherency/`](msc_cache_coherency/) | Re-read one static LBA range as multi-sector transfers under cache pressure and compare against a single-sector reference, catching CPU-cache/USB-DMA incoherency (read only) | ESP32-P4 + USB storage (ESP32-S3 is a negative control) | ✅ |
| [`adb_connect/`](adb_connect/) | Authorize a real Android ADB transport, authenticate with a persisted RSA key, and verify one shell echo stream | Android device with USB debugging enabled + USB data cable | ✅ |
| [`ccid_info/`](ccid_info/) | Dumps interface/endpoint layout and reports whether a CCID interface (class 0x0b) is present | CCID smart card reader (e.g. Sony RC-S300 PaSoRi) | ✅ |
| [`ccid_card/`](ccid_card/) | CCID reader end to end: open, class descriptor, slot status, power on with ATR, repeated Get UID APDUs, raw GetSlotStatus | CCID smart card reader + a card on it | ✅ |
| [`ccid_felica/`](ccid_felica/) | FeliCa IDm for a chosen System Code through a Sony RC-S300 transparent session: session start, switch protocol to FeliCa, RF on/off, a Polling for the wildcard 0xffff and for the transit 0x0003, and the IDm out of the answer. Logs the reader's own polling alongside it for comparison | Sony RC-S300 + a FeliCa card (a transit card exercises 0x0003) | ✅ |
| [`ccid_hotplug/`](ccid_hotplug/) | CCID slot-change notifications reach onCcidCardRemoved()/onCcidCardInserted() when the card is lifted and replaced | CCID reader with an interrupt IN endpoint + a card | ✅ |
| [`vendor_bulk_out_only/`](vendor_bulk_out_only/) | `vendorOpen()` accepts a 0xff interface with a bulk OUT but no bulk IN; packet sizes and endpoint channel accounting match the descriptor. Also dumps the interface/endpoint layout | USB graphics adapter (DisplayLink DL-1xx, VID 0x17e9) or another bulk-OUT-only vendor device | ✅ |
| [`vendor_bulk_throughput/`](vendor_bulk_throughput/) | Effective bulk OUT throughput: synchronous `vendorWrite()` against the async queue at depths 1/2/4/8 and transfer sizes 512 B–16 KB, plus queue slot accounting and reuse. Establishes the practical bulk OUT ceiling of the board: 1.098 MB/s at full speed (ESP32-S3), 36.4 MB/s at high speed (ESP32-P4) | Any device with a vendor-specific (0xff) bulk OUT endpoint | ✅ |
| [`usb_display_dl1xx/`](usb_display_dl1xx/) | DL-1xx bring-up: EDID read, 1920x1080 mode set, solid fills, color bars, 1px checkerboard, image persistence with no traffic, and mode resend. The images must be judged on the monitor | USB graphics adapter (DisplayLink DL-1xx, VID 0x17e9) + a 1920x1080 monitor | ✅ |
| [`usb_display_turing/`](usb_display_turing/) | 3.5-inch USB smart screen bring-up: CDC OUT queue, orientation, solid fills, primary bands, color bars, 1px checkerboard, partial rectangle, a 1/3/8/24/48/96-rectangle sweep of the same full screen, image persistence with no traffic, and brightness. The images must be judged on the panel | 3.5-inch USB smart screen (`1a86:5722`, `USB35INCHIPSV2`) | ✅ |
| [`usb_display_throughput/`](usb_display_throughput/) | Tuning sweep for the display path: tile geometry, double buffering, diff transfer, auto clear, whole-screen vs sprite redraw, direct-to-panel (including the full-clear flicker case) and scene content. Source of the guidance in the example README. Run per target: the bus share is taken against the ceiling of the board selected by `--profile` | Same as `usb_display_dl1xx`; on an ESP32-P4 the adapter needs a self-powered hub and must be the only device on it | ✅ |
| [`usbtmc_scpi/`](usbtmc_scpi/) | USBTMC end to end: find and claim the class 0xfe / subclass 0x03 interface through the vendor bulk API, GET_CAPABILITIES and CLEAR on EP0 via `vendorControlTransfer()`, `*IDN?`, a setting written and read back, measurements, 20 back-to-back queries, a CLEAR on a live connection, and an empty SCPI error queue. Never switches the instrument's output on | KIKUSUI PMX series DC power supply (developed against a PMX18-5A, `0b3e:1029`) | ✅ |
| [`dp100/`](dp100/) | ALIENTEK DP100 power supply over HID: the framed request/response exchange through `onHIDInput()` and `sendHIDVendorOutput()`, DEVICE_INFO and BASIC_INFO field offsets, the mV / 0.1 degC scales checked against physical ranges, 50 repeated and 5 interleaved reads with no refusals. Read only, so safe with a load connected | ALIENTEK DP100 (`2e3c:af01`) connected directly, not through a hub | ✅ |
| [`dp100_output/`](dp100_output/) | ALIENTEK DP100 write path: the BASIC_SET frame with its 0x20 index flag, the state byte as the output enable, and the protection thresholds surviving a setpoint change. Nothing is trusted from the write's answer -- the device reports success for a write it then ignores -- so each step is confirmed by reading the setpoint back and by reading what the output is doing. **Energises the output at 5.000 V / 0.500 A: run with the terminals bare.** Refuses to start if the output is already on, and restores the original setpoint | ALIENTEK DP100 (`2e3c:af01`) connected directly, output terminals disconnected | ✅ |
| [`printer_escpos/`](printer_escpos/) | USB Printer Class request layer: find and claim the class 0x07 interface through the vendor bulk API, GET_DEVICE_ID / GET_PORT_STATUS / SOFT_RESET on EP0 via `vendorControlTransfer()`, the four ESC/POS real-time status bytes with their fixed bits, 20 back-to-back status polls, and the endpoints still working after SOFT_RESET. **Uses no paper**: nothing is queued for printing. Tolerates a printer that answers the class requests with nothing (an empty device ID, a 0x00 port status), which is what the XP-C58K does | ESC/POS USB receipt printer with paper loaded (developed against an Xprinter XP-C58K, `0483:070b`) | ✅ |
| [`printer_print/`](printer_print/) | ESC/POS print data path: a whole receipt in one bulk transfer, Japanese text from the printer's Shift-JIS font ROM, a CODE128 barcode, a QR code and the auto cutter, with the printer's status checked before and after and the status path re-checked after the transfer. **Uses one slip (~10 cm) per run and cuts the paper.** Refuses to print if the printer reports paper out or an error, and fails if either appears afterwards. What is on the slip has to be judged by looking at it | Same as `printer_escpos`, and the Japanese text needs a printer with a two-byte font ROM | ✅ |
| [`device_dump/`](device_dump/) | Dumps descriptors, interfaces, endpoints and channel accounting for every enumerated device, plus the bulk/interrupt endpoints a wrapper would use for interfaces the library does not claim itself (USBTMC, printer, vendor-specific). For working out what an unsupported device exposes | Any USB device | ✅ |
| [`raw_descriptor/`](raw_descriptor/) | Reads the raw DEVICE and CONFIGURATION descriptors with standard GET_DESCRIPTOR requests on EP0 and walks the configuration block by block, printing the bytes of each block with its `bDescriptorType` -- including the class-specific descriptors (HID, CDC functional, CCID, UAC) the parsed dump never shows. These are the bytes to compare against a USBPcap capture or `lsusb -v`. Truncates at 248 bytes, the control transfer limit | Any USB device | ✅ |
| [`hid_report_descriptor/`](hid_report_descriptor/) | Fetches and prints the HID report descriptor of a connected HID device | USB HID keyboard or mouse | — |

## Hub combinations that crash ESP-IDF

Some hub / device pairs take down the ESP-IDF host stack's own hub driver, and
nothing this library does or avoids changes it. Recorded here so a run that
reboot-loops can be recognised rather than re-investigated.

| Hub | Device behind it | Result |
|---|---|---|
| CH335F (`1a86:8094`) | ALIENTEK DP100 (`2e3c:af01`) | **Reboot loop.** `assert failed: device_release ext_hub.c:509 (ext_hub_dev->dynamic.flags.waiting_release)`, inside `ext_hub_process` -> `handle_device` -> `device_control_response_handling` -> `handle_hub_descriptor` -> `device_configure`. ESP-IDF v5.5.5 / arduino-esp32 3.3.11 |
| CH335F (`1a86:8094`) | KIKUSUI PMX18-5A, USB flash drives, HID devices | Fine |
| RTD5411 (`0bda:5411`) | ALIENTEK DP100 | Fine |
| — (direct connection) | ALIENTEK DP100 | Fine |

What was established with `probe/hub_enum`:

- The CH335F never reports a connection on **any** port with the DP100 attached
  (`connected=0` on all four, `powered=1`), and power-cycling the ports does not
  change that. So the hub does not see the device even when it is not crashing.
- **The library is not part of the mechanism.** It crashes with hub tracking turned
  off, i.e. with no client handle on the hub and no hub descriptor or port-status
  traffic from this library. The log line before the assert is
  `[phase1] tracked=0 hub_tracking=0 host_addresses=0`: it goes down inside ext_hub
  before the hub even reaches the host stack's address list.
- It is intermittent: the same firmware survived two runs before one caught it.

A board stuck in this loop can be hard to flash. `probe/hub_enum` therefore boots
with `setHubTrackingEnabled(false)` and only turns tracking on part way through, so
it always comes up in a state that can be reflashed.

## ESP32-S3 HCD Channel Limits

ESP32-S3 has 8 USB host channels (`OTG_NUM_HOST_CHAN`). When several devices are connected through a USB hub, ESP-IDF may fail to allocate enough host channels for all claimed interfaces. Because the exact channel use depends on the hub and device descriptors, this document records only combinations that have been observed with this test suite.

Observed `multi_serial` results on ESP32-S3 through a USB hub:

| Combination | Result | Notes |
|-------------|--------|-------|
| FTDI + CP210x | PASS | Both loopback ports passed |
| FTDI + CH34x | FAIL | Endpoint allocation failed with HCD channel exhaustion |

If the log contains `No more HCD channels available`, `EP Alloc error: ESP_ERR_NOT_SUPPORTED`, or `Claiming interface error: ESP_ERR_NOT_SUPPORTED`, the selected device mix exceeded the available ESP32-S3 host-channel resources in that setup. Test one serial device at a time or use a different hardware setup.

## Test results

Manual test results are saved automatically by `--save-state` to:

```
tests/.pytest-results/state.json
```

This file records the last outcome and timestamp for each test node ID. When a feature is changed, check this file to see when the related manual tests were last run and whether they need to be re-run. Tests that have never passed, or that passed before the relevant code was modified, should be flagged for re-execution.

Note that this file is local to the machine and not committed to the repository. It may be missing (deleted or never run), and results from other machines are not reflected. Treat it as a helpful hint, not a definitive record.

## Why each category is manual

| Category | Reason automation is not possible |
|----------|------------------------------------|
| VCP serial (FTDI, CP210x, CH34x) | Requires real VCP hardware; ESP32 cannot emulate these vendor-specific protocols |
| Multiple simultaneous devices | Requires multiple physical USB devices connected at the same time |
| Keyboard LED visual verification | Pass/fail depends on whether a physical LED lights up |
| Device hot-plug stress | Requires a person to physically plug and unplug cables on a timing cue |
| USB hub (info display, power management) | Requires a physical USB hub. While it is technically possible to route multiple devices through a hub in the automated test environment, doing so would mix hub behaviour into the test results and introduce noise. Automated tests therefore use direct 1-to-1 connections only |
| USB Mass Storage | Requires real USB flash-drive descriptors, timing, and SCSI command behavior. Peer tests cover the protocol skeleton but cannot prove real-device compatibility |
| USB Ethernet | Requires real USB NIC descriptors and configuration layouts. Peer tests cannot emulate the product-specific mix of vendor, CDC-ECM, CDC-NCM, and optional storage configurations |
| Android ADB | Requires a real Android device with USB debugging enabled. Its descriptors, ADB transport timing, and authorization state cannot be fully reproduced by the peer device |
| Hub cascade (hub behind hub) | Requires two or more nested physical hubs; cannot be emulated in software |
| Human-only observable output (audio, MIDI, etc.) | Involves physical output such as sound that cannot be observed directly from software. Automatable with audio loopback hardware, but typically requires human confirmation |

## Judgment approach

Tests should use the simplest judgment method that works for the scenario, in order of preference:

1. **Special device, fully automated** — connect the required hardware before running; the test itself runs fully automatically via `dut.expect()`. Manual only because the hardware cannot be emulated.

2. **Timeout** — the test prints an instruction (e.g., "connect the device now"), then waits with a timeout for the device to be recognised. No y/n input needed.

3. **Human visual confirmation** — reserved for things that cannot be observed in software (e.g., a physical LED, audio output). The test asks the operator to judge and enter `y` / `n`.

## Test independence

Flashing is done once per `.py` file. Multiple test functions within the same file share the device state — the board is not reset between them, so a later test may be affected by what an earlier test left behind.

For full independence, separate the test into its own `.py` file with its own sketch. The recommended practice is **one test function per `.py` file**. Only group multiple tests in one file when they intentionally share setup state and you accept that dependency.

## Test file template

```python
"""
Purpose:
    One sentence describing what this test verifies.

Why manual:
    The specific reason this test cannot be automated.
    (e.g., "Requires real FTDI hardware — ESP32 cannot emulate this VID/PID.")

Required hardware:
    - Device type and model (e.g., FT232R breakout board)
    - Connection method (e.g., USB-A to the ESP32-S3 host board)

Setup:
    1. Flash the host board with the EspUsbHostUSBSerial sketch.
    2. Connect the VCP device to the USB port of the host board.
    3. Run: uv run --env-file .env pytest manual/<name>/<name>.py -v -s
"""

import pytest

# ---------------------------------------------------------------------------
# Test(s)
# ---------------------------------------------------------------------------

def test_something(dut):
    """
    Expected result (pass):  <concrete observable outcome>
    Expected result (fail):  <what failure looks like>
    """
    ...
```

### Writing expected results

Write expected results so that any team member — not just the author — can judge pass or fail without ambiguity.

| Avoid | Write instead |
|-------|---------------|
| "Check if the LED lights up" | "NumLock LED turns ON within 1 second of sending `n`; LED turns OFF after sending `0`" |
| "Verify data is received" | "All 64 bytes sent from the host appear in the device's serial output in the same order" |
| "Make sure it doesn't crash" | "After 10 connect/disconnect cycles the sketch is still printing to Serial and no error is logged" |
