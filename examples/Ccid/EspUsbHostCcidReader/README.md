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
- Prints the card standard and name decoded from the ATR (`ccidGetCardInfo`)
- Sends the PC/SC Get UID pseudo APDU `FF CA 00 00 00` and prints the UID and status word
- Deactivates the card (`ccidPowerOff`)

A card already sitting on the reader when the sketch starts produces no insertion notification, so `ccidCardPresent()` is checked once right after opening.

## Card type

The card standard comes from the ATR. A contactless storage card has no ATR of
its own, so a PC/SC compliant reader synthesizes one carrying the PC/SC RID
`A0 00 00 03 06`, a standard byte (ISO 14443 A/B, ISO 15693, FeliCa, ...) and a
card name — that is what `ccidGetCardInfo()` reads. A card that answers with an
ATR of its own (a contact card, or a contactless card speaking ISO 14443-4)
carries no such identification and is reported as `ISO 7816 card (own ATR)`. An
ATR with no historical bytes at all identifies nothing and stays `unknown`.

For those, `ccidIdentifyCard()` asks the card instead: it sends Get UID and
infers the standard from the identifier (8 bytes = FeliCa IDm, or an ISO 15693
UID when it starts with `0xe0`; 7 or 10 bytes = ISO 14443 A NFCID1; 4 bytes
starting with `0x08` = the random NFCID1 a phone presents; other 4-byte
identifiers leave the ISO 14443 type open). That is a heuristic on the
identifier's shape, so `info.fromUid` marks it.

## Key APIs

- `usb.ccidOpen(address)` — claims the CCID interface and starts slot-change notifications
- `usb.ccidGetInterface(info, address)` — endpoints plus the CCID class descriptor values (slot count, `dwFeatures`, exchange level, `dwMaxCCIDMessageLength`)
- `usb.ccidGetStatus(status, slot, address)` / `usb.ccidCardPresent(slot, address)` — slot state
- `usb.ccidPowerOn(atr, capacity, &length, voltage, slot, address)` — activates the card and returns its ATR
- `usb.ccidGetCardInfo(info, slot, address)` — card standard (ISO 14443 A/B, ISO 15693, FeliCa, ...), level and card name, decoded from the ATR
- `usb.ccidIdentifyCard(info, slot, address)` — the same, plus a Get UID fallback for cards the ATR does not identify
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
card: ISO 14443 A level 3, MIFARE Classic 1K (0x0001)
Get UID: sw=9000
UID: 6b6dccae
```
