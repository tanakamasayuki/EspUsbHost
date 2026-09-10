# Test Plan

> 日本語版: [TEST_PLAN.ja.md](TEST_PLAN.ja.md)

## Testing strategy

Tests are divided into two categories based on whether the test environment can be fully controlled by software.

**Automated tests** run in CI or locally without human interaction.
All inputs are generated programmatically; all expected outputs are verified by assertion.

**Manual tests** are used when the environment cannot be fully controlled by software — not because automation is inconvenient, but because the test inherently requires physical hardware that cannot be emulated, or verification that requires human judgment (visual inspection, tactile feedback, etc.).

### Test Split With EspUsbDevice

EspUsbHost `peer/` tests should generally keep their current setup based on the
Arduino-ESP32 standard USB Device implementation. This keeps the Host library
checked against Arduino Core's device stack and avoids overfitting Host behavior
to the sibling `EspUsbDevice` library.

EspUsbDevice tests pair with the released EspUsbHost version for detailed
Host/Device coverage. That repository owns tests for behavior that Arduino
Core's standard USB Device implementation cannot control well, such as
descriptors, report IDs, output/feature reports, composite HID, CDC, MIDI, and
MSC behavior. ESP32-P4 single-board loopback tests are also currently organized
primarily in EspUsbDevice.

ESP32-P4 loopback is not maintained in this repository because Arduino-ESP32's
standard USB Device implementation only runs on the HS side on P4. In a
single-board direct loopback setup, the device role uses HS and the host role is
fixed to FS. The FS host side cannot handle an HS device, so endpoint
claim/allocation fails. This is only a direct loopback limitation; it is not a
general issue when using USB Host or the standard USB Device feature
independently.

Local EspUsbHost checkouts may still be substituted in EspUsbDevice when an
unreleased Host-side fix needs pre-release validation, but the normal acceptance
baseline here remains released EspUsbHost plus the Arduino Core standard Device
implementation.

A `peer/` test uses an `EspUsbDevice` peer only when the Arduino Core device
stack cannot express the device at all: `usb_vendor`, `usb_ncm`,
`usb_ncm_throughput`, `hid_keyboard_composite`, `hid_keyboard_nkro`, and
`usb_audio_uac2`. UAC2 is such
a case because `USBAudioCard` is UAC1 only, so nothing in Arduino Core can
present the Clock Source entity, the 4-byte Feature Unit controls, or the
`RANGE` requests that the host's UAC2 path exists to handle.

```
tests/
  harness/    Automated — tests for the test harness itself; no board
  peer/       Automated — two ESP32-S3 boards, one host + one device
  loopback/   Reserved — single ESP32-P4 setup; no runnable tests in this repo now
  manual/     Manual — special hardware or human interaction required
  probe/      Probes — throwaway bring-up and protocol-investigation sketches
  unit/       Automated — host-side g++ tests, no board required
```

**Files under `manual/` and `probe/` must not be named `test_*.py`.** That is
what keeps them out of the default run, and there is no second mechanism: no
`testpaths`, no marker. Declaring the paths as well would say the same thing
twice and would hide the mistake -- a file wrongly named `test_*.py` under
`manual/` should join the run and be noticed rather than be quietly skipped.
Naming a path on the command line still collects it, which is how a manual test
is run.

See each subdirectory's README for hardware setup and individual test details.

---

## How the tests are structured

**One test per module.** A module is one sketch directory, and running it costs a
compile and an upload; a test inside it costs almost nothing. Measured on this
bench: a module's fixed cost is about 29 s, a test inside one about 0.6 s. The
granularity worth paying for is therefore the module, and what would otherwise be
several tests is written as **checks** inside one test.

A check is a plain nested function, and `run_checks` (a fixture in
`tests/conftest.py`) calls them in order. A failure propagates as it normally
would, so pytest's traceback names the frame it happened in -- the check's own
name -- and shows the line that failed.

