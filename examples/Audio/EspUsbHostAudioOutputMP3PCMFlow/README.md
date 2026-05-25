# EspUsbHostAudioOutputMP3PCMFlow

> 日本語版: [README.ja.md](README.ja.md)

Uses [PCMFlow](https://github.com/tanakamasayuki/PCMFlow) to decode embedded MP3 assets and send formatted PCM to a USB Audio output device.

## Hardware

- ESP32-S3 (or another board supported by Arduino-ESP32 USB Host)
- USB Audio output device, such as a USB speaker

## What it does

- Uses the embedded MP3 assets in this sketch
- Opens each MP3 with `audio.open(data, size, PCMFlow::CodecKind::Mp3)`
- Selects an OUT stream from the connected USB Audio device
- Configures PCMFlow to the selected USB Audio format, such as mono for a mono speaker
- Uses a fixed software gain of 80%
- Reads the requested frame count with `audio.readFrames()` from `onAudioOutputRequest()`
- Lets EspUsbHost drive the USB Audio OUT transfer timing

## Notes

- PCMFlow decodes/converts the audio, and EspUsbHost manages USB Audio OUT timing.
- `espUsbHostSelectAudioOutputStream()` is called without a custom filter, so the library scores the available streams and prefers common formats such as 48 kHz / 16-bit / stereo.
- The important integration point is `PCMFlow::readFrames(void *out, size_t frameCount)`.
- Decoding runs in `loop()` via `audio.pump()`; only already-decoded PCM frames are copied in the USB callback.
- Hardware volume control is intentionally not used here. See `EspUsbHostAudioOutputHardwareVolume` for Feature Unit volume control.
- PCMFlow is a lightweight MP3 decoding / PCM conversion library under the MIT license. If you only need MP3 playback, this is the recommended starting point.

## Flow

1. `setup()` registers `usb.onDeviceConnected()`, `usb.onDeviceDisconnected()`, and `usb.onAudioOutputRequest()`.
2. `usb.begin()` starts USB Host.
3. When a USB Audio device is connected, `onDeviceConnected()` is called.
4. `usb.audioOutputReady(info.address)` checks whether the device has a USB Audio OUT endpoint.
5. `usb.getAudioStreams()` returns the parsed Audio stream candidates. Each candidate is printed with `espUsbHostPrint()`.
6. `espUsbHostSelectAudioOutputStream()` scores candidates and picks the best stream and sample rate.
7. `audio.setOutputFormat(outputFormat)` configures PCMFlow to match the selected USB stream (sample rate, channels, bit depth).
8. `usb.audioOutputStart(stream, sampleRate, address)` starts the selected output stream.
9. `openNextFile()` opens the first embedded MP3 with `audio.open()`.
10. When the USB Audio OUT transfer needs PCM frames, `onAudioOutputRequest()` is called.
11. `audio.readFrames(request.data, request.frameCount)` copies decoded frames and returns the frame count.
12. The sketch sets `request.writtenFrames` to that count.
13. `loop()` calls `audio.pump()` to advance decoding, and `openNextFile()` when `audio.isEof()` is true.

## Key APIs

- `usb.onAudioOutputRequest(callback)` — fired each time the USB transfer needs the next PCM frames
  - `request.data` — buffer to fill with interleaved PCM samples
  - `request.frameCount` — number of frames requested
  - `request.writtenFrames` — set this to the number of frames actually written
  - `request.channels` — channel count of the selected stream
- `usb.audioOutputReady(address)` — true when a USB Audio OUT endpoint was found
- `usb.getAudioStreams(address, streams, maxStreams)` — return parsed audio stream candidates
- `espUsbHostSelectAudioOutputStream(streams, count)` — score candidates and return the best match
- `usb.audioOutputStart(stream, sampleRate, address)` — start the selected output stream
- `audio.open(data, size, PCMFlow::CodecKind::Mp3)` — open an embedded MP3 for decoding
- `audio.setOutputFormat(format)` — set output sample rate, channels, and bit depth
- `audio.setGain(gain)` — set playback volume (0.0–1.0)
- `audio.readFrames(out, frameCount)` — copy decoded frames; returns the number written
- `audio.pump()` — advance decoding; call from `loop()`
- `audio.isEof()` — true when the current file has finished

## Dependencies

- EspUsbHost
- [PCMFlow](https://github.com/tanakamasayuki/PCMFlow) (MIT License) — MP3 decoding / PCM conversion

## Choosing a Codec Library

This repository includes two examples that play MP3 through USB Audio OUT.

| Example | Best fit |
| --- | --- |
| `EspUsbHostAudioOutputMP3PCMFlow` | Use this when you only need MP3 playback. It is lightweight, MIT licensed, and straightforward for USB Audio PCM conversion. |
| `EspUsbHostAudioOutputMP3ESP8266Audio` | Use this when you want to integrate with ESP8266Audio. It supports many formats, but it is GPL-3.0 licensed and needs more glue code for USB Audio. |

For MP3-only playback, start with the PCMFlow example. ESP8266Audio is well-known and supports many codecs, but in a USB Audio sketch you mainly use its codec layer rather than its output layer. That means the sketch needs extra buffering and resampling code, and builds can take longer because the library includes support for many codecs.

## Expected Serial output

```
EspUsbHost Audio Output MP3 PCMFlow example start
playing: /sfx_alarm_loop6.mp3 (5447 bytes)
connected: device: address=1 portId=0x01 vid=1234 pid=5678 class=0x00(Device) speed=full product="USB Speaker"
audio stream: addr=1 iface=1 alt=1 ep=0x03 dir=OUT channels=2 bytes=2 bits=16 rate=48000 rates=1 max_packet=192 interval=1
selected PCMFlow output: 48000 Hz, 2 ch, 16-bit
audio output ready: addr=1
playing: /sfx_coin_double1.mp3 (4265 bytes)
...
all files played
```

## Assets

The bundled sound effects are from [512 Sound Effects (8-bit style)](https://opengameart.org/content/512-sound-effects-8-bit-style)
by Juhani Junkala, published on [OpenGameArt.org](https://opengameart.org) under the
[CC0 1.0 Universal (Public Domain)](https://creativecommons.org/publicdomain/zero/1.0/) license.
