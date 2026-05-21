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

## Dependencies

- EspUsbHost
- PCMFlow