```python
def test_usb_msc(dut, peers, run_checks):
    def capacity():
        dut.write("c")
        dut.expect_exact("MSC_CAPACITY ok=1 blocks=16 block_size=512")

    def inquiry():
        dut.write("i")
        dut.expect_exact("MSC_INQUIRY ok=1 removable=1 vendor='ESP32' ...")

    run_checks([capacity, inquiry])
```

This is not a compromise for the sake of a rule. `usb_msc` took 46.8 s as twenty
tests and 31.0 s as one, because pytest's per-test cost is paid once rather than
twenty times.

**Two exceptions, both because something has to be attached per case.**

- `usb_midi` keeps `test_usb_midi_lifecycle_listeners_on_peer_reboot` as its own
  test, because `tests/conftest.py` permits one serial line for that node id
  alone. Merged, the permission would widen to the whole module, and the same
  line would go unnoticed where it should not be allowed.
- A destructive check would get its own module rather than a fixed position among
  tests: a second test file in the same sketch directory is a second module, and
  its upload restores the board. Nothing needs this today.

**The order of checks is not allowed to matter.** Merging tests into one moves
order dependence inside the test rather than removing it, so the audit moves with
it:

```bash
ESPUSBHOST_REVERSE_CHECKS=1 pytest peer/
```

Every module's checks then run back to front. A check that passes in only one
order is a design error: build the state it needs instead of relying on an
earlier check to have left it behind. Reversing costs no extra upload, so it
stays an everyday check rather than a release-time one.

**The host is not started at boot.** The peer board is flashed after the DUT has
booted, and a host that is already running sees those resets and records the
enumerations they cause as errors. So `setup()` only prints `HOST_STATE idle
devices=0`, and an autouse fixture sends `G` to start the host and `H` to stop it.

Two details in that fixture are load-bearing. It requests `peers` **for the
ordering alone** -- without it the autouse fixture is set up before the peer
upload, which puts the host back inside the window the gating exists to close.
And `G` is idempotent in the sketch, so a module pays the start once: paying it
per test stretched the suite from 12 m 33 s to 21 m 49 s, while paying it once per
module costs 47 s over not gating at all.

**Readiness is queried, never awaited.** `HOST_CONNECTED` and friends are printed
once, when the device enumerates, so a check that waits for one only works while
it happens to run first. Every gated sketch answers `Q` with its current state,
and the checks poll that.

## Test coverage matrix

