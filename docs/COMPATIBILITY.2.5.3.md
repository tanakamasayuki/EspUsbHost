# EspUsbHost 2.5.3 — arduino-esp32 core compatibility

- Library ref: `v2.5.3`
- Targets: esp32s3, esp32s2, esp32p4
- Core versions: 3.2.0, 3.2.1, 3.3.0, 3.3.1, 3.3.2, 3.3.3, 3.3.4, 3.3.5, 3.3.6, 3.3.7, 3.3.8, 3.3.9, 3.3.10, 3.3.11

Legend: ✅ builds · ❌ fails · — example absent in this version · · not applicable (no profile / board not in core)

## esp32s3

| Feature (example) | 3.2.0 | 3.2.1 | 3.3.0 | 3.3.1 | 3.3.2 | 3.3.3 | 3.3.4 | 3.3.5 | 3.3.6 | 3.3.7 | 3.3.8 | 3.3.9 | 3.3.10 | 3.3.11 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| HID (`HID/EspUsbHostKeyboard`) | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| Serial (`Serial/EspUsbHostUSBSerial`) | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| Audio (`Audio/EspUsbHostAudioOutputTone`) | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| Storage (`Storage/EspUsbHostMSCFatList`) | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| MIDI (`MIDI/EspUsbHostMIDI`) | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| Vendor (`Vendor/EspUsbHostVendorBulk`) | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| Network (`UsbNetwork`) | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| Info (`Info/EspUsbHostDeviceInfo`) | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |

## esp32s2

| Feature (example) | 3.2.0 | 3.2.1 | 3.3.0 | 3.3.1 | 3.3.2 | 3.3.3 | 3.3.4 | 3.3.5 | 3.3.6 | 3.3.7 | 3.3.8 | 3.3.9 | 3.3.10 | 3.3.11 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| HID (`HID/EspUsbHostKeyboard`) | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| Serial (`Serial/EspUsbHostUSBSerial`) | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| Audio (`Audio/EspUsbHostAudioOutputTone`) | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| Storage (`Storage/EspUsbHostMSCFatList`) | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| MIDI (`MIDI/EspUsbHostMIDI`) | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| Vendor (`Vendor/EspUsbHostVendorBulk`) | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| Network (`UsbNetwork`) | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| Info (`Info/EspUsbHostDeviceInfo`) | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |

## esp32p4

| Feature (example) | 3.2.0 | 3.2.1 | 3.3.0 | 3.3.1 | 3.3.2 | 3.3.3 | 3.3.4 | 3.3.5 | 3.3.6 | 3.3.7 | 3.3.8 | 3.3.9 | 3.3.10 | 3.3.11 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| HID (`HID/EspUsbHostKeyboard`) | ❌ | ❌ | ❌ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| Serial (`Serial/EspUsbHostUSBSerial`) | ❌ | ❌ | ❌ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| Audio (`Audio/EspUsbHostAudioOutputTone`) | ❌ | ❌ | ❌ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| Storage (`Storage/EspUsbHostMSCFatList`) | ❌ | ❌ | ❌ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| MIDI (`MIDI/EspUsbHostMIDI`) | ❌ | ❌ | ❌ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| Vendor (`Vendor/EspUsbHostVendorBulk`) | ❌ | ❌ | ❌ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| Network (`UsbNetwork`) | ❌ | ❌ | ❌ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| Info (`Info/EspUsbHostDeviceInfo`) | ❌ | ❌ | ❌ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |

