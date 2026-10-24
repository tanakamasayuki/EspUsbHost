# EspUsbHostMSCFatList

> 日本語版: [README.ja.md](README.ja.md)

Mounts a USB Mass Storage device through `EspUsbHostMscFS` at `/usb`, lists the root directory with the Arduino `fs::FS` / `File` API, then writes, reads back, and deletes a small probe file.

Use a USB flash drive that may be safely written by the sketch.

> **Caution:** USB MSC support in this library is experimental. It should work for simple read and write use cases, but an SPI-connected SD card is usually a better choice when storage reliability or speed matters. Unless USB connectivity is required, USB MSC is not recommended as the default storage solution.

## Hardware

- ESP32-S3 (or another board supported by Arduino-ESP32 USB Host)
- FAT-formatted USB Mass Storage device (USB flash drive, USB card reader, etc.) that may be safely written

## What it demonstrates

- Mounting FAT with `EspUsbHostMscFS::begin(usb, "/usb")` and using it as a `fs::FS`-compatible object
- Checking the mount state with `EspUsbHostMscFS::mounted()`
- Listing files with `File root = fs.open("/")` and `openNextFile()`
- Writing and reading a file with `File::print()` / `File::readBytes()`

For basic `fs::FS` use, sketches do not need to call `mscReady()`, `mscWaitReady()`, or `mscGetBlockDeviceInfo()` directly. `EspUsbHostMscFS::begin()` fails while no MSC device is usable, so the sketch waits briefly and retries. Use the lower-level MSC APIs only when you need block device details.

`EspUsbHostMscFS` derives from `fs::FS`, so it can be passed to Arduino libraries such as WebServer or Update that accept `fs::FS &`. Use it from `loop()`, not from USB callbacks. Removing a USB drive while files are open or writes are in progress may lose unwritten data.

If SCSI `SYNCHRONIZE CACHE(10)` fails during FatFs sync, this mount automatically falls back to skipping it. For known non-compliant devices, call `usbMassStorage.setSkipSyncCache(true)` before `usbMassStorage.begin(...)` to skip it from the start. This improves compatibility with some devices but weakens explicit flush behavior.

The current FatFs/VFS mount path depends on the ESP-IDF FatFs build and is limited to 32-bit sectors. Multiple MSC devices are constrained by HCD channel limits on ESP32-S3, so assume a single MSC device for practical use.

## Expected Serial output

Until a usable MSC device is mounted, the sketch retries about once per second and prints `USB MSC FS mount failed: ...`. After a successful mount, the root listing depends on the contents of the drive:

```
connected: device: address=1 portId=0x01 vid=058f pid=6387 class=0x00(Device) speed=full product="Mass Storage Device"
Root entries:
  FILE README.TXT size=1024
  DIR  MUSIC size=0
Wrote 31 bytes to /ESPUSBHT.TST
Read back 31 bytes: EspUsbHost MSC FAT write probe
Removed /ESPUSBHT.TST
```

## Walkthrough and checks

### Before running

Mount FAT storage, list files and perform a write/read/delete probe. Follow the hardware and dependency requirements above, select the board and the example's profile, and locate the diagnostic Serial connection. [Example workflow](../../../docs/example-workflow.md) explains installation, connectors, USB roles and the difference between startup and a usable peer.

### Follow the code

Use spare writable media: `writeReadDeleteProbe()` writes /ESPUSBHT.TST and removes it after reading it back. Close each File before removal or unplugging. If that path already exists, its contents may be replaced; this example is not a read-only directory listing.

### API use and adaptation

A block is addressed by LBA and sized by the medium; a file needs a filesystem on top. Compare capacity, mount success and file operations separately. Check returned byte counts, close handles, and allow outstanding host writes to finish before removing or reusing storage. Neither USB connection nor a FAT signature guarantees a valid filesystem.

Give the medium one writer at a time. A host-mounted volume and the sketch must not independently edit the same filesystem. RAM examples lose changes on reset; physical-media examples can change persistent data. Read the operation list before using a medium containing files you want to retain.

### Verify one change at a time

Run the unchanged example first and compare the operation above with its logs or host application. Test the peer absent and then connected; note which stage succeeds instead of expecting the final result immediately. Reset to the original configuration before changing one pin, format or command. The demonstration's error logging is not a general retry or recovery policy.

- [Sketch](EspUsbHostMSCFatList.ino)
- [All examples](../../README.md)
- [Example workflow](../../../docs/example-workflow.md)
