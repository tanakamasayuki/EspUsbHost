# EspUsbHostMSCBlockDump

Reads capacity and device information from a USB Mass Storage device and dumps the first 64 bytes of LBA 0.

MSC support is block-level only. This example does not mount FAT or expose files.

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
