# EspUsbHost

> 日本語版: [README.ja.md](README.ja.md)

Arduino library for using USB Host on ESP32-S3 and ESP32-P4.

USB events are processed in a background FreeRTOS task, so `loop()` does not need to call any USB polling function. Register callbacks in `setup()`, call `begin()`, and the library handles the rest.

## Requirements

Minimum Arduino-ESP32 core (board package) version:

| Target | Minimum arduino-esp32 |
| --- | --- |
| ESP32-S2 / ESP32-S3 | 3.2.0 |
| ESP32-P4 | 3.3.1 |

Older cores are not supported: 3.1.x and earlier may fail to build. Per-library-version build results across core versions are published under [`docs/`](docs/) as `COMPATIBILITY.<version>.md`.

## Version 2 status

Version 2 is a ground-up redesign and is **not compatible with the 1.x API**. The old inheritance/virtual-method style is no longer the primary interface; use the callback-based APIs such as `onKeyboard()`, `onMouse()`, `onDeviceConnected()`, and class-specific send/start APIs.

This release has substantially broader USB class support than 1.x, but it is still a practical Arduino USB Host library under active development, not a fully validated replacement for every ESP-IDF USB class driver. APIs may still change incompatibly in later 2.x releases when that makes the API simpler, safer, or more consistent.

Test coverage is better than the 1.x series and includes example build checks plus peer/manual tests for major paths, but many combinations still depend on real hardware. Treat USB Audio, USB Hub edge cases, multi-device setups, and unusual/non-compliant USB devices as areas that need device-specific verification.

## Sibling Library: EspUsbDevice

