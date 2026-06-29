# Probe tests

Temporary sketches for ESP32-P4 bring-up and USB port identification.
These are not formal regression tests; they depend on board wiring, the connected port, and host-PC enumeration behavior.

Run them individually from the `tests/` directory:
The profile name is `esp32p4`, representing a generic P4 board used for individual runs.

```sh
uv run --env-file .env pytest probe/p4_hs_host/p4_hs_host_probe.py
uv run --env-file .env pytest probe/p4_fs_host/p4_fs_host_probe.py
uv run --env-file .env pytest probe/p4_hs_device/p4_hs_device_probe.py
uv run --env-file .env pytest probe/p4_cdc/p4_cdc_probe.py
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
