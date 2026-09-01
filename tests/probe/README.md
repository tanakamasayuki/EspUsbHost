# Probe tests

> 日本語版: [README.ja.md](README.ja.md)

Temporary sketches for bring-up and for working out a device's protocol.
These are not formal regression tests; they depend on board wiring, the connected port, host-PC enumeration behavior, or on a device whose protocol is not yet known.

Run them individually from the `tests/` directory:
The profile name is `esp32p4`, representing a generic P4 board used for individual runs.

```sh
uv run --env-file .env pytest probe/p4_hs_host/p4_hs_host_probe.py
uv run --env-file .env pytest probe/p4_hs_fs_hub/p4_hs_fs_hub_probe.py -v -s
uv run --env-file .env pytest probe/p4_fs_host/p4_fs_host_probe.py
uv run --env-file .env pytest probe/p4_hs_device/p4_hs_device_probe.py
uv run --env-file .env pytest probe/p4_cdc/p4_cdc_probe.py
uv run --env-file .env pytest probe/rcs300_felica/rcs300_felica_probe.py -v -s
```

## ESP32-P4 probes

- `p4_hs_host` — starts HS OTG as USB Host and checks enumeration of an external USB device.
- `p4_hs_fs_hub` — forces the HS OTG physical port into full-speed-only mode
  with `HCFG.FSLSSUPP`, then checks that an HS-capable hub enumerates at full
  speed and that an FS/LS child enumerates behind it. See
  [`docs/p4-hs-port-fs-only-hub.ja.md`](../../docs/p4-hs-port-fs-only-hub.ja.md)
  for the investigation and pass criteria.
- `p4_fs_host` — starts FS OTG as USB Host and checks enumeration of an external USB device.
- `p4_hs_device` — checks HS device enumeration as HID keyboard + CDC composite.
- `p4_cdc` — uses the plain `esp32p4` configuration to check whether the suspected connector enumerates as Hardware CDC/JTAG COM.

`p4_hs_device` and `p4_cdc` require checking Device Manager or a serial monitor on the PC side.
Connect an external USB device to the target port before running `p4_hs_host` or `p4_fs_host`.

Set `TEST_SERIAL_PORT_ESP32P4` in `.env` to the serial port of the P4 board used
for these checks. This repository currently does not use a runnable P4 profile
for `loopback/`.
`p4_cdc` intentionally does not use `USBMode=hwcdc,CDCOnBoot=cdc`. That option maps `Serial` to Hardware CDC/JTAG, which is not suitable when the goal is to identify the board's default port wiring.

## Hub / enumeration probes

- `hub_enum` — for a device that enumerates when connected directly but not through
  a particular hub. Built with `DebugLevel=verbose`, it reports the tracked devices,
  the host stack's own address list, and each hub's per-port connection status, then
  power-cycles every downstream port and reports again. It never sends anything that
  is not diagnostic, so it also separates "the hub cannot see the device" from
  "`printAllDeviceInfo()`'s hub queries broke the enumeration". Run it with `-s`: the
  log is the output.

## Reader protocol probes

- `rcs300_felica` — works out the command sequence a Sony RC-S300 needs for a
  FeliCa Polling with an explicit System Code. The sketch is a byte pump: it reads
  pseudo APDUs as hex lines from serial and sends them over `PC_to_RDR_XfrBlock` or
  `PC_to_RDR_Escape`, so candidate sequences are changed on the host side without
  reflashing. What it established is written up in the probe's docstring and in
  [`examples/Ccid/EspUsbHostCcidFelicaIdm`](../../examples/Ccid/EspUsbHostCcidFelicaIdm/).
  Run it with `-s`: the log is the output.

- `dp100` — works out the frame layout an ALIENTEK DP100 power supply expects
  inside its 64-byte HID reports. The sketch is a byte pump: it composes frames
  from an opcode and data given as hex lines on serial, with a selectable CRC
  variant, so the frame format is searched from the host side without reflashing.
  Which CRC gets a payload rather than the device's one-byte refusal is what
  identifies it. Read-only: the setpoint opcode carries the output enable and is
  deliberately never sent. What it established is written up in the probe's
  docstring and in
  [`examples/HID/EspUsbHostDp100Power`](../../examples/HID/EspUsbHostDp100Power/).
  Run it with `-s`: the log is the output.

- `printer_class` — works out which USB Printer Class requests an ESC/POS printer
  really answers. `manual/printer_escpos` found GET_DEVICE_ID failing and
  GET_PORT_STATUS returning a byte that decodes as "deselected, error" while the
  printer was demonstrably fine, and a host mistake looks exactly like an
  unimplemented request: wValue is the configuration index and wIndex packs the
  interface in the *high* byte, unlike the other requests in the class. The sketch
  sweeps both fields plus the device-recipient and vendor-type forms, and reads the
  port status before other exchanges and after SOFT_RESET. The answer was that the
  spec-correct form is the only one accepted and the printer simply has nothing to
  say -- an empty device ID and a 0x00 status -- which is why the example treats
  neither as a failure. Uses no paper. What it established is written up in the
  probe's docstring and in
  [`examples/Vendor/EspUsbHostPrinterEscPos`](../../examples/Vendor/EspUsbHostPrinterEscPos/).
  Run it with `-s`: the log is the output.

Place a FeliCa card on the reader before running `rcs300_felica`, and use
`TEST_SERIAL_PORT_ESP32S3` for it.
