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
audio stream: addr=1 iface=1 alt=1 ep=0x01 dir=OUT channels=2 bytes=2 bits=16 rate=48000 rates=1 max_packet=196 interval=1 proto=UAC1 clock=0 startable=1
audio feature unit: unit=2 source=1 channels=2 master=0x3
audio mute off
audio volume range: -64.00..0.00 dB step 1.00 dB
audio hardware volume: -12.00 dB
audio output ready: addr=1
```

## Walkthrough and checks

### Before running

Distinguish generated PCM level from USB hardware volume controls. Follow the hardware and dependency requirements above, select the board and the example's profile, and locate the diagnostic Serial connection. [Example workflow](../../../docs/example-workflow.md) explains installation, connectors, USB roles and the difference between startup and a usable peer.

### Follow the code

After selecting the output stream, inspect Feature Units and their supported controls before setting mute/volume. Compare the requested dB with the read-back value and range. No suitable Feature Unit is a capability difference, not proof that audio streaming failed.

### API use and adaptation

USB audio directions are named from the device/host relationship. Confirm direction, sample rate, channels, bytes per sample and valid bits before interpreting PCM. A frame is one sample for every channel; a byte count is not a frame count. Interleaved stereo alternates channels rather than sending two independent byte streams.

Streaming must be enabled by the host. Track stream state and underflow/overflow statistics separately from successful USB enumeration. Buffers and queues are finite, and an example that ignores a write's returned count does not promise lossless audio. Keep USB callbacks short; perform decoding, storage and expensive analysis elsewhere. Start with a known test signal before adding a codec or external hardware.

### Verify one change at a time

Run the unchanged example first and compare the operation above with its logs or host application. Test the peer absent and then connected; note which stage succeeds instead of expecting the final result immediately. Reset to the original configuration before changing one pin, format or command. The demonstration's error logging is not a general retry or recovery policy.

- [Sketch](EspUsbHostAudioOutputHardwareVolume.ino)
- [All examples](../../README.md)
- [Example workflow](../../../docs/example-workflow.md)
