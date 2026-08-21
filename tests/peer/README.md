# Peer Tests

> 日本語版: [README.ja.md](README.ja.md)

`tests/peer` contains two-board tests. One ESP32-S3 runs an EspUsbHost sketch as
the USB host, and the peer ESP32-S3 runs a matching USB Device sketch.

Most tests are kept as the baseline that checks Host interoperability with the
Arduino Core standard Device stack. A test pairs with the sibling
`EspUsbDevice` library only when Arduino Core cannot express the device at all
(`usb_vendor`, `usb_ncm`, `usb_ncm_throughput`, `hid_keyboard_composite`,
`hid_keyboard_nkro`, `usb_audio_uac2`); see [../TEST_PLAN.md](../TEST_PLAN.md).

Run from `tests`:

```sh
uv run --env-file .env pytest peer/
```

## Hardware wiring

The host board and the peer board must be connected via USB.

Connecting them with a standard USB cable also shares the VBUS (5 V) line between the two boards, which can be problematic when both boards are already powered from separate USB connections (e.g., both plugged into a PC). In that case, connecting only the data lines is safer.

On ESP32-S3, the USB D− and D+ signals are on GPIO19 and GPIO20. Wire only these two pins between the two boards (and a shared GND) and leave the VBUS line unconnected:

| Host board | Peer board |
|------------|------------|
| GPIO19 (D−) | GPIO19 (D−) |
| GPIO20 (D+) | GPIO20 (D+) |
| GND | GND |

> **Note:** If you use a USB cable with the VBUS line cut, or a data-only cable, you can connect normally without worrying about power conflicts.

Peer tests use these Arduino CLI profile names:

- `s3_peer_host`: ESP32-S3 USB host board running EspUsbHost
- `s3_peer_device`: ESP32-S3 USB device peer

Set matching serial ports in `.env`:

```sh
TEST_SERIAL_PORT_S3_PEER_HOST=/dev/ttyACM0
TEST_SERIAL_PORT_PEER_DEVICE_S3_PEER_DEVICE=/dev/ttyUSB0
```

Current coverage:

