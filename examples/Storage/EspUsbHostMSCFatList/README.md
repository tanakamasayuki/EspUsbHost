# EspUsbHostMSCFatList

Mounts a USB Mass Storage device through `EspUsbHostMscFS` at `/usb`, lists the root directory with the Arduino `fs::FS` / `File` API, then writes, reads back, and deletes a small probe file.

Use a USB flash drive that may be safely written by the sketch.

## What it demonstrates

- Mounting FAT with `EspUsbHostMscFS::begin(usb, "/usb")` and using it as a `fs::FS`-compatible object
- Checking the mount state with `EspUsbHostMscFS::mounted()`
- Listing files with `File root = fs.open("/")` and `openNextFile()`
- Writing and reading a file with `File::print()` / `File::readBytes()`

For basic `fs::FS` use, sketches do not need to call `mscReady()`, `mscWaitReady()`, or `mscGetBlockDeviceInfo()` directly. `EspUsbHostMscFS::begin()` fails while no MSC device is usable, so the sketch waits briefly and retries. Use the lower-level MSC APIs only when you need block device details.

`EspUsbHostMscFS` derives from `fs::FS`, so it can be passed to Arduino libraries such as WebServer or Update that accept `fs::FS &`. Use it from `loop()`, not from USB callbacks. Removing a USB drive while files are open or writes are in progress may lose unwritten data.

The current FatFs/VFS mount path depends on the ESP-IDF FatFs build and is limited to 32-bit sectors. Multiple MSC devices are constrained by HCD channel limits on ESP32-S3, so assume a single MSC device for practical use.
