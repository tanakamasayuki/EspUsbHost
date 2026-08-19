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
