# Probe tests

Temporary sketches for bring-up and for working out a device's protocol.
These are not formal regression tests; they depend on board wiring, the connected port, host-PC enumeration behavior, or on a device whose protocol is not yet known.

Run them individually from the `tests/` directory:
The profile name is `esp32p4`, representing a generic P4 board used for individual runs.

```sh
uv run --env-file .env pytest probe/p4_hs_host/p4_hs_host_probe.py
uv run --env-file .env pytest probe/p4_fs_host/p4_fs_host_probe.py
uv run --env-file .env pytest probe/p4_hs_device/p4_hs_device_probe.py
uv run --env-file .env pytest probe/p4_cdc/p4_cdc_probe.py
uv run --env-file .env pytest probe/rcs300_felica/rcs300_felica_probe.py -v -s
```

## ESP32-P4 probes

- `p4_hs_host` — starts HS OTG as USB Host and checks enumeration of an external USB device.
- `p4_fs_host` — starts FS OTG as USB Host and checks enumeration of an external USB device.
- `p4_hs_device` — checks HS device enumeration as HID keyboard + CDC composite.
- `p4_cdc` — uses the plain `esp32p4` configuration to check whether the suspected connector enumerates as Hardware CDC/JTAG COM.

`p4_hs_device` and `p4_cdc` require checking Device Manager or a serial monitor on the PC side.
Connect an external USB device to the target port before running `p4_hs_host` or `p4_fs_host`.

Set `TEST_SERIAL_PORT_ESP32P4` in `.env` to the serial port of the P4 board used
for these checks. This repository currently does not use a runnable P4 profile
for `loopback/`.
`p4_cdc` intentionally does not use `USBMode=hwcdc,CDCOnBoot=cdc`. That option maps `Serial` to Hardware CDC/JTAG, which is not suitable when the goal is to identify the board's default port wiring.

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

Place a FeliCa card on the reader before running `rcs300_felica`, and use
`TEST_SERIAL_PORT_ESP32S3` for it.
