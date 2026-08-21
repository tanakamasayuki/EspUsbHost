# EspUsbHost documentation index

> 日本語版: [README.ja.md](README.ja.md)

Everything under `docs/`, grouped by what you would be looking for. The library API itself is documented in the top-level [README.md](../README.md).

## Guides

| Document | What it covers |
|----------|----------------|
| [usb-host-guide.md](usb-host-guide.md) · [ja](usb-host-guide.ja.md) | **Start here.** USB Host fundamentals, power and hubs, ESP32-specific limits, the order to run experiments in, capturing and analysing an unknown protocol, troubleshooting |
| [usb-host-advanced.md](usb-host-advanced.md) · [ja](usb-host-advanced.ja.md) | Architecture and task model, descriptor byte layouts, control transfer anatomy, timing and bandwidth, channels and the FIFO split, error recovery, throughput design, callback context, implementing a new class |
| [tested-devices.md](tested-devices.md) · [ja](tested-devices.ja.md) | Every device and board verified on real hardware: VID:PID, conditions, what was and was not checked |
| [usb-display.md](usb-display.md) · [ja](usb-display.ja.md) | Index of the USB display examples — three different protocols over three different transports |

## Protocol notes

Analysis notes for devices whose protocol had to be worked out, written while implementing the corresponding example. Useful as a model for [working out a protocol](usb-host-guide.md#5-working-out-a-protocol) yourself. **Written in Japanese**, except where an English page is noted below; each one carries an English summary at the top saying what it covers and where the current English documentation is.

| Document | Device / protocol | Example it backs |
|----------|-------------------|------------------|
| [vendor-api-spec.ja.md](vendor-api-spec.ja.md) | The vendor bulk/control API design | [`Vendor`](../examples/Vendor/) |
| [printer-spec.ja.md](printer-spec.ja.md) | USB Printer class and ESC/POS | [`EspUsbHostPrinterEscPos`](../examples/Vendor/EspUsbHostPrinterEscPos/) |
| [usbtmc-spec.ja.md](usbtmc-spec.ja.md) | USBTMC / USB488 and SCPI | [`EspUsbHostUsbtmcScpi`](../examples/Vendor/EspUsbHostUsbtmcScpi/) |
| [ccid-api-spec.ja.md](ccid-api-spec.ja.md) | CCID smart card readers, ATR, FeliCa | [`Ccid`](../examples/Ccid/) |
| [dp100-spec.ja.md](dp100-spec.ja.md) | ALIENTEK DP100: a framed protocol inside HID reports | [`EspUsbHostDp100Power`](../examples/HID/EspUsbHostDp100Power/) |
| [usb-network-spec.ja.md](usb-network-spec.ja.md) | CDC-NCM / CDC-ECM and the lwIP netif attach | [`UsbNetwork`](../examples/UsbNetwork/) |
| [usb-display-spec.ja.md](usb-display-spec.ja.md) — protocol findings also in English: [usb-display-spec.md](usb-display-spec.md) | The DL-1xx bulk display protocol (the AX206 and smart-screen protocols are in their example READMEs, indexed in [usb-display.md](usb-display.md)) | [`EspUsbHostDisplayDl1xx`](../examples/Vendor/EspUsbHostDisplayDl1xx/) |

## Design proposals

Design documents written before an API was added, kept for the reasoning behind it. **Japanese only.**

| Document | Status |
|----------|--------|
| [lifecycle-listener-proposal.ja.md](lifecycle-listener-proposal.ja.md) | Adopted and implemented |
| [midi-cable-discovery-proposal.ja.md](midi-cable-discovery-proposal.ja.md) | Cable count implemented; cable names not started |

## Generated reports

Produced by CI, not written by hand. Both workflows are dispatched manually after a release rather than on every push, so these files trail the current version until they are re-run.

| Document | Contents |
|----------|----------|
| [FOOTPRINT.md](FOOTPRINT.md) | Flash and RAM cost per feature, measured with the probe sketches in [`tools/footprint_sketches`](../tools/footprint_sketches/). `footprint.json` is the normalised source data |
| `COMPATIBILITY.<version>.md` | Per-release build results across arduino-esp32 core versions and targets. One file per released library version, added after that release, so pick the version you are on from this directory |

## Elsewhere in the repository

| Location | Contents |
|----------|----------|
| [README.md](../README.md) | API reference, supported classes, per-class status, examples index |
| [examples/](../examples/) | Runnable sketches, each with its own README |
| [tests/manual/README.md](../tests/manual/README.md) | Manual test catalog, plus known hub problems and channel limits |
| [tests/TEST_PLAN.md](../tests/TEST_PLAN.md) | Test strategy and why each category exists |
| [tests/probe/README.md](../tests/probe/README.md) | Throwaway bring-up and protocol-investigation sketches |
| [CHANGELOG.md](../CHANGELOG.md) | Release history, bilingual |
