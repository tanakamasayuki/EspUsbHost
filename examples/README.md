# EspUsbHost example guide

[日本語](README.ja.md)

Start with [Working through the examples](../docs/example-workflow.md) for installation, wiring, callbacks and return values. Each sketch has a detailed guide.

1. Use [BringUpCheck](Info/EspUsbHostBringUpCheck/README.md) to separate host startup from peripheral enumeration.
2. Read VID/PID, interfaces and endpoints with [DeviceInfo](Info/EspUsbHostDeviceInfo/README.md).
3. Run one class example for a device you own, such as [Keyboard](HID/EspUsbHostKeyboard/README.md).
4. For unfamiliar HID, compare [RawDump](HID/EspUsbHostHIDRawDump/README.md) with [ReportDescriptor](Info/EspUsbHostHIDReportDescriptor/README.md).
5. Add hubs, multiple devices, audio and custom protocols after the single-device case works.

| Example | What to observe |
| --- | --- |
| [EspUsbHostAudioInput](Audio/EspUsbHostAudioInput/README.md) | Receive USB microphone PCM and inspect signal peaks. |
| [EspUsbHostAudioOutputHardwareVolume](Audio/EspUsbHostAudioOutputHardwareVolume/README.md) | Distinguish generated PCM level from USB hardware volume controls. |
| [EspUsbHostAudioOutputMP3ESP8266Audio](Audio/EspUsbHostAudioOutputMP3ESP8266Audio/README.md) | Decode embedded MP3 assets and feed a USB audio output. |
| [EspUsbHostAudioOutputMP3PCMFlow](Audio/EspUsbHostAudioOutputMP3PCMFlow/README.md) | Connect PCMFlow decoding to a selected USB audio stream. |
| [EspUsbHostAudioOutputTone](Audio/EspUsbHostAudioOutputTone/README.md) | Generate PCM tones for a compatible USB audio output. |
| [EspUsbHostCcidFelicaIdm](Ccid/EspUsbHostCcidFelicaIdm/README.md) | Read a FeliCa IDm using a selected System Code. |
| [EspUsbHostCcidReader](Ccid/EspUsbHostCcidReader/README.md) | Activate a smart card and inspect its ATR and UID response. |
| [EspUsbHostCompositeHID](HID/EspUsbHostCompositeHID/README.md) | Read keyboard, mouse and media functions from a composite HID device. |
| [EspUsbHostConsumerControl](HID/EspUsbHostConsumerControl/README.md) | Receive media-key press and release events. |
| [EspUsbHostDp100Power](HID/EspUsbHostDp100Power/README.md) | Read and optionally configure a DP100 power supply over HID. |
| [EspUsbHostGamepad](HID/EspUsbHostGamepad/README.md) | Inspect gamepad fields using their HID usages. |
| [EspUsbHostHIDRawDump](HID/EspUsbHostHIDRawDump/README.md) | Print raw HID input before applying an application-specific interpretation. |
| [EspUsbHostHIDVendor](HID/EspUsbHostHIDVendor/README.md) | Receive vendor HID input and send matching output or feature reports. |
| [EspUsbHostKeyboard](HID/EspUsbHostKeyboard/README.md) | Read a USB keyboard and follow its Lock LED state. |
| [EspUsbHostKeyboardDump](HID/EspUsbHostKeyboardDump/README.md) | Inspect keyboard usages, modifiers and translated characters. |
| [EspUsbHostKeyboardNKRO](HID/EspUsbHostKeyboardNKRO/README.md) | Observe held-key bitmaps and simultaneous key changes. |
| [EspUsbHostMacroPadN3](HID/EspUsbHostMacroPadN3/README.md) | Render LCD key images and receive macro-pad controls. |
| [EspUsbHostMouse](HID/EspUsbHostMouse/README.md) | Decode mouse motion and individual button transitions. |
| [EspUsbHostSystemControl](HID/EspUsbHostSystemControl/README.md) | Receive system-control HID inputs. |
| [EspUsbHostBringUpCheck](Info/EspUsbHostBringUpCheck/README.md) | Check host startup separately from peripheral enumeration. |
| [EspUsbHostCustomDeviceCallbacks](Info/EspUsbHostCustomDeviceCallbacks/README.md) | Classify connected devices from their interface information. |
| [EspUsbHostDeviceExplorer](Info/EspUsbHostDeviceExplorer/README.md) | Inspect an unknown device and choose a suitable communication path. |
| [EspUsbHostDeviceInfo](Info/EspUsbHostDeviceInfo/README.md) | Enumerate devices and print their interfaces and endpoints. |
| [EspUsbHostHIDReportDescriptor](Info/EspUsbHostHIDReportDescriptor/README.md) | Capture the HID report descriptor used to interpret input bytes. |
| [EspUsbHostHubPPPS](Info/EspUsbHostHubPPPS/README.md) | Inspect hub port power and request individual port switching. |
| [EspUsbHostP4FsPhyRouting](Info/EspUsbHostP4FsPhyRouting/README.md) | Route the P4 full-speed PHY before starting USB Host. |
| [EspUsbHostMIDI](MIDI/EspUsbHostMIDI/README.md) | Decode USB MIDI input and send explicit test messages. |
| [EspUsbHostDisplayTuring](Serial/EspUsbHostDisplayTuring/README.md) | Render a scene on a compatible Turing USB serial display. |
| [EspUsbHostMultiUSBSerial](Serial/EspUsbHostMultiUSBSerial/README.md) | Bind separate serial wrappers to selected USB device addresses. |
| [EspUsbHostUSBSerial](Serial/EspUsbHostUSBSerial/README.md) | Bridge a USB serial peripheral to the board diagnostic serial port. |
| [EspUsbHostMSCBlockDump](Storage/EspUsbHostMSCBlockDump/README.md) | Read storage capacity and inspect the first raw block. |
| [EspUsbHostMSCFatList](Storage/EspUsbHostMSCFatList/README.md) | Mount FAT storage, list files and perform a write/read/delete probe. |
| [UsbNetwork](UsbNetwork/README.md) | Attach a USB network adapter and use the IP networking stack. |
| [EspUsbHostAdbConnect](Vendor/EspUsbHostAdbConnect/README.md) | Authenticate an ADB connection and run a bounded shell echo test. |
| [EspUsbHostDisplayAx206](Vendor/EspUsbHostDisplayAx206/README.md) | Render a scene on a compatible AX206 USB display. |
| [EspUsbHostDisplayDl1xx](Vendor/EspUsbHostDisplayDl1xx/README.md) | Render a scene through a compatible DL-1xx USB graphics adapter. |
| [EspUsbHostPrinterEscPos](Vendor/EspUsbHostPrinterEscPos/README.md) | Inspect a printer and optionally send an ESC/POS sample. |
| [EspUsbHostProtocolConsole](Vendor/EspUsbHostProtocolConsole/README.md) | Issue explicit control and bulk requests from a diagnostic console. |
| [EspUsbHostUsbtmcScpi](Vendor/EspUsbHostUsbtmcScpi/README.md) | Exchange SCPI commands with a compatible USBTMC instrument. |
| [EspUsbHostVendorBulk](Vendor/EspUsbHostVendorBulk/README.md) | Open a matching interface and exercise vendor bulk and control transfers. |
| [EspUsbHostVideoCamera](Video/EspUsbHostVideoCamera/README.md) | Select a UVC stream and hand completed frames to foreground processing. |