The device-side sibling library is
[`EspUsbDevice`](https://github.com/tanakamasayuki/EspUsbDevice).
EspUsbDevice is a USB Device library expanded alongside EspUsbHost, and is used
for Host/Device combination tests and ESP32-P4 single-board loopback validation.

Arduino-ESP32's standard `USB`, `USBHIDKeyboard`, `USBHIDMouse`, `USBCDC`, and
similar APIs are convenient for short, common USB Device sketches. EspUsbDevice
instead focuses on explicit sketch-side control over port, speed, descriptors,
endpoint packet sizes, HID report IDs, output/feature reports, and raw class
reports.

For keyboards, the standard `USBHIDKeyboard` API is useful for sending simple
text, but it has limits when you need complete Japanese-layout coverage or exact
HID usages for keys such as Muhenkan, Henkan, Kana, Hankaku/Zenkaku, and
JIS-specific symbol keys. EspUsbDevice keeps raw HID usage/report control
available alongside text helpers so EspUsbHost keyboard layout behavior can be
tested precisely.

Use the standard Arduino-ESP32 USB Device APIs first when you only need a normal
keyboard, mouse, CDC, or similar device connected to a PC. Use EspUsbDevice when
you need detailed EspUsbHost validation, descriptors or reports that the
Arduino Core standard Device implementation cannot easily control, or ESP32-P4
Host/Device loopback tests.

## Features

- **HID input** — keyboard, mouse, consumer control (media keys), system control (power/standby), gamepad
- **HID output** — keyboard LED control, vendor output/feature reports
- **USB serial** — CDC ACM and common VCP devices (FTDI, CP210x, CH34x) via `EspUsbHostCdcSerial` (Arduino `Stream`/`Print` compatible)
- **MIDI** — USB MIDI input and output
- **USB audio** — raw isochronous IN payloads and isochronous OUT writes for USB Audio streaming interfaces
- **USB Mass Storage** — USB Mass Storage Bulk-Only Transport with SCSI capacity/read/write block access, FatFs/VFS mounting, and Arduino `fs::FS` / `File` compatibility
- **USB network** — CDC-NCM / CDC-ECM USB Ethernet adapters, either as raw Ethernet frames or attached as an lwIP (`esp_netif`) interface so `NetworkClient` / `HTTPClient` run over USB with no Wi-Fi
- **Vendor bulk/control** — generic non-HID vendor-specific interfaces with bulk IN/OUT, an asynchronous bulk OUT queue with zero-copy buffers and automatic ZLP handling, and EP0 vendor requests
- **Device discovery** — enumerate connected devices, interfaces, and endpoints
- **Multiple devices** — each callback and send API accepts an optional `address` parameter to target a specific device

## Roadmap

### USB class support

| Class | Status |
|-------|--------|
| HID — keyboard, mouse, gamepad, consumer control, system control, vendor | ✅ Done |
| USB serial — CDC ACM and VCP (FTDI, CP210x, CH34x) via `EspUsbHostCdcSerial`; baud, data bits, parity, and stop bits are configurable | ✅ Done |
| USB MIDI | ✅ Done |
| Vendor-specific bulk/control | ✅ Basic support implemented. Covers explicit interface claim, bulk IN/OUT (synchronous and an asynchronous queue), automatic ZLP, and EP0 vendor IN/OUT requests |
| USB graphics adapter (DL-1xx bulk protocol) | 📄 Example only, best effort. Implemented in [`examples/Vendor/EspUsbHostDisplayDl1xx`](examples/Vendor/EspUsbHostDisplayDl1xx/) on top of the vendor bulk API — nothing display-specific is in the library. A reference implementation for one chip family at 16 bpp; for other adapters or higher frame rates use a dedicated library such as [Pico_USB_Disp](https://github.com/htlabnet/Pico_USB_Disp) |
| UAC — USB audio input/output | 🔲 Experimental. Audio OUT/IN are peer-tested with the standard Arduino `USBAudioCard`; real USB microphone/audio-interface validation remains |
| HUB — hub detection, topology info, and port power control | ✅ Basic support implemented. `hub_info` and `hub_power` manual tests pass; change-bit handling, cascaded hubs, and USB 3.x hub compatibility remain ongoing |
| CDC-NCM / CDC-ECM — USB Ethernet with raw frame access and lwIP netif attach | 🔲 Experimental. Peer-tested against the EspUsbDevice `UsbNetwork` sketch and an AX88179A adapter. Adapters whose network function is not in the default configuration need `setConfigurationSelector()` and two enumeration passes |
| MSC — USB storage block I/O and FatFs/Arduino FS mount | 🔲 Experimental. Basic read/write and FatFs mounting with a single MSC device are peer/manual tested. Non-compliant devices, multiple MSC devices/LUNs, and full abnormal BOT recovery need further validation |
| UVC — USB camera | ❌ Currently unsupported. The configuration-descriptor length limit in Arduino-ESP32's precompiled USB Host stack prevents typical UVC devices from enumerating |

### Other planned features

| Feature | Status |
|---------|--------|
| `onHIDReportDescriptor()` — HID report descriptor access | ✅ Done |
| HID gamepad input — descriptor-decoded fields plus raw/report bytes for user-defined mapping | ✅ Mappable event API; mapping helpers still under consideration |
| Channel count and endpoint usage visibility | ✅ Implemented as experimental diagnostics; useful for understanding limits with multi-device, Audio, MSC, and HUB combinations |
| USB Audio IN real payload validation | ✅ Peer validation complete with standard Arduino `USBAudioCard`; real USB microphone/audio-interface validation remains |
| ESP32-P4 validation | 🔲 Ongoing; verify FS/HS OTG, hub behavior, and loopback tests separately |
| Loopback tests (ESP32-P4 single-board) | 🔲 In progress in `EspUsbDevice`; `tests/loopback` in this repository only contains README files |
| Manual tests — VCP serial, multi-device, hot-plug | ✅ Main cases confirmed; additional device compatibility remains ongoing |

## Current limits and cautions

- **Version compatibility:** 2.x is not source-compatible with 1.x. Existing 1.x sketches should be ported to the callback-based API and the new class-specific APIs.
- **Future breaking changes:** more incompatible API changes may still happen in 2.x while the API is being shaped around real devices and examples.
- **USB host resources:** ESP32-S3 has a small number of USB host channels. Composite devices, hubs, audio, MSC, and multiple serial devices can exhaust channels quickly. Use `printDeviceInfo()` / `printAllDeviceInfo()` and the endpoint/channel diagnostic APIs to inspect resource use.
- **Hubs:** use a self-powered USB 2.0 hub for multi-device tests. USB 3.x hubs and internally cascaded hubs may behave differently and are not fully validated.
- **USB Audio:** input/output peer tests pass with the standard Arduino `USBAudioCard`. Real microphone/audio-interface validation is still limited. UAC2 clock/selector behavior and advanced Audio Control units are limited.
- **UVC / USB cameras:** currently unsupported. Arduino-ESP32's precompiled USB Host stack is built with `CONFIG_USB_HOST_CONTROL_TRANSFER_MAX_SIZE=256`, so a USB device whose configuration descriptor exceeds 256 bytes fails during enumeration, before a class driver can start. The Logitech C920 tested here has a 1,974-byte descriptor. A sketch or EspUsbHost build option cannot change this value because it belongs to the build configuration used to generate Arduino-ESP32's precompiled libraries. UVC support will be reconsidered if Arduino-ESP32 removes or raises this limit.
- **Mass Storage:** FAT access is intended for a single practical MSC device. Multiple MSC devices, multiple LUN devices, unusual block sizes, and abnormal BOT recovery need more real-device validation. Non-compliant devices may require the `SYNCHRONIZE CACHE(10)` fallback described in the MSC section.
- **Hot plug:** unplugging while files, serial transfers, audio streams, or class operations are active can still fail or lose data depending on device behavior.
- **ESP32-P4:** FS/HS OTG selection is supported through `EspUsbHostConfig::port`, but P4 validation is still ongoing. HS OTG has practical limitations, especially with hubs.

## Requirements

- ESP32-S3, or any board supported by Arduino-ESP32 USB Host
- Arduino-ESP32 core

### Recommended ESP32-S3 hardware and cautions

Before using a board as a USB Host, first verify whether its USB connector supplies VBUS (5 V) to the attached USB device.

The official Espressif Systems ESP32-S3-DevKitC-1 does not supply power to an attached device through its USB OTG connector. This is convenient when the board is used as a USB Device, but in USB Host mode you must either wire a separate power supply to the attached USB device or use an externally powered (self-powered) USB hub.

Some M5Stack products can control USB connector power from software. Check the schematic and power-control procedure for the specific product you use.

For a straightforward USB Host setup, we recommend a board such as the Freenove ESP32-S3-WROOM Board, which can power an attached device through its USB Type-C OTG connector.

The final application can use a product with only one USB connector, such as the AtomS3. During development, however, we recommend a board with two USB connectors so that the USB-to-UART connector used for flashing and the Serial Monitor remains separate from the USB OTG connector used for the attached device.

Even when a board has two USB connectors, which one is connected to the USB-to-UART bridge and which one is connected to the ESP32-S3 USB OTG peripheral depends on the board. For example, the USB-to-UART and USB OTG connector positions are reversed between the official Espressif Systems ESP32-S3-DevKitC-1 and the Freenove ESP32-S3-WROOM Board. Do not rely on connector position alone; check the board silkscreen, product documentation, and schematic.

### ESP32-P4 notes

ESP32-P4 contains three USB functions. These are controllers/PHY paths inside the SoC, not necessarily three physical connectors on every board:

1. **USB Serial/JTAG** — a fixed-function Full-speed USB controller for flashing, console CDC, and JTAG.
2. **USB OTG FS** — a programmable Full-speed/Low-speed OTG controller that can operate as Host or Device.
3. **USB OTG HS** — a programmable High-speed OTG controller with dedicated USB pins that can operate as Host or Device.

A board may expose all three, combine roles on a connector, or expose only some of them. It may also add an external USB-to-UART bridge such as CH34x or CP210x. That bridge is not one of the P4 USB controllers, so such a board can appear to have four USB-related connectors or paths. Common arrangements include:

- USB OTG HS + USB OTG FS + built-in USB Serial/JTAG
- USB OTG HS + built-in FS USB Serial/JTAG + external USB-to-UART serial
- USB OTG HS + USB OTG FS + built-in USB Serial/JTAG + external USB-to-UART serial

Check the board schematic rather than relying on connector labels such as `USB`, `OTG`, `UART`, or `DOWNLOAD`. An external USB-to-UART connector carries a normal P4 UART behind the bridge and is independent of the built-in USB Serial/JTAG and OTG controllers.

The chip-level signal pins are as follows. Board connectors may be wired differently, so always check the board schematic before wiring or choosing a port.

| Typical ESP32-P4 role | D- | D+ | Notes |
|------------------------|----|----|-------|
| USB CDC FS / USB Serial/JTAG FS | GPIO24 | GPIO25 | Commonly used for built-in USB Serial/JTAG or a FS device-side CDC connector. This connector is easy to confuse with USB Host on some boards. |
| USB OTG FS | GPIO26 | GPIO27 | Commonly used as the full-speed OTG connector; selectable as USB Host with `ESP_USB_HOST_PORT_FULL_SPEED`. |
| USB OTG HS | package pin 49 | package pin 50 | High-speed OTG port; these are dedicated USB pins, not general GPIOs. Select with `ESP_USB_HOST_PORT_HIGH_SPEED`. |

ESP32-P4 has two internal Full-speed/Low-speed PHYs. USB OTG FS and USB Serial/JTAG are mapped to opposite PHYs:

| Mapping | GPIO24/GPIO25 (FSLS PHY0) | GPIO26/GPIO27 (FSLS PHY1) |
|---------|----------------------------|----------------------------|
| Default | USB Serial/JTAG | USB OTG FS |
| Swapped | USB OTG FS | USB Serial/JTAG |

The assignment can be swapped permanently with the `USB_PHY_SEL` eFuse or temporarily at runtime. This is a one-to-one swap, not signal duplication: USB OTG FS and USB Serial/JTAG always use different FSLS PHYs. **Burning the eFuse is irreversible and is not recommended just to support a particular board connector.** The runtime override is normally the safer choice during development and for board-specific firmware.

ESP-IDF exposes the runtime mapping through its P4 low-level HAL. Call it before `usb.begin()` and before any other USB OTG FS driver is initialized:

```cpp
#include "hal/usb_wrap_ll.h"
#include "soc/usb_wrap_struct.h"

// Route USB OTG FS to GPIO24/GPIO25. USB Serial/JTAG moves to GPIO26/GPIO27.
usb_wrap_ll_phy_select(&USB_WRAP, 0);

EspUsbHostConfig config;
config.port = ESP_USB_HOST_PORT_FULL_SPEED;
usb.begin(config);
```

`usb_wrap_ll_phy_select(&USB_WRAP, 0)` maps USB OTG FS to FSLS PHY0 (GPIO24/GPIO25) and simultaneously maps USB Serial/JTAG to FSLS PHY1 (GPIO26/GPIO27). Passing `1` applies the opposite mapping. It does not make the same USB function appear on both pin pairs. This is a software override of the FS PHY mapping; it does not burn an eFuse. Resetting the chip returns startup control to the configured eFuse/default mapping.

Moving USB Serial/JTAG to GPIO26/GPIO27 does not create or initialize another CDC stack. It relocates the built-in fixed-function USB Serial/JTAG controller; if that controller is enabled, its CDC/JTAG USB device can therefore operate on GPIO26/GPIO27. If the serial monitor, flashing connection, or JTAG debugger is using GPIO24/GPIO25, it disconnects when OTG FS is moved there. Use an external USB-to-UART connector, another available console, or connect USB Serial/JTAG to GPIO26/GPIO27 when logs are needed after the switch.

Do not change the mapping while an OTG FS Host or Device driver is running. If another framework initialized OTG FS earlier, stop and uninstall that driver before changing the route, then initialize `EspUsbHost`. In a normal sketch that has not started another FS stack, calling the function immediately before `usb.begin(config)` is sufficient. USB Serial/JTAG may already have been initialized by ROM or the Arduino core; switching it causes a physical USB disconnect from the old PHY and possible re-enumeration on the new PHY.

This routing changes D+/D- connectivity only. USB Host also requires correct VBUS sourcing, over-current protection, and, for USB-C, appropriate role/CC handling supplied by the board hardware. Never assume that a connector wired to GPIO24/GPIO25 is electrically capable of Host mode from PHY routing alone. Because USB Serial/JTAG moves onto GPIO26/GPIO27, ensure that connector is not still sourcing Host VBUS and is not connected to a device that conflicts with the relocated USB Serial/JTAG device role.

GPIO26/GPIO27 cannot remain in use as ordinary GPIOs or by another peripheral while USB Serial/JTAG is mapped to them. Stop that GPIO/peripheral use before changing the USB route. If the USB mapping is later restored and GPIO26/GPIO27 are returned to another purpose, initialize those pins again with `pinMode()` or restart the owning peripheral driver with its `begin()`/configuration API. USB PHY setup can change the pin mux, direction, and pull configuration, so the previous GPIO/peripheral initialization must not be assumed to remain valid.

Only the OTG ports are usable as USB Host ports. Some boards make it hard to tell which connector is FS OTG versus a CDC/device connector, so check the board schematic and examples carefully.

The ESP-IDF USB Host stack can use only one host peripheral at a time. On ESP32-P4, choose either FS OTG or HS OTG with `EspUsbHostConfig::port`; you cannot run both as USB Host simultaneously. In Arduino-library use, the USB device function uses HS, and FS cannot be selected for that device role.

On ESP32-P4 the DMA-capable memory used for USB transfers is cached, and ESP-IDF's host driver only invalidates an IN buffer after the transfer completes — it never writes the buffer back before the DMA starts. Cache lines left dirty by the allocator can therefore be evicted over data the controller is still writing. This library writes those lines back with `esp_cache_msync()` immediately before every IN submit, so no application-side workaround is needed; `tests/manual/msc_cache_coherency` verifies this on real hardware.

High-speed OTG support is still limited in practice. USB hubs are effectively not usable on HS OTG in the current environment. This library has confirmed a USB keyboard working as USB Host on ESP32-P4, but detailed ESP32-P4 validation is still incomplete.

## Installation

Open the Arduino IDE Library Manager, search for **EspUsbHost**, and install.

Or clone this repository into your Arduino `libraries/` folder:

```sh
git clone https://github.com/tanakamasayuki/EspUsbHost
```

## Quick start

```cpp
#include "EspUsbHost.h"

EspUsbHost usb;

void setup() {
  Serial.begin(115200);

  usb.onKeyboard([](const EspUsbHostKeyboardEvent &event) {
    if (event.pressed && event.ascii) {
      Serial.print((char)event.ascii);
    }
  });

  if (!usb.begin()) {
    Serial.printf("usb.begin() failed: %s\n", usb.lastErrorName());
  }
}

void loop() {
}
```

## Examples

### HID

| Sketch | Description |
|--------|-------------|
| [EspUsbHostKeyboard](examples/HID/EspUsbHostKeyboard/) | Read keyboard input and print typed characters to Serial |
| [EspUsbHostKeyboardDump](examples/HID/EspUsbHostKeyboardDump/) | Dump parsed keyboard events and show how to handle `onKeyboard` yourself |
| [EspUsbHostKeyboardNKRO](examples/HID/EspUsbHostKeyboardNKRO/) | Host an N-key rollover keyboard and count simultaneously-held keys |
| [EspUsbHostMouse](examples/HID/EspUsbHostMouse/) | Read mouse movement and button events |
| [EspUsbHostCompositeHID](examples/HID/EspUsbHostCompositeHID/) | Handle composite HID devices such as keyboard + mouse devices |
| [EspUsbHostConsumerControl](examples/HID/EspUsbHostConsumerControl/) | Detect media keys (volume, play/pause, etc.) |
| [EspUsbHostSystemControl](examples/HID/EspUsbHostSystemControl/) | Detect system keys (power, standby, wake) |
| [EspUsbHostGamepad](examples/HID/EspUsbHostGamepad/) | Read gamepad axes, hat switch, and buttons |
| [EspUsbHostHIDVendor](examples/HID/EspUsbHostHIDVendor/) | Vendor HID input and output/feature reports |
| [EspUsbHostHIDRawDump](examples/HID/EspUsbHostHIDRawDump/) | Raw hex dump with device address (supports multiple devices) |

### Info

| Sketch | Description |
|--------|-------------|
| [EspUsbHostDeviceInfo](examples/Info/EspUsbHostDeviceInfo/) | Print device descriptors, interfaces, and endpoints for all connected devices |
| [EspUsbHostHIDReportDescriptor](examples/Info/EspUsbHostHIDReportDescriptor/) | Print HID report descriptors and a simple item decode for HID investigation |
| [EspUsbHostCustomDeviceCallbacks](examples/Info/EspUsbHostCustomDeviceCallbacks/) | Define custom connect/disconnect callbacks and inspect connected devices |
| [EspUsbHostHubPPPS](examples/Info/EspUsbHostHubPPPS/) | Control power on a PPPS-capable USB hub port |
| [EspUsbHostP4FsPhyRouting](examples/Info/EspUsbHostP4FsPhyRouting/) | Route ESP32-P4 USB OTG FS to the GPIO24/GPIO25 USB connector at runtime |

### MIDI

| Sketch | Description |
|--------|-------------|
| [EspUsbHostMIDI](examples/MIDI/EspUsbHostMIDI/) | USB MIDI input and output |

### Audio

| Sketch | Description |
|--------|-------------|
| [EspUsbHostAudioInput](examples/Audio/EspUsbHostAudioInput/) | Receive USB Audio isochronous IN payloads |
| [EspUsbHostAudioOutputTone](examples/Audio/EspUsbHostAudioOutputTone/) | Generate a simple tone and send it to USB Audio OUT |
| [EspUsbHostAudioOutputHardwareVolume](examples/Audio/EspUsbHostAudioOutputHardwareVolume/) | Check USB Audio Feature Unit mute and hardware volume support |
| [EspUsbHostAudioOutputMP3PCMFlow](examples/Audio/EspUsbHostAudioOutputMP3PCMFlow/) | Decode embedded MP3 assets with PCMFlow and play them to USB Audio OUT |
| [EspUsbHostAudioOutputMP3ESP8266Audio](examples/Audio/EspUsbHostAudioOutputMP3ESP8266Audio/) | Decode embedded MP3 assets with ESP8266Audio and play them to USB Audio OUT |

### Serial

| Sketch | Description |
|--------|-------------|
| [EspUsbHostUSBSerial](examples/Serial/EspUsbHostUSBSerial/) | Bidirectional serial bridge (CDC ACM and VCP) |
| [EspUsbHostMultiUSBSerial](examples/Serial/EspUsbHostMultiUSBSerial/) | Use FTDI and CP210x USB serial devices at the same time |

### Storage

| Sketch | Description |
|--------|-------------|
| [EspUsbHostMSCBlockDump](examples/Storage/EspUsbHostMSCBlockDump/) | Print MSC capacity and dump the first block |
| [EspUsbHostMSCFatList](examples/Storage/EspUsbHostMSCFatList/) | Mount MSC as Arduino `fs::FS`, list files, and run a small write/read/delete probe |

### Network

| Sketch | Description |
|--------|-------------|
| [UsbNetwork](examples/UsbNetwork/) | Bring up a CDC-NCM/ECM USB Ethernet adapter as a DHCP-client lwIP netif and run an `HTTPClient` GET over USB. Prints the CDC-ECM/NCM candidates found in every configuration on connect |

### Vendor

| Sketch | Description |
|--------|-------------|
| [EspUsbHostVendorBulk](examples/Vendor/EspUsbHostVendorBulk/) | Generic non-HID vendor-specific interface: bulk IN/OUT and EP0 vendor control IN/OUT |
| [EspUsbHostAdbConnect](examples/Vendor/EspUsbHostAdbConnect/) | Authenticate Android ADB and run one shell stream over the generic vendor-bulk API |
| [EspUsbHostDisplayDl1xx](examples/Vendor/EspUsbHostDisplayDl1xx/) | Drive a USB graphics adapter (DL-1xx bulk protocol) as a LovyanGFX panel, with LGFXVirtualCanvas for a Full HD surface |

## API reference

### Core

```cpp
bool begin();
bool begin(const EspUsbHostConfig &config);
void end();
bool ready() const;
bool setConfigurationSelector(ConfigurationSelector selector);
```

`setConfigurationSelector()` is registered before `begin()` and returns the
configuration value to activate for a given device descriptor (`0` keeps the
device default). It runs on the USB Host task during enumeration and must not
block. It requires Arduino-ESP32 3.3.11 or later (`enum_filter_cb`); on older
cores it returns `false` with `ESP_ERR_NOT_SUPPORTED`. It is mainly needed by
USB Ethernet adapters that hide CDC-NCM/ECM in a non-default configuration.

`end()` synchronously stops the client and daemon tasks, cancels and drains
in-flight endpoint transfers, deregisters the client, waits for the IDF
`ALL_FREE` handshake, and uninstalls the USB Host Library. After it returns,
the same `EspUsbHost` object can be started again with `begin()`. Call `end()`
from the application task, not from a USB event/data callback.

`EspUsbHostConfig` lets you adjust the background task stack size, priority, and core affinity:

```cpp
struct EspUsbHostConfig {
  uint32_t    taskStackSize = 4096;
  UBaseType_t taskPriority  = 5;
  BaseType_t  taskCore      = tskNO_AFFINITY;
  EspUsbHostPort port       = ESP_USB_HOST_PORT_DEFAULT;
};
```

On ESP32-P4, set `port` to `ESP_USB_HOST_PORT_FULL_SPEED` or `ESP_USB_HOST_PORT_HIGH_SPEED` when you need to choose a specific OTG peripheral. Other chips ignore this setting.

### Device events

```cpp
void onDeviceConnected(DeviceCallback callback);
void onDeviceDisconnected(DeviceCallback callback);
void espUsbHostPrint(const EspUsbHostDeviceInfo &device, Print &out = Serial);
```

Callbacks receive `const EspUsbHostDeviceInfo &device`. Key fields: `address`, `vid`, `pid`, `product`, `manufacturer`, `serial`, `speed`, `parentAddress`, `portId`.
Use `espUsbHostPrint(device)` for a one-line summary. Add event context such as `connected:` or `disconnected:` in your callback.

`portId` identifies where the device is attached. `0x01` means the root port. For hub-attached devices, the upper nibble is the hub index assigned in detection order and the lower nibble is the hub port number, for example `0x12` means hub #1 port 2.

### HID input

```cpp
void onKeyboard(KeyboardCallback callback);
void onKeyboardState(KeyboardStateCallback callback);
void onMouse(MouseCallback callback);
void onConsumerControl(ConsumerControlCallback callback);
void onSystemControl(SystemControlCallback callback);
void onGamepad(GamepadCallback callback);
void onHIDInput(HIDInputCallback callback);    // raw — fires for all HID interfaces
void onHIDVendorInput(HIDVendorInputCallback callback);
EspUsbHostListenerId addKeyboardListener(KeyboardCallback callback);
EspUsbHostListenerId addKeyboardStateListener(KeyboardStateCallback callback);
EspUsbHostListenerId addMouseListener(MouseCallback callback);
EspUsbHostListenerId addConsumerControlListener(ConsumerControlCallback callback);
EspUsbHostListenerId addSystemControlListener(SystemControlCallback callback);
EspUsbHostListenerId addGamepadListener(GamepadCallback callback);
bool removeListener(EspUsbHostListenerId listenerId);
void espUsbHostPrint(const EspUsbHostHIDInput &input, Print &out = Serial);
void espUsbHostPrint(const EspUsbHostKeyboardEvent &event, Print &out = Serial);
const char *espUsbHostConsumerControlUsageName(uint16_t usage);
const char *espUsbHostSystemControlUsageName(uint8_t usage);
```

Each `on*()` function keeps one callback for compatibility. The matching
`add*Listener()` functions allow adapters and the sketch to receive the same
parsed HID event without replacing one another. A successful registration
returns a nonzero `EspUsbHostListenerId`; zero
(`ESP_USB_HOST_INVALID_LISTENER_ID`) means that the callback was empty or the
event's listener capacity was reached. `removeListener()` accepts an ID from any
of the six listener types and returns whether it removed one.

There are four listeners per event by default (`EspUsbHost::MaxListenersPerEvent`),
configurable at compile time with `ESP_USB_HOST_MAX_LISTENERS_PER_EVENT`. The
single `on*()` callback runs first, followed by listeners in registration order.
The callback set is snapshotted for each event, so adding or removing a listener
inside a callback affects the next event. Registration, replacement, and removal
are protected across the sketch and USB tasks; callbacks run without holding the
registry mutex.

```cpp
EspUsbHostListenerId adapterListener =
    usb.addKeyboardListener([](const EspUsbHostKeyboardEvent &event) {
      // adapter input path
    });

usb.onKeyboard([](const EspUsbHostKeyboardEvent &event) {
  // sketch input path; both callbacks receive the event
});

// Later, from task context:
usb.removeListener(adapterListener);
```

Both 6-key boot keyboards and N-key rollover (NKRO) keyboards are supported. NKRO
keyboards report keys as a bitmap (report protocol) so any number of keys can be
held at once; the host learns the report layout from the HID report descriptor and
decodes it automatically, so `onKeyboard` delivers the same press/release events
either way. `keyboardUsesBitmapReport(address)` reports which format was detected
(diagnostic only). See the [KeyboardNKRO](examples/HID/EspUsbHostKeyboardNKRO/) example.

`onKeyboardState` provides a single format-independent snapshot whenever a keyboard
report changes. `keys` contains the current state of Keyboard/Keypad usages
`0x00-0xFF`, and `changedKeys` identifies the usages changed by that report. Use
`isDown(keycode)`, `wasPressed(keycode)`, and `wasReleased(keycode)` to query them.
Modifier keys are ordinary usages `0xE0-0xE7`, so modifier-only changes are included.
Boot, Report-ID boot, and NKRO keyboards all use this same API. Reports with no state
change do not invoke the callback.

```cpp
usb.onKeyboardState([](const EspUsbHostKeyboardState &state) {
  for (uint16_t usage = 0; usage <= 0xff; usage++) {
    uint8_t keycode = static_cast<uint8_t>(usage);
    if (state.wasPressed(keycode)) {
      // keycode includes modifiers such as Left Ctrl (0xE0)
    }
    if (state.wasReleased(keycode)) {
      // released in this report
    }
  }
});
```

Notable event fields:

Parsed HID callbacks (`onKeyboard`, `onKeyboardState`, `onMouse`, `onConsumerControl`, `onSystemControl`, `onGamepad`, `onHIDVendorInput`) all include `vid`, `pid`, `manufacturer`, `product`, `serial`, `rawData` / `rawLength` for the full input report, and `reportData` / `reportLength` for the report bytes after removing the Report ID when one is present.

| Callback | Key fields |
|----------|-----------|
| `onKeyboard` | `pressed`, `keycode`, `ascii`, `modifiers`, `address` |
| `onKeyboardState` | `keys`, `changedKeys`, `modifiers`, `isDown()`, `wasPressed()`, `wasReleased()`, `address` |
| `onMouse` | `x`, `y`, `wheel`, `buttons`, `previousButtons`, `moved`, `buttonsChanged`, `address` |
| `onConsumerControl` | `pressed`, `usage` (16-bit HID usage code), `address` |
| `onSystemControl` | `pressed`, `usage` (8-bit), `address` |
| `onGamepad` | `fields`, `fieldCount`, `rawData`, `reportData`, `vid`, `pid`, `address` |
| `onHIDInput` | `address`, `vid`, `pid`, `interfaceNumber`, `subclass`, `protocol`, `data`, `length` |

Common Consumer Control constants include `ESP_USB_HOST_CONSUMER_CONTROL_PLAY_PAUSE`, `ESP_USB_HOST_CONSUMER_CONTROL_MUTE`, `ESP_USB_HOST_CONSUMER_CONTROL_VOLUME_UP`, `ESP_USB_HOST_CONSUMER_CONTROL_VOLUME_DOWN`, `ESP_USB_HOST_CONSUMER_CONTROL_NEXT_TRACK`, and `ESP_USB_HOST_CONSUMER_CONTROL_PREVIOUS_TRACK`.

### HID output

```cpp
void setKeyboardLayout(EspUsbHostKeyboardLayout layout);
bool setKeyboardLeds(bool numLock, bool capsLock, bool scrollLock,
                     uint8_t address = ESP_USB_HOST_ANY_ADDRESS);
bool sendHIDReport(uint8_t interfaceNumber, uint8_t reportType, uint8_t reportId,
                   const uint8_t *data, size_t length,
                   uint8_t address = ESP_USB_HOST_ANY_ADDRESS);
bool sendHIDVendorOutput(const uint8_t *data, size_t length,
                      uint8_t address = ESP_USB_HOST_ANY_ADDRESS);
bool sendHIDVendorFeature(const uint8_t *data, size_t length,
                       uint8_t address = ESP_USB_HOST_ANY_ADDRESS);
```

`setKeyboardLeds()` works with boot keyboards and with keyboards that only expose a
report protocol (report-ID composite HID devices, NKRO keyboards): when no boot
interface is declared, the LED output report found in the HID report descriptor is
used and the Set_Report carries the keyboard's report ID. The host also pushes its
current lock state to the keyboard once at connect.

`sendHIDVendorOutput()` and `sendHIDVendorFeature()` are HID vendor-report helpers. For non-HID vendor-specific interfaces, use the Vendor bulk/control APIs below.

The default layout is `ESP_USB_HOST_KEYBOARD_LAYOUT_EN_US`. Pass any of the following constants to `setKeyboardLayout()`:

| Constant | Locale | Notes |
|----------|--------|-------|
| `ESP_USB_HOST_KEYBOARD_LAYOUT_ZH_TW` | zh_TW | Traditional Chinese — US QWERTY symbols |
| `ESP_USB_HOST_KEYBOARD_LAYOUT_DA_DK` | da_DK | Danish |
| `ESP_USB_HOST_KEYBOARD_LAYOUT_DE_DE` | de_DE | German QWERTZ |
| `ESP_USB_HOST_KEYBOARD_LAYOUT_EN_US` | en_US | English US (**default**) |
| `ESP_USB_HOST_KEYBOARD_LAYOUT_FI_FI` | fi_FI | Finnish |
| `ESP_USB_HOST_KEYBOARD_LAYOUT_FR_FR` | fr_FR | French AZERTY |
| `ESP_USB_HOST_KEYBOARD_LAYOUT_HU_HU` | hu_HU | Hungarian QWERTZ |
| `ESP_USB_HOST_KEYBOARD_LAYOUT_IT_IT` | it_IT | Italian |
| `ESP_USB_HOST_KEYBOARD_LAYOUT_JA_JP` | ja_JP | Japanese |
| `ESP_USB_HOST_KEYBOARD_LAYOUT_KO_KR` | ko_KR | Korean — US QWERTY symbols |
| `ESP_USB_HOST_KEYBOARD_LAYOUT_NL_NL` | nl_NL | Dutch |
| `ESP_USB_HOST_KEYBOARD_LAYOUT_NB_NO` | nb_NO | Norwegian Bokmål |
| `ESP_USB_HOST_KEYBOARD_LAYOUT_PT_BR` | pt_BR | Brazilian Portuguese ABNT2 |
| `ESP_USB_HOST_KEYBOARD_LAYOUT_SV_SE` | sv_SE | Swedish |
| `ESP_USB_HOST_KEYBOARD_LAYOUT_ZH_CN` | zh_CN | Simplified Chinese — US QWERTY symbols |
| `ESP_USB_HOST_KEYBOARD_LAYOUT_EN_GB` | en_GB | English UK |
| `ESP_USB_HOST_KEYBOARD_LAYOUT_PT_PT` | pt_PT | Portuguese (Portugal) |
| `ESP_USB_HOST_KEYBOARD_LAYOUT_ES_ES` | es_ES | Spanish |
| `ESP_USB_HOST_KEYBOARD_LAYOUT_FR_CH` | fr_CH | Swiss French QWERTZ |

`event.ascii` is a Latin-1 encoded `uint8_t` (0x00–0xFF). Dead keys (´, \`, ^, ~, ¨) and characters outside Latin-1 produce `ascii = 0`. For `ZH_TW`, `KO_KR`, and `ZH_CN`, the symbol layout is identical to `EN_US`; actual CJK character input requires an IME on the host side.

### USB serial (CDC ACM and VCP)

Low-level send API on `EspUsbHost`:

```cpp
bool sendSerial(const uint8_t *data, size_t length,
                uint8_t address = ESP_USB_HOST_ANY_ADDRESS);
bool sendSerial(const char *text,
                uint8_t address = ESP_USB_HOST_ANY_ADDRESS);
bool serialReady(uint8_t address = ESP_USB_HOST_ANY_ADDRESS) const;
bool setSerialBaudRate(uint32_t baud,
                       uint8_t address = ESP_USB_HOST_ANY_ADDRESS);
bool setSerialConfig(const EspUsbHostSerialConfig &config,
                     uint8_t address = ESP_USB_HOST_ANY_ADDRESS);
```

`EspUsbHostCdcSerial` wraps the above as a standard Arduino `Stream` / `Print`:

```cpp
EspUsbHostCdcSerial CdcSerial(usb);

bool    begin(uint32_t baud = 115200);
void    end();
bool    connected() const;
int     available();
int     read();
int     peek();
void    flush();
size_t  write(uint8_t data);
size_t  write(const uint8_t *buffer, size_t size);
bool    setBaudRate(uint32_t baud);
bool    setConfig(const EspUsbHostSerialConfig &config);
bool    setDtr(bool enable);
bool    setRts(bool enable);
void    setAddress(uint8_t address);
uint8_t address() const;
void    clearAddress();
```

`EspUsbHostSerialConfig` defaults to 115200 8N1. `dataBits` supports 5 to 8 bits. `parity` accepts `ESP_USB_HOST_SERIAL_PARITY_NONE`, `ODD`, `EVEN`, `MARK`, or `SPACE`. `stopBits` accepts `ESP_USB_HOST_SERIAL_STOP_BITS_1`, `1_5`, or `2`.

Use `setAddress()` inside `onDeviceConnected` to bind a specific device when multiple USB serial devices are connected.

### Vendor bulk/control

For non-HID vendor-specific interfaces (`bInterfaceClass == 0xff`) with bulk endpoints:

```cpp
void onVendorData(VendorDataCallback callback);

bool vendorOpen(uint8_t address = ESP_USB_HOST_ANY_ADDRESS,
                uint8_t interfaceNumber = 0xff);
bool vendorWrite(const uint8_t *data, size_t length,
                 uint8_t address = ESP_USB_HOST_ANY_ADDRESS);
size_t vendorRead(uint8_t *buffer, size_t length,
                  uint8_t address = ESP_USB_HOST_ANY_ADDRESS);
uint16_t vendorOutPacketSize(uint8_t address = ESP_USB_HOST_ANY_ADDRESS) const;
uint16_t vendorInPacketSize(uint8_t address = ESP_USB_HOST_ANY_ADDRESS) const;
uint8_t vendorOutEndpoint(uint8_t address = ESP_USB_HOST_ANY_ADDRESS) const;
uint8_t vendorInEndpoint(uint8_t address = ESP_USB_HOST_ANY_ADDRESS) const;

bool vendorControlIn(uint8_t request, uint16_t value, uint16_t index,
                     uint8_t *data, size_t length,
                     size_t *actualLength = nullptr,
                     uint8_t address = ESP_USB_HOST_ANY_ADDRESS,
                     uint32_t timeoutMs = ESP_USB_HOST_VENDOR_CONTROL_DEFAULT_TIMEOUT_MS);
bool vendorControlOut(uint8_t request, uint16_t value, uint16_t index,
                      const uint8_t *data = nullptr, size_t length = 0,
                      uint8_t address = ESP_USB_HOST_ANY_ADDRESS,
                      uint32_t timeoutMs = ESP_USB_HOST_VENDOR_CONTROL_DEFAULT_TIMEOUT_MS);
```

`vendorOpen()` explicitly claims the vendor-specific interface and starts bulk IN reception. An interface with both a bulk IN and a bulk OUT endpoint is preferred; an interface that only exposes a bulk OUT endpoint is also accepted, in which case no IN transfer is started and `vendorRead()` / `onVendorData()` never produce data. USB graphics adapters, for example, pair their bulk OUT with an interrupt IN that this API does not use.

When an interface exposes several bulk endpoints in the same direction, the first one in descriptor order is selected. `vendorOutPacketSize()` / `vendorInPacketSize()` return the max packet size of the opened endpoints and `vendorOutEndpoint()` / `vendorInEndpoint()` their addresses (0 when not open). Callers need the packet size when a transfer must be terminated on a packet boundary, and the address to confirm which endpoint was chosen.

`vendorWrite()` waits for transfer completion and therefore cannot be called from USB callbacks such as `onDeviceConnected()` or `onVendorData()`; record the send request in the callback and perform it from `loop()`. `vendorRead()` is non-blocking and reads from a 512-byte per-device receive buffer. `onVendorData()` receives the same bulk IN payload as a callback; its data pointer is valid only during the callback.

`vendorControlIn()` uses `bmRequestType = 0xc0`; `vendorControlOut()` uses `bmRequestType = 0x40`.

The asynchronous bulk OUT queue keeps several transfers in flight, so the bus does not go idle between them:

```cpp
bool vendorWriteQueueBegin(size_t depth, size_t bufferBytes,
                           uint8_t address = ESP_USB_HOST_ANY_ADDRESS);
void vendorWriteQueueEnd(uint8_t address = ESP_USB_HOST_ANY_ADDRESS);
bool vendorWriteQueueReady(uint8_t address = ESP_USB_HOST_ANY_ADDRESS) const;

uint8_t *vendorWriteAcquire(size_t *capacity, uint32_t timeoutMs = 0,
                            uint8_t address = ESP_USB_HOST_ANY_ADDRESS);
bool vendorWriteSubmit(uint8_t *buffer, size_t length,
                       uint8_t address = ESP_USB_HOST_ANY_ADDRESS);
void vendorWriteRelease(uint8_t *buffer, uint8_t address = ESP_USB_HOST_ANY_ADDRESS);
bool vendorWriteAsync(const uint8_t *data, size_t length,
                      uint8_t address = ESP_USB_HOST_ANY_ADDRESS);

size_t vendorWritePending(uint8_t address = ESP_USB_HOST_ANY_ADDRESS) const;
size_t vendorWriteQueueFree(uint8_t address = ESP_USB_HOST_ANY_ADDRESS) const;
bool vendorWriteFlush(uint32_t timeoutMs, uint8_t address = ESP_USB_HOST_ANY_ADDRESS);
EspUsbHostVendorWriteStats vendorWriteStats(uint8_t address = ESP_USB_HOST_ANY_ADDRESS) const;
void vendorWriteStatsReset(uint8_t address = ESP_USB_HOST_ANY_ADDRESS);

bool vendorWriteZlp(uint8_t address = ESP_USB_HOST_ANY_ADDRESS);
void vendorSetAutoZlp(bool enable, uint8_t address = ESP_USB_HOST_ANY_ADDRESS);
bool vendorAutoZlp(uint8_t address = ESP_USB_HOST_ANY_ADDRESS) const;
```

`vendorWriteQueueBegin()` preallocates `depth` reusable transfers of `bufferBytes` each (max depth `ESP_USB_HOST_VENDOR_WRITE_QUEUE_MAX_DEPTH`). The preferred sequence is zero-copy: `vendorWriteAcquire()` lends you a pooled DMA buffer, you write the payload into it, and `vendorWriteSubmit()` sends it. `vendorWriteAsync()` is the copying convenience form and fails if `length` exceeds the slot size rather than splitting. `vendorWriteRelease()` returns a slot you acquired but decided not to send.

None of these wait for completion, so unlike `vendorWrite()` they may be called from USB callbacks. Completion state is observed through `vendorWritePending()` / `vendorWriteFlush()` / `vendorWriteStats()`. `vendorWriteFlush()` cannot be called from the USB client task, because that is where completion callbacks run.

Measured on an ESP32-S3 (full-speed OTG) with `tests/manual/vendor_bulk_throughput`: the queue reaches 1.098 MB/s, about 90% of the 1.216 MB/s full-speed bulk ceiling, and a depth of 2 is enough to stay there at any transfer size. Synchronous `vendorWrite()` reaches the same figure only with large transfers and drops to 0.88 MB/s at 512 bytes, where per-transfer latency dominates. Depths beyond 2 did not help on full speed.

A bulk OUT transfer whose length is a multiple of the endpoint max packet size does not terminate the USB transfer by itself. `vendorSetAutoZlp(true)` makes the library append the required zero-length packet; `vendorWriteZlp()` sends one explicitly. Auto ZLP is off by default and consumes a second queue slot, so use a depth of at least 2 with it.

### MIDI

```cpp
void onMidiMessage(MidiCallback callback);   // receive

bool midiReady(uint8_t address = ESP_USB_HOST_ANY_ADDRESS) const;
bool midiSend(const uint8_t *data, size_t length,
              uint8_t address = ESP_USB_HOST_ANY_ADDRESS);
bool midiSendNoteOn(uint8_t channel, uint8_t note, uint8_t velocity,
                    uint8_t address = ESP_USB_HOST_ANY_ADDRESS);
bool midiSendNoteOff(uint8_t channel, uint8_t note, uint8_t velocity,
                     uint8_t address = ESP_USB_HOST_ANY_ADDRESS);
bool midiSendControlChange(uint8_t channel, uint8_t control, uint8_t value,
                           uint8_t address = ESP_USB_HOST_ANY_ADDRESS);
bool midiSendProgramChange(uint8_t channel, uint8_t program,
                           uint8_t address = ESP_USB_HOST_ANY_ADDRESS);
bool midiSendPolyPressure(uint8_t channel, uint8_t note, uint8_t pressure,
                          uint8_t address = ESP_USB_HOST_ANY_ADDRESS);
bool midiSendChannelPressure(uint8_t channel, uint8_t pressure,
                             uint8_t address = ESP_USB_HOST_ANY_ADDRESS);
bool midiSendPitchBend(uint8_t channel, uint16_t value,
                       uint8_t address = ESP_USB_HOST_ANY_ADDRESS);
bool midiSendPitchBendSigned(uint8_t channel, int16_t value,
                             uint8_t address = ESP_USB_HOST_ANY_ADDRESS);
bool midiSendSysEx(const uint8_t *data, size_t length,
                   uint8_t address = ESP_USB_HOST_ANY_ADDRESS);
```

`onMidiMessage` callback receives `const EspUsbHostMidiMessage &message` with fields `cable`, `codeIndex`, `status`, `data1`, `data2`.

### USB audio

```cpp
void onAudioData(AudioDataCallback callback);
void onAudioOutputRequest(AudioOutputCallback callback);
bool audioInputReady(uint8_t address = ESP_USB_HOST_ANY_ADDRESS) const;
bool audioInputStart(uint8_t channels,
                     uint8_t bitsPerSample,
                     uint32_t sampleRate,
                     uint8_t address = ESP_USB_HOST_ANY_ADDRESS);
bool audioInputStart(const EspUsbHostAudioStreamInfo &stream,
                     uint32_t sampleRate = 0,
                     uint8_t address = ESP_USB_HOST_ANY_ADDRESS);
bool audioOutputReady(uint8_t address = ESP_USB_HOST_ANY_ADDRESS) const;
bool setAudioSampleRate(uint32_t sampleRate,
                        uint8_t address = ESP_USB_HOST_ANY_ADDRESS);
bool audioOutputStart(uint8_t channels,
                      uint8_t bitsPerSample,
                      uint32_t sampleRate,
                      uint8_t address = ESP_USB_HOST_ANY_ADDRESS);
bool audioOutputStart(const EspUsbHostAudioStreamInfo &stream,
                      uint32_t sampleRate = 0,
                      uint8_t address = ESP_USB_HOST_ANY_ADDRESS);
void audioOutputStop(uint8_t address = ESP_USB_HOST_ANY_ADDRESS);
bool audioOutputRunning(uint8_t address = ESP_USB_HOST_ANY_ADDRESS) const;
uint32_t audioOutputUnderruns(uint8_t address = ESP_USB_HOST_ANY_ADDRESS) const;
bool audioSend(const uint8_t *data, size_t length,
               uint8_t address = ESP_USB_HOST_ANY_ADDRESS);
size_t getAudioStreams(uint8_t address, EspUsbHostAudioStreamInfo *streams,
                       size_t maxStreams) const;
size_t getAudioFeatureUnits(uint8_t address,
                            EspUsbHostAudioFeatureUnitInfo *units,
                            size_t maxUnits) const;
bool audioHasMute(uint8_t address = ESP_USB_HOST_ANY_ADDRESS,
                  uint8_t unitId = 0,
                  uint8_t channel = 0) const;
bool audioHasVolume(uint8_t address = ESP_USB_HOST_ANY_ADDRESS,
                    uint8_t unitId = 0,
                    uint8_t channel = 0) const;
bool audioGetMute(bool &mute, uint8_t address = ESP_USB_HOST_ANY_ADDRESS,
                  uint8_t unitId = 0, uint8_t channel = 0);
bool audioSetMute(bool mute, uint8_t address = ESP_USB_HOST_ANY_ADDRESS,
                  uint8_t unitId = 0, uint8_t channel = 0);
bool audioGetVolume(int16_t &volume, uint8_t address = ESP_USB_HOST_ANY_ADDRESS,
                    uint8_t unitId = 0, uint8_t channel = 0);
bool audioSetVolume(int16_t volume, uint8_t address = ESP_USB_HOST_ANY_ADDRESS,
                    uint8_t unitId = 0, uint8_t channel = 0);
bool audioGetVolumeRange(EspUsbHostAudioVolumeRange &range,
                         uint8_t address = ESP_USB_HOST_ANY_ADDRESS,
                         uint8_t unitId = 0,
                         uint8_t channel = 0);
bool audioGetVolumeDb(float &db, uint8_t address = ESP_USB_HOST_ANY_ADDRESS,
                      uint8_t unitId = 0, uint8_t channel = 0);
bool audioSetVolumeDb(float db, uint8_t address = ESP_USB_HOST_ANY_ADDRESS,
                      uint8_t unitId = 0, uint8_t channel = 0);
bool audioSetVolumeDbClamped(float db, uint8_t address = ESP_USB_HOST_ANY_ADDRESS,
                             uint8_t unitId = 0, uint8_t channel = 0);
bool audioConfigureVolume(float db, bool mute = false,
                          uint8_t address = ESP_USB_HOST_ANY_ADDRESS,
                          uint8_t unitId = 0, uint8_t channel = 0);
bool audioSetVolumePercent(uint8_t percent,
                           uint8_t address = ESP_USB_HOST_ANY_ADDRESS,
                           uint8_t unitId = 0, uint8_t channel = 0);
bool audioConfigureVolumePercent(uint8_t percent,
                                 uint8_t address = ESP_USB_HOST_ANY_ADDRESS,
                                 uint8_t unitId = 0, uint8_t channel = 0);
void espUsbHostPrint(const EspUsbHostAudioStreamInfo &stream,
                     Print &out = Serial);
bool espUsbHostAudioStreamSupportsSampleRate(const EspUsbHostAudioStreamInfo &stream,
                                             uint32_t sampleRate);
uint32_t espUsbHostAudioStreamPreferredSampleRate(const EspUsbHostAudioStreamInfo &stream,
                                                  uint32_t preferredSampleRate);
bool espUsbHostAudioStreamMatchesPcm(const EspUsbHostAudioStreamInfo &stream,
                                     uint8_t channels,
                                     uint8_t bytesPerSample,
                                     uint8_t bitsPerSample,
                                     uint32_t sampleRate);
using EspUsbHostAudioStreamFilter = bool (*)(uint32_t sampleRate,
                                             uint8_t channels,
                                             uint8_t bitsPerSample);
EspUsbHostAudioStreamSelection espUsbHostSelectAudioInputStream(
    const EspUsbHostAudioStreamInfo *streams,
    size_t count,
    EspUsbHostAudioStreamFilter filter = nullptr);
EspUsbHostAudioStreamSelection espUsbHostSelectAudioOutputStream(
    const EspUsbHostAudioStreamInfo *streams,
    size_t count,
    EspUsbHostAudioStreamFilter filter = nullptr);
```

`onAudioData` receives raw payload bytes from USB Audio streaming isochronous IN endpoints. The callback receives `const EspUsbHostAudioData &audio` with fields `address`, `interfaceNumber`, `data`, and `length`.

`onAudioOutputRequest` is the preferred USB Audio OUT API. After `audioOutputStart()`, the library drives isochronous OUT transfers and calls the callback when it needs the next PCM frames. Fill `request.data` with up to `request.frameCount` interleaved frames and set `request.writtenFrames`; any unwritten frames are sent as silence and counted as underruns. The callback runs on the USB client task, so keep it short and non-blocking; copy from an existing PCM buffer rather than doing heavy decoding in the callback.

`getAudioStreams` reports the streaming endpoint direction, endpoint packet size, and UAC1 Type I format fields when available, including discrete sample rates or a continuous sample-rate range. `espUsbHostSelectAudioInputStream` and `espUsbHostSelectAudioOutputStream` apply an optional `(sampleRate, channels, bitsPerSample)` filter, then score the remaining candidates. The default scoring prefers 48 kHz, then 44.1 kHz, 16-bit PCM, and stereo when available. `setAudioSampleRate` sets the UAC1 sampling frequency request used when activating audio streaming endpoints. `audioSend` remains as a low-level API for manually submitting raw PCM payload bytes to a USB Audio streaming isochronous OUT endpoint.

`getAudioFeatureUnits` reports parsed UAC1 Audio Control Feature Units. `audioGetMute`, `audioSetMute`, `audioGetVolume`, `audioSetVolume`, and the dB/range helpers use UAC1 class-specific Feature Unit requests. `audioSetVolumeDbClamped` applies the device min/max/resolution when the range is available. `audioConfigureVolume` is the simple playback helper: it unmutes/mutes when mute is supported and sets clamped dB volume when volume is supported. The percent helpers treat `1..100` as a PCM amplitude ratio (`20 * log10(percent / 100)`) and round to the device step after clamping to min/max; `0` mutes when mute is supported, or falls back to minimum volume. `unitId=0` selects the first Feature Unit that exposes the requested control. `channel=0` means master; channel values starting at 1 address per-channel controls. Raw volume values are signed 1/256 dB units.

#### Audio scope

The audio support targets **UAC1 (Audio Class 1.0), Type I PCM** streaming:

- **Supported:** isochronous IN/OUT streaming, UAC1 Type I format parsing and sample-rate selection, and the **Feature Unit** Mute / Volume controls (get/set, range, dB and percent helpers).
- **Not supported:** UAC2 devices and their **Clock Source / Clock Selector** entities; other Audio Control units — **Mixer / Selector / Processing Unit** — and Feature Unit controls beyond Mute/Volume (Bass, Mid, Treble, Automatic Gain, Delay, etc.). Devices that require these to start streaming, or that only expose UAC2 descriptors, may enumerate but not stream.

Audio OUT and IN are peer-verified with the standard Arduino `USBAudioCard`; validation against real USB microphones and audio interfaces is still limited.

### USB Mass Storage

```cpp
bool mscReady(uint8_t address = ESP_USB_HOST_ANY_ADDRESS) const;
bool mscInquiry(EspUsbHostMscInquiry &inquiry,
                uint8_t address = ESP_USB_HOST_ANY_ADDRESS,
                uint32_t timeoutMs = ESP_USB_HOST_MSC_DEFAULT_TIMEOUT_MS);
bool mscRequestSense(EspUsbHostMscSense &sense,
                     uint8_t address = ESP_USB_HOST_ANY_ADDRESS,
                     uint32_t timeoutMs = ESP_USB_HOST_MSC_DEFAULT_TIMEOUT_MS);
bool mscLastSense(EspUsbHostMscSense &sense,
                  uint8_t address = ESP_USB_HOST_ANY_ADDRESS);
bool mscMaxLun(uint8_t &maxLun,
               uint8_t address = ESP_USB_HOST_ANY_ADDRESS,
               uint32_t timeoutMs = ESP_USB_HOST_MSC_DEFAULT_TIMEOUT_MS);
bool mscSelectLun(uint8_t lun,
                  uint8_t address = ESP_USB_HOST_ANY_ADDRESS,
                  uint32_t timeoutMs = ESP_USB_HOST_MSC_DEFAULT_TIMEOUT_MS);
bool mscGetBlockDeviceInfo(EspUsbHostMscBlockDeviceInfo &info,
                           uint8_t address = ESP_USB_HOST_ANY_ADDRESS,
                           uint32_t timeoutMs = ESP_USB_HOST_MSC_DEFAULT_TIMEOUT_MS);
bool mscTestUnitReady(uint8_t address = ESP_USB_HOST_ANY_ADDRESS,
                      uint32_t timeoutMs = ESP_USB_HOST_MSC_DEFAULT_TIMEOUT_MS);
bool mscWaitReady(uint8_t address = ESP_USB_HOST_ANY_ADDRESS,
                  uint32_t readyTimeoutMs = ESP_USB_HOST_MSC_DEFAULT_TIMEOUT_MS,
                  uint32_t commandTimeoutMs = ESP_USB_HOST_MSC_DEFAULT_TIMEOUT_MS);
bool mscCapacity64(uint64_t &blockCount, uint32_t &blockSize,
                   uint8_t address = ESP_USB_HOST_ANY_ADDRESS,
                   uint32_t timeoutMs = ESP_USB_HOST_MSC_DEFAULT_TIMEOUT_MS);
bool mscCapacity(uint32_t &blockCount, uint32_t &blockSize,
                 uint8_t address = ESP_USB_HOST_ANY_ADDRESS,
                 uint32_t timeoutMs = ESP_USB_HOST_MSC_DEFAULT_TIMEOUT_MS);
bool mscReadBlocks(uint32_t lba, uint8_t *data, uint32_t blockCount,
                   uint8_t address = ESP_USB_HOST_ANY_ADDRESS,
                   uint32_t timeoutMs = ESP_USB_HOST_MSC_DEFAULT_TIMEOUT_MS);
bool mscWriteBlocks(uint32_t lba, const uint8_t *data, uint32_t blockCount,
                    uint8_t address = ESP_USB_HOST_ANY_ADDRESS,
                    uint32_t timeoutMs = ESP_USB_HOST_MSC_DEFAULT_TIMEOUT_MS);
bool mscReadBlocks64(uint64_t lba, uint8_t *data, uint32_t blockCount,
                     uint8_t address = ESP_USB_HOST_ANY_ADDRESS,
                     uint32_t timeoutMs = ESP_USB_HOST_MSC_DEFAULT_TIMEOUT_MS);
bool mscWriteBlocks64(uint64_t lba, const uint8_t *data, uint32_t blockCount,
                      uint8_t address = ESP_USB_HOST_ANY_ADDRESS,
                      uint32_t timeoutMs = ESP_USB_HOST_MSC_DEFAULT_TIMEOUT_MS);
bool mscSynchronizeCache(uint8_t address = ESP_USB_HOST_ANY_ADDRESS,
                         uint32_t timeoutMs = ESP_USB_HOST_MSC_DEFAULT_TIMEOUT_MS);
bool mscMount(const char *basePath = "/usb",
              uint8_t address = ESP_USB_HOST_ANY_ADDRESS,
              uint8_t lun = 0,
              uint8_t maxFiles = 4,
              uint32_t timeoutMs = ESP_USB_HOST_MSC_DEFAULT_TIMEOUT_MS,
              bool skipSyncCache = false);
bool mscUnmount(const char *basePath = "/usb");
bool mscMounted(const char *basePath = "/usb") const;

class EspUsbHostMscFS : public fs::FS {
public:
  bool begin(EspUsbHost &host,
             const char *basePath = "/usb",
             uint8_t address = ESP_USB_HOST_ANY_ADDRESS,
             uint8_t lun = 0,
             uint8_t maxFiles = 4,
             uint32_t timeoutMs = ESP_USB_HOST_MSC_DEFAULT_TIMEOUT_MS,
             bool skipSyncCache = false);
  void end();
  bool mounted() const;
  const char *basePath() const;
  void setSkipSyncCache(bool skip);
  bool skipSyncCache() const;
};
```

MSC support covers SCSI transparent / Bulk-Only Transport block I/O, ESP-IDF FatFs/VFS mounting, and an Arduino `fs::FS` / `File` compatible wrapper.

For normal file access, use `EspUsbHostMscFS`. It derives from `fs::FS`, so it can be passed to Arduino libraries such as WebServer or Update that accept `fs::FS &`. `EspUsbHostMscFS::begin()` returns `false` while no MSC device is connected or the media is not ready, so retry it at a low rate from `loop()`. Use `mounted()` to check whether the filesystem is already mounted.

The low-level APIs such as `mscReadBlocks64()`, `mscWriteBlocks64()`, `mscInquiry()`, and `mscRequestSense()` are intended for capacity reporting, block dumps, diagnostics, and custom filesystem implementations. For normal FAT file access, sketches do not need to call `mscReady()`, `mscWaitReady()`, or `mscGetBlockDeviceInfo()` directly.

The block APIs support 64-bit LBA, but the current FatFs/VFS mount path is limited by the ESP-IDF FatFs build to 32-bit sectors. Multiple MSC devices and multiple LUNs can be addressed by API parameters, but ESP32-S3 has tight HCD channel limits, so assume a single MSC device for practical use. Multiple MSC devices remain an ESP32-P4-oriented validation item.

Do not call these MSC APIs from USB callbacks, because they wait for USB transfer completion. Removing a USB drive while files are open or writes are in progress may lose unwritten data. After reconnecting media, call `begin()` again.

Some non-compliant MSC devices stall or disconnect when they receive SCSI `SYNCHRONIZE CACHE(10)` during FatFs sync. Whenever `SYNCHRONIZE CACHE(10)` fails — through FatFs `CTRL_SYNC` or a direct `mscSynchronizeCache()` call — the library clears the resulting bulk-pipe halt, remembers the failure for that device, and skips the command for the rest of that mount and for later `mscMount()` calls until the device is reconnected. For known-problem devices, call `usbMassStorage.setSkipSyncCache(true)` before `begin()`, or pass `skipSyncCache = true` to `begin()` / `mscMount()` to skip it from the start. This improves compatibility but relies on normal write completion instead of an explicit media flush.

### USB Hub

```cpp
bool getHubInfo(uint8_t hubAddress, EspUsbHostHubInfo &hub);
bool getHubPortStatus(uint8_t hubAddress, uint8_t port,
                      uint16_t &status, uint16_t &change);
bool setHubPortPower(uint8_t hubAddress, uint8_t port, bool enable);
```

USB Hub support covers detection, simple topology reporting, hub descriptor queries, port status queries, and port power on/off for PPPS-capable hubs. Use `EspUsbHostDeviceInfo::isHub` to identify hub devices. For devices behind a hub, `parentAddress` and `portId` report the hub/port path used for display.

`getHubInfo()` fetches the hub descriptor and fills `EspUsbHostHubInfo` with port count, PPPS/ganged/no power switching, over-current mode, power-on-to-power-good time, and related fields. `getHubPortStatus()` returns current status and change bits for a downstream port. `setHubPortPower()` sends the hub class request to enable or disable port power.

Per-port power control is only safe when the hub reports PPPS (Per-Port Power Switching). On ganged-power hubs, a port power request may affect multiple ports or the whole hub. USB 3.x hubs and products implemented internally as cascaded hubs can be more complex, so a self-powered USB 2.0 hub is recommended for validation.

This is not a complete hub class driver. It exposes user-facing information and explicit port power control. Clearing port change bits, cascaded hub behavior, USB 3.x hub compatibility, and ESP32-P4 FS/HS differences remain validation items. Do not call these APIs from USB callbacks, because they wait for USB transfer completion.

### USB network (CDC-NCM / CDC-ECM)

```cpp
size_t getNetworkInterfaces(uint8_t address,
                            EspUsbHostNetworkInterfaceInfo *interfaces,
                            size_t maxInterfaces);
bool networkOpen(uint8_t address = ESP_USB_HOST_ANY_ADDRESS);
bool networkOpen(const EspUsbHostNetworkInterfaceInfo &network);
void networkClose(uint8_t address = ESP_USB_HOST_ANY_ADDRESS);
bool networkReady(uint8_t address = ESP_USB_HOST_ANY_ADDRESS) const;
bool networkLinkUp(uint8_t address = ESP_USB_HOST_ANY_ADDRESS) const;

// Raw Ethernet frames
void onNetworkFrame(NetworkFrameCallback callback);
bool   networkWriteFrame(const uint8_t *frame, size_t length, uint8_t address = ESP_USB_HOST_ANY_ADDRESS);
size_t networkReadFrame(uint8_t *buffer, size_t length, uint8_t address = ESP_USB_HOST_ANY_ADDRESS);

// lwIP (esp_netif) integration
bool      networkAttachNetif(const EspUsbHostNetworkConfig &config = EspUsbHostNetworkConfig(),
                             uint8_t address = ESP_USB_HOST_ANY_ADDRESS);
bool      networkDetachNetif(uint8_t address = ESP_USB_HOST_ANY_ADDRESS);
IPAddress networkLocalIP(uint8_t address = ESP_USB_HOST_ANY_ADDRESS) const;
bool      networkStats(EspUsbHostNetworkStats &stats, uint8_t address = ESP_USB_HOST_ANY_ADDRESS) const;
```

`networkAttachNetif()` opens the CDC-NCM/ECM interface (if not already open) and
registers it as an `esp_netif` interface, so standard Arduino networking
(`NetworkClient`, `HTTPClient`) runs over the USB NIC. `EspUsbHostNetworkConfig`
defaults to a DHCP client; set `dhcpClient=false` and fill
`ip`/`gateway`/`subnet`(`/dns1`/`dns2`) for a static address. For raw Ethernet
frames instead of an IP stack, use `onNetworkFrame()` / `networkWriteFrame()` /
`networkReadFrame()` and do not attach a netif. CDC-NCM is preferred over
CDC-ECM when a device offers both.

`getNetworkInterfaces()` walks *every* configuration (`usb_host_get_config_desc()`)
and reports each candidate with its `configurationValue`, while `networkOpen()`
only accepts a candidate in the **active** configuration. An adapter whose
network function is not in its default configuration therefore needs
`setConfigurationSelector()` and two enumeration passes — see
[examples/UsbNetwork/](examples/UsbNetwork/) for the details and for how to
automate the second pass.

lwIP integration requires `esp_netif` in the build (it is present in the standard
Arduino-ESP32 core). Without it, `networkAttachNetif()` returns `false` and the
raw frame API can still be used. Call these APIs from the application task, not
from a USB callback.

### Device discovery

```cpp
size_t deviceCount() const;
size_t getDevices(EspUsbHostDeviceInfo *devices, size_t maxDevices) const;
bool   getDevice(uint8_t address, EspUsbHostDeviceInfo &device) const;
size_t getInterfaces(uint8_t address, EspUsbHostInterfaceInfo *interfaces,
                     size_t maxInterfaces) const;
size_t getEndpoints(uint8_t address, EspUsbHostEndpointInfo *endpoints,
                    size_t maxEndpoints) const;
size_t endpointChannelCount(uint8_t address = ESP_USB_HOST_ANY_ADDRESS) const;
size_t managedEndpointCount(uint8_t address = ESP_USB_HOST_ANY_ADDRESS) const;
size_t ep0ChannelCount(uint8_t address = ESP_USB_HOST_ANY_ADDRESS) const;
size_t hubEndpointChannelCount(uint8_t address = ESP_USB_HOST_ANY_ADDRESS) const;
size_t estimatedHcdChannelCount(uint8_t address = ESP_USB_HOST_ANY_ADDRESS) const;
size_t maxEndpointChannelCount() const;
void   espUsbHostPrint(const EspUsbHostInterfaceInfo &interface,
                       Print &out = Serial);
void   espUsbHostPrint(const EspUsbHostEndpointInfo &endpoint,
                       Print &out = Serial);
void   printDeviceInfo(uint8_t address, bool includeHubInfo = false,
                       Print &out = Serial);
void   printAllDeviceInfo(Print &out = Serial);
```

Array size constants: `ESP_USB_HOST_MAX_DEVICES`, `ESP_USB_HOST_MAX_INTERFACES`, `ESP_USB_HOST_MAX_ENDPOINTS`.
`endpointChannelCount()` is based on endpoints in successfully claimed interfaces. `managedEndpointCount()` counts endpoints with persistent receive transfers managed by this library. `estimatedHcdChannelCount()` is an experimental estimate: tracked devices as EP0/control pipes + claimed endpoints + hub descriptor endpoints.

### Error handling

```cpp
int         lastError() const;
const char *lastErrorName() const;
```

## Design

**Callbacks over inheritance.** Register lambdas or functions with `onKeyboard()`, `onMouse()`, etc. The old pattern of subclassing `EspUsbHost` and overriding virtual methods is not the primary API.

**Breaking changes are accepted in 2.x.** The library prioritises a clean Arduino-oriented API over backwards compatibility with its earlier inheritance-based interface. Even within the 2.x series, APIs may change when examples or real devices show a better shape.

**HID gamepad reports are exposed as mappable data.** `onGamepad()` reports descriptor-decoded fields plus raw/report bytes. It does not assign semantic names such as left stick X/Y or right stick X/Y because those fields vary by device and may be 8-bit, 12-bit, 16-bit, or packed. Use `vid` / `pid`, `fields`, `rawData`, and `reportData` to build the mapping that matches your controller.

**Non-goals:**
- Fully automatic interpretation of all HID report descriptors from the first implementation
- Implementing all USB classes in the first release
- ESP-IDF HID Host Driver API compatibility
- Exposing all USB spec internals directly as Arduino APIs

## Multiple devices

For multi-device use, a self-powered USB 2.0 hub is recommended. Cheap bus-powered hubs may fail to enumerate devices or become unstable when devices draw current. Many USB 3.x hubs also expose more complex internal topology or behavior that is not a good starting point for this library.

Prefer hubs with up to 4 downstream ports. Hubs with many ports are often implemented internally as cascaded hubs, which increases topology depth and resource use. ESP32-S3 has only 8 USB host channels, and some USB devices consume multiple channels for their interfaces/endpoints, so multi-device setups can hit the channel limit quickly.

All send APIs and `EspUsbHostCdcSerial` default to `ESP_USB_HOST_ANY_ADDRESS`, which targets the first available device of the appropriate class. Pass an explicit `address` to target the currently connected device.

Device identification fields have different stability:

| Field | Use |
|-------|-----|
| `address` | Current USB address. Use it for API calls such as `setAddress()`, `sendSerial()`, `midiSend()`, or `setKeyboardLeds()`. It can change after unplug/replug. |
| `portId` | Physical/topology location. Use it to remember "the device on this port" across reconnects. |
| `vid` / `pid` | Device type/model identification. Use it to select supported chip families or products. |
| `manufacturer` / `product` | Human-readable USB string descriptors. Useful for logs and UI, but not guaranteed unique. |
| `serial` | USB serial-number string descriptor when the device provides one. It may be empty. Use it for per-unit identity when available. |

```cpp
usb.onDeviceConnected([](const EspUsbHostDeviceInfo &device) {
  if (device.vid == 0x0403) {
    CdcSerial.setAddress(device.address);
  }
});
```

## Tests

- [`tests/peer/`](tests/peer/) — two-board USB tests using an ESP32-S3 as the device peer. The peer side mainly uses the Arduino-ESP32 standard USB Device implementation to check basic Host interoperability
- [`tests/loopback/`](tests/loopback/) — single-board loopback tests. Practical ESP32-P4 loopback coverage is currently being developed in the sibling `EspUsbDevice` library
- [`tests/manual/`](tests/manual/) — manual tests for special hardware and human verification

See [tests/README.md](tests/README.md) for setup instructions.

The manually triggered _Library Footprint Matrix_ workflow uses the Arduino core
pinned by a representative example's `sketch.yaml` and builds fixed Base, HID,
Serial, Audio, Storage, MIDI, Vendor, Network, and Info probes against each
selected library release. It overwrites one canonical normalized Flash/static-RAM
JSON and Markdown report, while retaining compiler logs, ELF, map, and application
bin files as short-lived workflow artifacts. This is separate from the core
compatibility matrix, which varies the Arduino core to test build compatibility
rather than library-version size trends.

## Release checklist

1. **Clean working tree** — confirm `git status` shows no uncommitted changes
2. **Update dependencies** — use the [vscode-arduino-cli-wrapper](https://marketplace.visualstudio.com/items?itemName=tanakamasayuki.vscode-arduino-cli-wrapper) _sketch.yaml Versions_ feature to check all `sketch.yaml` files for outdated board/library versions; update to the latest and re-run steps 3–5 if anything changed
3. **Build check** — use _Build Check_ in vscode-arduino-cli-wrapper, or run `python tools/build_check.py <profile>` to compile every example that declares that sketch.yaml profile (e.g. `python tools/build_check.py esp32s2`). Minimum: `examples/` with the `esp32s3` profile; add all profiles if the change touches ESP32-P4 support. Also build the `esp32s2` profile when a change grows static RAM use (the S2 has far less internal RAM; `ESP_USB_HOST_MAX_DEVICES` defaults lower there), so `dram0_0_seg overflowed` regressions are caught early. The `UsbNetwork` example is intentionally excluded from the S2 matrix.
4. **Automated tests** — all `peer/` or `loopback/` tests pass
5. **Manual tests** — run tests related to the change (check `tests/.pytest-results/state.json` for last-run timestamps); not mandatory but strongly recommended
6. **CHANGELOG** — verify the entry for this release is accurate and complete
7. **Documentation** — confirm that API reference, examples, and README reflect the change
8. **Release** — trigger the GitHub Actions release workflow (`workflow_dispatch`, default: patch bump). The workflow will:
   - Bump the version in `library.properties` (major / minor / patch selectable)
   - Commit and push the version bump to the default branch
   - Create a `release` branch with `tests/` removed
   - Build a `.zip` archive of the library
   - Extract release notes from `CHANGELOG.md`
   - Create a GitHub release with the archive and release notes

## License

MIT License. See [LICENSE](LICENSE).
