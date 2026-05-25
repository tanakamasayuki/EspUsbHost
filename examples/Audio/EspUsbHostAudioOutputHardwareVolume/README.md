# EspUsbHostAudioOutputHardwareVolume

> 日本語版: [README.ja.md](README.ja.md)

Generates a simple USB Audio OUT tone and tries to control the USB device's hardware mute/volume Feature Unit.

## Hardware

- ESP32-S3 (or another board supported by Arduino-ESP32 USB Host)
- USB Audio output device with hardware mute or volume support

## Notes

- Many USB speakers and small USB audio adapters do not expose usable hardware volume controls.
- This example is for checking USB Audio Class Feature Unit support. For normal playback, start with `EspUsbHostAudioOutputTone` or `EspUsbHostAudioOutputMP3PCMFlow`.
- The tone stream uses 48 kHz, 16-bit, stereo PCM.
- The example prints detected Feature Units, unmutes the selected unit when mute is available, and sets volume to `USB_OUTPUT_VOLUME_DB` with clamping.

## Key APIs

- `usb.getAudioFeatureUnits(address, units, maxUnits)` — get parsed USB Audio Feature Units
- `usb.audioHasMute(address, unitId)` / `usb.audioSetMute(mute, address, unitId)`
- `usb.audioHasVolume(address, unitId)`
- `usb.audioGetVolumeRange(range, address, unitId)`
- `usb.audioSetVolumeDbClamped(db, address, unitId)`
- `usb.audioGetVolumeDb(db, address, unitId)`

## Expected Serial output

```
EspUsbHost Audio Output Hardware Volume example start
connected: device: address=1 portId=0x01 vid=1234 pid=5678 class=0x00(Device) speed=full product="USB Speaker"
audio stream: addr=1 iface=1 alt=1 ep=0x01 dir=OUT channels=2 bytes=2 bits=16 rate=48000 rates=1 max_packet=196 interval=1
audio feature unit: unit=2 source=1 channels=2 master=0x3
audio mute off
audio volume range: -64.00..0.00 dB step 1.00 dB
audio hardware volume: -12.00 dB
audio output ready: addr=1
```