| Feature | Automated | Manual | Not covered |
|---------|-----------|--------|-------------|
| HID keyboard input | ✅ peer (plain text, Shift-modified boot reports) | | |
| HID keyboard layout (JP) | ✅ peer (`Shift+International3`), ✅ hid_logic | | |
| HID mouse input | ✅ peer | | |
| HID consumer control | ✅ peer | | |
| HID system control | ✅ peer | | |
| HID gamepad | ✅ peer | | |
| HID multi-listener dispatch | ✅ peer (single-callback coexistence, listener-only delivery, ordering, capacity, removal, callback-time mutation) | | |
| HID vendor input/output | ✅ peer | | |
| HID raw input dump | ✅ peer (custom_hid) | | |
| Keyboard LED output | ✅ peer (hid_logic) | ✅ manual (visual) | |
| USB serial — CDC ACM | ✅ peer, line coding config, `end()`/restart | | |
| USB serial — VCP (FTDI/CP210x/CH34x) | | ✅ manual, serial format configs | |
| USB MIDI | ✅ peer | | |
| MIDI multi-listener dispatch | ✅ peer (single-callback coexistence, listener-only delivery, ordering, capacity, removal, callback-time mutation) | | |
| Device lifecycle multi-listener dispatch | ✅ peer (connect event from `end()` + re-begin, disconnect then reconnect from a peer reboot, single-callback coexistence, ordering, dedicated capacity of 8, removal) | | |
| Vendor-specific bulk/control | ✅ peer (usb_vendor, including `end()`/restart) | ✅ manual (Android ADB auth + shell stream) | |
| USB audio input/output — UAC1 | ✅ peer (bidirectional with standard `USBAudioCard`) | | ⬜ real USB microphones/audio interfaces |
| USB audio input/output — UAC2 | ✅ peer (`usb_audio_uac2`: class revision, Clock Source sample rates, 4-byte/2-bit Feature Unit controls, volume `RANGE`, explicit feedback endpoint polling and OUT pacing, bidirectional streaming), ✅ host unit (`unit/audio_uac`: descriptor and RANGE decoding) | | ⬜ real UAC2 devices, which are usually high-speed designs the full-speed host cannot enumerate; Clock Selector / Clock Multiplier; long asynchronous playback against a real DAC (the peer computes feedback from its FIFO level, not from a hardware clock) |
| USB Mass Storage — block I/O / FatFs mount | ✅ peer (capacity, Inquiry/Sense, read/write, out-of-range rejection, write failure reporting, `end()`/restart with the device attached and with an empty device list; usb_msc_fat: mount/read/unmount of a peer-formatted FAT12 volume, mount-unmount-`end()`-`begin()`-remount, `end()` releasing a still-mounted volume) | ✅ manual (real USB flash capacity, LBA0 read, FatFs/VFS mount, `fs::FS` wrapper, file write/read/delete, mounted disconnect/remount) | ⬜ full BOT recovery after failed data phase, multiple LUNs, >32-bit-sector FatFs mount |
| CCID smart card readers | | ✅ manual (`ccid_info` descriptor dump, `ccid_card` open/status/ATR/APDU and `ccid_hotplug` slot-change notifications on a Sony RC-S300) | ⬜ multi-slot readers, contact cards, chained responses, ICCD variants |
| USBTMC — SCPI instruments | | ✅ manual (`usbtmc_scpi`: class 0xfe interface claim, GET_CAPABILITIES and CLEAR on EP0, `*IDN?`, setting readback, measurements, repeated queries, empty error queue on a KIKUSUI PMX18-5A), ✅ host unit (`unit/usbtmc`: message headers, bTag rules, out-of-sync rejection, capability offsets) | ⬜ USB488 service requests over interrupt IN, ABORT_BULK_IN/OUT recovery on real hardware, instruments other than a PMX power supply |
| Printer — ESC/POS receipt printers | | ✅ manual (`printer_escpos`: class 0x07 interface claim, GET_DEVICE_ID / GET_PORT_STATUS / SOFT_RESET on EP0, the four real-time status bytes, 20 back-to-back polls, endpoints alive after SOFT_RESET -- no paper used), ✅ manual (`printer_print`: a receipt in one bulk transfer with Japanese Shift-JIS text, a CODE128 barcode, a QR code and an auto cut, verified on the slip; status checked before and after), ✅ host unit (`unit/escpos`: class requests, device ID parsing, port status bits, every ESC/POS command byte for byte, builder overflow), 🔍 probe (`probe/printer_class` established that this model answers both class requests with nothing) | ⬜ paper-out and cutter-jam paths on real hardware, unidirectional printers (protocol 0x01), 80 mm paper width, IPP / PWG-Raster / PCL, IEEE 1284.4 packet mode (out of scope) |
| ALIENTEK DP100 power supply (HID framed protocol) | | ✅ manual (`dp100`: framed request/response over the HID API, DEVICE_INFO / BASIC_INFO offsets and units, repeated and interleaved reads), ✅ host unit (`unit/dp100`: CRC-16/MODBUS, frame encode/decode, captured-report regression), ✅ manual (`dp100_output`: BASIC_SET with its 0x20 index flag, the output enable measured on the terminals, thresholds carried through, original setpoint restored), 🔍 probe (`probe/dp100` established the frame, CRC and both index flags) | ⬜ SYSTEM_INFO field meanings; SYSTEM_SET; SCAN_OUT / SERIAL_OUT; firmware update (out of scope) |
| USB Ethernet — CDC-ECM/CDC-NCM | | ✅ manual (generic descriptor candidate detection across configurations) | ⬜ configuration selection, frame RX/TX, lwIP integration |
| Multiple devices | | ✅ manual | |
| Device hot-plug | | ✅ manual | |
| HUB detection, topology, and port power control | | ✅ manual (`hub_info`, `hub_power`) | ⬜ change bit clear, cascaded hubs, USB 3.x hub compatibility |
