# EspUsbHostMSCFatList

Mounts a USB Mass Storage device through ESP-IDF FatFs/VFS at `/usb`, lists the root directory, then writes, reads back, and deletes a small probe file.

Use a USB flash drive that may be safely written by the sketch.

## What it demonstrates

- Waiting for MSC media with `mscWaitReady()`
- Querying block device details with `mscGetBlockDeviceInfo()`
- Mounting FAT through `mscMount("/usb")`
- Listing files with POSIX `opendir()` / `readdir()` / `stat()`
- Writing and reading a file with `fopen()` / `fwrite()` / `fread()`
- Unmounting with `mscUnmount("/usb")`

The current FatFs/VFS mount path depends on the ESP-IDF FatFs build and is limited to 32-bit sectors. Arduino `FS` object integration is not provided yet.
