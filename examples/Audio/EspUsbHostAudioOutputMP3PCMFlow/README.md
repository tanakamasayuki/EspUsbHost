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
- Reads the requested frame count with `audio.readFrames()` from `onAudioOutputRequest()`
- Lets EspUsbHost drive the USB Audio OUT transfer timing

## Notes

- PCMFlow decodes/converts the audio, and EspUsbHost manages USB Audio OUT timing.
- This example supports USB Audio OUT streams that report 1 or 2 channels and 8-bit or 16-bit PCM.
- The important integration point is `PCMFlow::readFrames(void *out, size_t frameCount)`.
- PCMFlow is a lightweight MP3 decoding / PCM conversion library under the MIT license. If you only need MP3 playback, this is the recommended starting point.

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

## Assets

The bundled sound effects are from [512 Sound Effects (8-bit style)](https://opengameart.org/content/512-sound-effects-8-bit-style)
by Juhani Junkala, published on [OpenGameArt.org](https://opengameart.org) under the
[CC0 1.0 Universal (Public Domain)](https://creativecommons.org/publicdomain/zero/1.0/) license.
