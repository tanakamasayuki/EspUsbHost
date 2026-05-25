# EspUsbHostAudioOutputTone

Generates simple stereo tones (different frequencies for left/right) and sends them to a USB Audio OUT device such as a USB speaker or audio interface.

## Hardware

- ESP32-S3 (or another board supported by Arduino-ESP32 USB Host)
- USB Audio output device, such as a USB speaker

## Notes

- This example sends 48 kHz, 16-bit, stereo (2-channel) PCM.
- It prints detected USB Audio stream format information and only starts output when a compatible OUT stream is found.
- It generates the number of frames requested by `onAudioOutputRequest()` and lets the library drive the USB transfer timing.
- Adjust the conditions in `isSupportedOutputStream()` to match your device.
- Hardware volume control is not used in this basic tone example. See `EspUsbHostAudioOutputHardwareVolume` for Feature Unit volume control.

## Sketch Settings

This example defines the generated tone PCM format with constants at the top of the sketch.

| Constant | Meaning | Default |
| --- | --- | --- |
| `TONE_HZ_LEFT` | Generated tone frequency for the left channel | `440` |
| `TONE_HZ_RIGHT` | Generated tone frequency for the right channel | `880` |
| `VOLUME` | 16-bit PCM amplitude | `100` |

The accepted PCM format is defined in `isSupportedOutputStream()` and defaults to 48 kHz / 2 channels / 16-bit. The sketch selects only a USB Audio OUT stream that exactly matches that format.

## Flow

1. `setup()` registers `usb.onDeviceConnected()`, `usb.onDeviceDisconnected()`, and `usb.onAudioOutputRequest()`.
2. `usb.begin()` starts USB Host.
3. When a USB Audio device is connected, `onDeviceConnected()` is called.
4. `usb.audioOutputReady(info.address)` checks whether the device has a USB Audio OUT endpoint.
5. `usb.getAudioStreams()` returns the parsed Audio stream candidates.
6. Each candidate is printed with `espUsbHostPrint(streams[i])`.
7. `isSupportedOutputStream(sampleRate, channels, bitsPerSample)` defines the accepted OUT stream formats.
8. `espUsbHostSelectAudioOutputStream()` scores the accepted candidates and selects the best stream and sample rate.
9. `usb.audioOutputStart(streams[selected.index], selected.sampleRate, info.address)` starts the selected output stream. This call passes the sample rate, interface, alternate setting, and endpoint to the library.
10. When the USB Audio OUT transfer needs the next PCM frames, `onAudioOutputRequest()` is called.
11. `fillTone()` generates `request.frameCount` PCM frames and writes them to `request.data`.
12. The sketch sets `request.writtenFrames` to the number of generated frames.
13. The library sends `request.data` to the USB Audio OUT endpoint.

## `audioInputReady()` and `audioOutputReady()`

Input uses `audioInputReady()` to detect a USB Audio IN endpoint, then `audioInputStart()` to start the selected input stream.

Output uses `audioOutputReady()` to detect a USB Audio OUT endpoint, then `audioOutputStart()` to start the selected output stream. This sketch passes the stream matching the tone format to `audioOutputStart()`.

| API | Meaning |
| --- | --- |
| `audioInputReady(address)` | A USB Audio IN endpoint was found |
| `audioInputStart(stream, sampleRate, address)` | Start the specified input stream |
| `audioOutputReady(address)` | A USB Audio OUT endpoint was found |
| `audioOutputStart(stream, sampleRate, address)` | Start the specified output stream |
| `streams[i].input` | Data flows from the USB device to the ESP32 |
| `streams[i].output` | Data flows from the ESP32 to the USB device |

## Expected Serial output

```
EspUsbHost Audio Output Tone example start
connected: device: address=1 portId=0x01 vid=1234 pid=5678 class=0x00(Device) speed=full product="USB Speaker"
audio stream: addr=1 iface=1 alt=1 ep=0x01 dir=OUT channels=2 bytes=2 bits=16 rate=48000 rates=1 max_packet=196 interval=1
audio output ready: addr=1
```

## See also

- [EspUsbHostAudioOutputHardwareVolume](../EspUsbHostAudioOutputHardwareVolume/) — USB Audio Feature Unit mute/volume control
