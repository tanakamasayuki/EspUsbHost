# EspUsbHostAdbConnect

> 日本語版: [README.ja.md](README.ja.md)

A minimal practical ADB host template built only on the generic `EspUsbHost` vendor-bulk API. ADB remains example-local rather than becoming part of the library core. The example provides the common foundation most ADB applications need:

- discovery and claiming of the `ff/42/01` ADB interface
- ADB packet encoding, checksums, receive reassembly, and validation
- first-run RSA-2048 key generation with NVS persistence
- SHA-1/PKCS#1 v1.5 signing of `AUTH TOKEN`
- public-key enrollment through Android's USB debugging dialog
- one `OPEN` / `OKAY` / `WRTE` / `CLSE` stream
- execution of `shell:echo ESP_USB_HOST_ADB_OK`

This is not a complete ADB client. Multiple streams, interactive shell I/O, `sync:`, shell v2, forwarding, and TLS are intentionally left as extensions.

## Requirements and first connection

Use an ESP32-S2/S3/P4 supported by Arduino-ESP32 USB Host, an Android device with USB debugging enabled, and a data-capable cable. Keep Android unlocked on the first connection and approve the USB debugging dialog.

The key is stored as `rsa-key` in the `esp-adb` NVS namespace, so later connections authenticate with the same signature key. If the dialog does not appear, revoke USB debugging authorizations in Android Developer options, toggle USB debugging off and on, then reconnect while unlocked.

## Expected output

On success the serial monitor shows:

```text
ADB send: AUTH SIGNATURE
ADB send: AUTH RSAPUBLICKEY
ADB connected: version=0x01000001 maxdata=4096
ADB send: OPEN shell:echo ESP_USB_HOST_ADB_OK
ADB stream data: ESP_USB_HOST_ADB_OK
[PASS]
```

If the key is already authorized, the public-key send and the approval dialog are skipped.

## Important implementation details

`vendorWrite()` waits synchronously for completion. The connection callback therefore only sets a flag; `loop()` performs all ADB writes outside the USB client task.

ADB sends its 24-byte header and payload as separate USB transfers. A payload whose length is an exact multiple of the Bulk OUT max-packet size must be followed by a zero-length packet (ZLP). This matters for a 256-byte RSA signature on a 64-byte full-speed endpoint; without the ZLP, adbd can wait for more payload and consume the next ADB header into the same USB transfer. The sketch enables `vendorSetAutoZlp(true)` after `vendorOpen()`, so the library appends the ZLP whenever a write ends on a packet boundary.

Incoming data is copied from `onVendorData()` into a dedicated ring buffer. This avoids losing the beginning of a long `CNXN` banner when relying only on the generic API's small built-in read buffer.

## Extension points

- Change the command through `SHELL_SERVICE` and its completion marker through `EXPECTED_OUTPUT`.
- For an interactive shell, keep the stream open, queue outgoing data, and send each next `WRTE` only after peer `OKAY`.
- For multiple streams, replace the single IDs/state with a table keyed by local stream ID.
- For shell v2, open `shell,v2,raw:` and decode its stdout, stderr, and exit packets inside the ADB stream.
- For file transfer, open `sync:` and implement the SYNC `SEND`, `RECV`, `DATA`, and `DONE` subprotocol.
- Services such as logcat use the same `A_OPEN` path but need long-lived stream and flow-control handling.

Production use should also address encrypted or provisioned key storage, fingerprint display, timeouts, reconnection, and per-stream backpressure. This template advertises ADB version `0x01000000` to demonstrate legacy RSA AUTH and does not negotiate STLS/TLS.

## Validation

The matching real-device manual test is:

```sh
cd tests
uv run --env-file .env pytest manual/adb_connect/adb_connect.py -v -s
```

It has been validated on a Pixel 6a through first authorization, saved-key authentication, one shell stream, echo output, and stream close.