- `hid_logic`: HID helper logic checks that do not require a peer device.
- `custom_hid`: pairs with an Arduino Core standard Custom HID-style device.
- `hid_keyboard`: pairs with an Arduino Core standard USB keyboard device. Also verifies HID listener coexistence with the single callback, capacity and invalid-operation failures, removal, registration order, persistent mutable callback state, and next-event mutation semantics.
- `hid_mouse`: pairs with an Arduino Core standard USB mouse device.
- `hid_mouse_report`: pairs with a custom HID device declaring the report-protocol mouse layout issue #39 reports for a Logitech G502 HERO (16 buttons, 16-bit X/Y, wheel and AC Pan in 8 bytes, no report ID). Verifies the fields are located from the report descriptor rather than assumed to be the boot layout: 16-bit deltas, a Y-only report producing an event instead of looking idle, X movement not leaking into the wheel, AC Pan, and button 16 in the high byte of `buttonMask`. The Arduino Core `USBHIDMouse` used by `hid_mouse` can only produce the boot layout, which is the one layout that works without reading the descriptor.
- `hid_keyboard_mouse`: pairs with an Arduino Core standard keyboard + mouse composite device.
- `hid_keyboard_nkro`: pairs with an `EspUsbDeviceHidKeyboard` NKRO keyboard from the sibling `EspUsbDevice` library. Verifies bitmap-report decoding (8-key chord) and that `setKeyboardLeds()` reaches the keyboard in report protocol.
- `hid_keyboard_composite`: pairs with an `EspUsbDevice` composite HID device (keyboard + consumer control + mouse in one interface with report IDs, no boot interface). Verifies each input reaches its host callback and that `setKeyboardLeds()` delivers the LED output report by report ID.
- `hid_consumer_control`: pairs with an Arduino Core standard consumer control device.
- `hid_system_control`: pairs with an Arduino Core standard system control device, including delivery with listeners only and no single callback.
- `hid_gamepad`: pairs with an Arduino Core standard gamepad device.
- `hid_vendor`: pairs with an Arduino Core standard vendor HID device.
- `usb_serial`: pairs with an Arduino Core standard USB CDC device.
- `usb_midi`: pairs with an Arduino Core standard USB MIDI device. Also covers the MIDI and device-lifecycle listener APIs; one test reboots the peer on purpose, because the device core has no USB detach API and a reboot is the only way to hand the host a real disconnect.
- `usb_msc`: pairs with an Arduino Core standard `USBMSC` device backed by a 16-block x 512-byte RAM disk. Covers capacity queries (32- and 64-bit variants), Inquiry, Max LUN and LUN selection, Request Sense, Test Unit Ready / wait-ready, Synchronize Cache, single-block, multi-block and chunked write/read round trips verified against the peer's memory, rejection of out-of-range accesses, and a failed write being reported (the peer fails one write on request). It also covers `end()`/restart twice: with the device attached, and with the device list empty because the peer is rebooting, which is the case that left the host library installed (issue #42).
- `usb_msc_fat`: pairs with an Arduino Core standard `USBMSC` device whose 256-block x 512-byte RAM disk the peer formats itself with `f_mkfs()` and populates with `PEER.TXT` before USB comes up, so the volume is produced by the same FatFs the host mounts it with. Covers `mscMount()` / `mscUnmount()` / `mscMounted()`, reading the peer's file through the VFS path, the reported sequence of mount, unmount, `end()`, `begin()` and mount again, and `end()` releasing a volume that is still mounted (issue #42) rather than stranding the FatFs drive slot and VFS path. Note that this peer answers SYNCHRONIZE CACHE inconsistently — the same command passes once and comes back with CSW status 1 the next time — so the `y` command is a diagnostic probe and is deliberately not asserted on; the library degrades to skipping it per device, and an unmount stays successful either way.
- `usb_audio`: pairs with an Arduino Core standard USB Audio device via `USBAudioCard` speaker output. UAC1.
- `usb_audio_uac2`: pairs with an `EspUsbAudioFunction` headset from the sibling `EspUsbDevice` library, selected as UAC2. `USBAudioCard` is UAC1 only, so this is the one audio configuration the Arduino Core device stack cannot produce. Covers the class revision, the Clock Source sample rates (read with a `SAM_FREQ` `RANGE` request, since UAC2 descriptors do not carry them), the 4-byte / 2-bit Feature Unit controls with the volume `RANGE` request, the feedback IN endpoint being kept out of the stream list and instead polled to pace the OUT packets (`f`: the reported rate near 48 kHz, the pacing rate following it, and updates continuing to arrive), OUT/IN streaming, and — not UAC2-specific — starting with zeroed format arguments so the library resolves the best format itself.
- `usb_vendor`: pairs with `EspUsbDeviceVendor` from the sibling `EspUsbDevice` library.
- `usb_ncm`: pairs with an `EspUsbDeviceNet` (CDC-NCM) device from the sibling `EspUsbDevice` library. The host attaches the USB NIC as a DHCP-client lwIP netif, gets a `192.168.7.x` lease from the device's DHCP server, and fetches a fixed page over HTTP.
- `usb_ncm_throughput`: the same pairing under sustained load, driving a TCP sink (port 9000) and a TCP source (port 9001) on the device for 5 s each and checking that both directions keep moving, that the bulk OUT reports no failures, and that no NTB is dropped for exceeding the negotiated size. The peer's `build_opt.h` raises `CFG_TUD_NCM_IN_NTB_MAX_SIZE` to 8192 with three transmit buffers, and the device writes in bursts, so it batches several datagrams per NTB the way a real USB NIC does — the configuration that exposed the host dropping every NTB above its formerly fixed 3200-byte buffer. Keep that peer configuration: `usb_ncm` produces one datagram per NTB and cannot see the regression. Because those are compiler flags, run this test with `--clean` after touching `build_opt.h` — otherwise the cached `EspUsbDevice` build is reused and the peer advertises the old size; the test checks the negotiated size and fails loudly when that happens.

Planned coverage:

- Host-side regression tests that can be reproduced with the Arduino Core standard Device stack.
- Minimal Host-side reproductions for issues first found in EspUsbDevice tests.
