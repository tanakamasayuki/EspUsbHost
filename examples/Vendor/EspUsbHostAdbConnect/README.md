# EspUsbHostAdbConnect

> 日本語版: [README.ja.md](README.ja.md)

Skeleton for talking to an Android device over USB ADB. It locates the ADB interface, claims it through the generic vendor bulk APIs, sends the ADB `A_CNXN` handshake, and prints the device's first reply.

**This is a starting point, not a full ADB client.** It stops at the first reply. A working client must implement `A_AUTH` (RSA token signing / the on-device "always allow" dialog) before it can `A_OPEN` shell/sync streams.

## Hardware

- ESP32-S3 (or another board supported by Arduino-ESP32 USB Host)
- An Android device with **USB debugging enabled** (Developer options)

## How ADB rides on vendor-specific bulk

The ADB interface is a vendor-specific interface identified by:

| Field | Value |
|-------|-------|
| `bInterfaceClass` | `0xff` |
| `bInterfaceSubClass` | `0x42` |
| `bInterfaceProtocol` | `0x01` |

`vendorOpen()` selects vendor interfaces by class `0xff` and interface *number* only — it does not filter by subclass/protocol. So this sketch enumerates interfaces with `getInterfaces()`, finds the `ff/42/01` triple itself, and passes that interface number to `vendorOpen(address, number)`.

## What it does

1. On connect, finds the ADB interface number and claims it
2. After leaving the USB callback, sends `A_CNXN` (`host::` banner) from `loop()`
3. Reads bulk IN, reassembles ADB messages, and reports each one:
   - `CNXN` reply → device is already authorized
   - `AUTH` reply → device wants an RSA-signed token (not implemented here)

`vendorWrite()` waits synchronously for transfer completion, so it must not run in the connection callback on the USB client task. The callback only sets a pending flag and `loop()` performs the actual write.

## Serial commands

| Command | Action |
|---------|--------|
| `r` | Re-send `A_CNXN` |

## ADB message format (24-byte header)

| Offset | Field | Notes |
|--------|-------|-------|
| 0 | command | 4-byte tag, e.g. `CNXN` |
| 4 | arg0 | version / id |
| 8 | arg1 | maxdata / id |
| 12 | data_length | payload length |
| 16 | data_checksum | **plain byte sum**, not CRC32 |
| 20 | magic | `command ^ 0xffffffff` |

## Expected Serial output (authorized device)

```
EspUsbHost ADB connect skeleton start
connected: device: address=1 portId=0x01 vid=18d1 pid=4ee7 class=0x00(Device) speed=high product="Pixel"
ADB interface found: number=1
CNXN send: ok
recv CNXN arg0=0x01000001 arg1=0x00100000 len=... banner=device::ro.product.name=...
-> device accepted connection (already authorized).
```

On a device that has not authorized this host, expect an `AUTH` reply instead:

```
recv AUTH arg0=0x00000001 arg1=0x00000000 len=20
-> device requests AUTH (RSA token). Signing is not implemented in this skeleton.
```

## Next steps for a real client

- Implement `A_AUTH`: sign the 20-byte token with an RSA-2048 key (reply type `2`), or send the public key (type `3`) to trigger the "allow USB debugging?" dialog
- After the device sends `CNXN`, open a stream with `A_OPEN` (e.g. `"shell:ls\0"`) and pump `A_WRTE` / `A_OKAY`
- Note the 512-byte per-device receive buffer: keep advertised `maxdata` small and drain bulk IN promptly
