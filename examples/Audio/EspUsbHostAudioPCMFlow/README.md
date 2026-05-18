# EspUsbHostAudioPCMFlow

> 日本語版: [README.ja.md](README.ja.md)

Uses [PCMFlow](https://github.com/tanakamasayuki/PCMFlow) to generate formatted PCM and sends it to a USB Audio output device.

## Hardware

- ESP32-S3 (or another board supported by Arduino-ESP32 USB Host)
- USB Audio output device, such as a USB speaker

## What it does

- Provides a generated WAV stream to PCMFlow through `ByteStream`
- Converts it to 48 kHz, stereo, 16-bit PCM with PCMFlow
- Reads the requested frame count with `audio.readFrames()` from `onAudioOutputRequest()`
- Lets EspUsbHost drive the USB Audio OUT transfer timing

## Notes

- This is a temporary PCMFlow integration example. PCMFlow decodes/converts the audio, and EspUsbHost manages USB Audio OUT timing.
- The important integration point is `PCMFlow::readFrames(void *out, size_t frameCount)`.

## Dependencies

- EspUsbHost
- PCMFlow
