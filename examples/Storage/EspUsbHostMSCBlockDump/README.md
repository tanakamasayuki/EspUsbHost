# EspUsbHostMSCBlockDump

> 日本語版: [README.ja.md](README.ja.md)

Reads capacity and device information from a USB Mass Storage device and dumps the first 64 bytes of LBA 0.

MSC support is block-level only. This example does not mount FAT or expose files.

> **Caution:** USB MSC support in this library is experimental. It should work for simple read and write use cases, but an SPI-connected SD card is usually a better choice when storage reliability or speed matters. Unless USB connectivity is required, USB MSC is not recommended as the default storage solution.

## Hardware

- ESP32-S3 (or another board supported by Arduino-ESP32 USB Host)
- USB Mass Storage device (USB flash drive, USB card reader, etc.)

## What it does

- Prints device VID, PID, and product string on connect
- Runs `mscInquiry()`, `mscRequestSense()`, `mscTestUnitReady()`, and `mscCapacity()` to gather device information
- Dumps the first 64 bytes of LBA 0 as hex
- If LBA 0 has an MBR signature, decodes and prints the partition table entries

## Notes

- MSC commands block until the USB transfer completes. They must be called from `loop()`, not from USB event callbacks.
- `mscReady()` returns true once the device is mounted and ready for block I/O.
- The sketch reads only once per connection (`printed` flag); disconnect and reconnect to read again.
- Block sizes larger than 512 bytes are reported but not dumped.
- MBR detection requires the `0x55 0xaa` boot signature at bytes 510–511 of LBA 0. GPT disks and freshly formatted cards may not have it.

## Key APIs

- `usb.mscReady()` — true when a USB Mass Storage device is ready for I/O
- `usb.mscInquiry(inquiry)` — fills `EspUsbHostMscInquiry` with vendor, product, revision, and removable flag
- `usb.mscRequestSense(sense)` — fills `EspUsbHostMscSense` with response code, sense key, ASC, and ASCQ
- `usb.mscTestUnitReady()` — returns true when the media is present and ready
- `usb.mscCapacity(blockCount, blockSize)` — returns total block count and block size in bytes
- `usb.mscReadBlocks(lba, buffer, count)` — reads `count` blocks starting at `lba` into `buffer`
- `usb.lastErrorName()` — human-readable name of the last error

## Expected Serial output

```
connected: device: address=1 portId=0x01 vid=058f pid=6387 class=0x00(Device) speed=full product="Mass Storage Device"
MSC inquiry: removable=1 vendor='VendorCo' product='ProductCode' revision='1.00'
MSC sense: response=0x70 key=0x00 asc=0x00 ascq=0x00
MSC test unit ready: 1
MSC capacity: blocks=7831552 block_size=512 total_kib=3993600
LBA0:
0000: eb 58 90 4d 53 44 4f 53 35 2e 30 00 02 08 20 00
0016: 02 00 00 00 00 f8 00 00 3f 00 ff 00 00 08 00 00
0032: 00 08 77 00 f4 02 00 00 00 00 00 00 02 00 00 00
0048: 01 00 06 00 00 00 00 00 00 00 00 00 00 00 00 00
MBR partitions:
  #1 boot=0x00 type=0x0b first_lba=8192 sectors=7823360 size_kib=3911680
```

## Walkthrough and checks

### Before running

Read storage capacity and inspect the first raw block. Follow the hardware and dependency requirements above, select the board and the example's profile, and locate the diagnostic Serial connection. [Example workflow](../../../docs/example-workflow.md) explains installation, connectors, USB roles and the difference between startup and a usable peer.

### Follow the code

After `mscReady()`, request capacity before reading LBA zero. blockSize determines buffer requirements; LBA is a block number, not a byte offset. The sample inspects raw data without mounting a filesystem, so an MBR signature alone does not mean files can be listed.

### API use and adaptation

A block is addressed by LBA and sized by the medium; a file needs a filesystem on top. Compare capacity, mount success and file operations separately. Check returned byte counts, close handles, and allow outstanding host writes to finish before removing or reusing storage. Neither USB connection nor a FAT signature guarantees a valid filesystem.

Give the medium one writer at a time. A host-mounted volume and the sketch must not independently edit the same filesystem. RAM examples lose changes on reset; physical-media examples can change persistent data. Read the operation list before using a medium containing files you want to retain.

### Verify one change at a time

Run the unchanged example first and compare the operation above with its logs or host application. Test the peer absent and then connected; note which stage succeeds instead of expecting the final result immediately. Reset to the original configuration before changing one pin, format or command. The demonstration's error logging is not a general retry or recovery policy.

- [Sketch](EspUsbHostMSCBlockDump.ino)
- [All examples](../../README.md)
- [Example workflow](../../../docs/example-workflow.md)
