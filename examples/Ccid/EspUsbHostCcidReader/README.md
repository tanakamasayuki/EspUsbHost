# EspUsbHostCcidReader

> 日本語版: [README.ja.md](README.ja.md)

Demonstrates the CCID smart card reader APIs: claiming a `bInterfaceClass == 0x0b` interface, card insertion and removal notifications, card activation with its ATR, and APDU exchange.

## Hardware

- ESP32-S3 (or another board supported by Arduino-ESP32 USB Host)
- A CCID smart card reader — verified with a Sony RC-S300 (`FeliCa Port/PaSoRi 4.0`, VID 0x054c PID 0x0dc8)
- A smart card. For a contactless reader, any ISO 14443 card (transit card, ID card, ...)

## What it does

- Claims the CCID interface on connect (`ccidOpen`) and prints what the reader's class descriptor reports
- Prints card insertion and removal reported over the reader's interrupt IN endpoint
- Activates the card (`ccidPowerOn`) and prints its ATR
- Sends the PC/SC Get UID pseudo APDU `FF CA 00 00 00` and prints the UID and status word
- Deactivates the card (`ccidPowerOff`)

A card already sitting on the reader when the sketch starts produces no insertion notification, so `ccidCardPresent()` is checked once right after opening.

## Key APIs

- `usb.ccidOpen(address)` — claims the CCID interface and starts slot-change notifications
- `usb.ccidGetInterface(info, address)` — endpoints plus the CCID class descriptor values (slot count, `dwFeatures`, exchange level, `dwMaxCCIDMessageLength`)
- `usb.ccidGetStatus(status, slot, address)` / `usb.ccidCardPresent(slot, address)` — slot state
- `usb.ccidPowerOn(atr, capacity, &length, voltage, slot, address)` — activates the card and returns its ATR
- `usb.ccidApdu(apdu, length, response, capacity, &responseLength, &statusWord, slot, address)` — APDU exchange with SW1SW2 split off
- `usb.ccidTransfer(...)` — raw `PC_to_RDR_XfrBlock`, without the status-word split
- `usb.ccidEscape(...)` — reader-specific commands
- `usb.ccidMessage(type, messageSpecific, data, length, response, slot, address)` — any other CCID message
- `usb.onCcidCardInserted(callback)` / `usb.onCcidCardRemoved(callback)` — slot-change notifications, called on the USB task

The commands are synchronous and must not be called from a USB callback; the callbacks here only set a flag that `loop()` acts on.

## Expected Serial output

```
EspUsbHost CCID reader example start
connected: address=2 vid=054c pid=0dc8 product="FeliCa Port/PaSoRi 4.0"
CCID reader ready: address=2 interface=0 slots=1 exchange=extended APDU interrupt=yes
ATR: 3b8f8001804f0ca000000306030001000000006a
Get UID: sw=9000
UID: 6b6dccae
```
