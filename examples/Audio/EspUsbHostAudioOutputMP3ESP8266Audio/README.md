# EspUsbHostAudioOutputMP3ESP8266Audio

> 日本語版: [README.ja.md](README.ja.md)

Uses [ESP8266Audio](https://github.com/earlephilhower/ESP8266Audio) to decode embedded MP3 files and play them sequentially through a USB speaker or audio interface.

## Hardware

- ESP32-S3 (or another board supported by Arduino-ESP32 USB Host)
- USB Audio output device, such as a USB speaker

## Dependencies

- [ESP8266Audio](https://github.com/earlephilhower/ESP8266Audio) (available in Arduino Library Manager, GPL-3.0 License) — MP3 decoding

## What it does

- Embeds MP3 files as C arrays via `assets_embed.h` and plays them in order
- Decodes each MP3 with `AudioGeneratorMP3` and feeds decoded PCM into a ring buffer via `RingBufferOutput`
- Resamples from the MP3 source rate to 48 kHz with a linear interpolation resampler in `RingBufferOutput::ConsumeSample()`
- In `onAudioOutputRequest()`, copies buffered PCM into `request.data` and sets `request.writtenFrames`
- Lets EspUsbHost drive USB Audio OUT timing; decoding runs from `loop()`

## Notes

- MP3 files are embedded as C arrays via `assets_embed.h` and played back in order. Non-MP3 files are skipped.
- The decoder reports the MP3 sample rate via `SetRate()`. A linear interpolation resampler converts it to the USB output rate (48 kHz).
- USB output is 48 kHz, 16-bit stereo PCM. The connected device must support this format.
- The sketch reads the requested frame count from its ring buffer in `onAudioOutputRequest()` and lets the library drive transfer timing.
- Playback loops through all files once, then stops.
- ESP8266Audio is a feature-rich library that supports multiple audio formats, not only MP3. It is GPL-3.0 licensed, so check the license terms before using it in a distributed project.
- ESP8266Audio also includes output support, but this USB Audio sketch lets EspUsbHost manage USB Audio OUT. As a result, this example mainly uses the codec layer and needs extra buffering and resampling glue code.
- Because ESP8266Audio supports many codecs, builds can take longer than the PCMFlow example.

## Key APIs

- `usb.onAudioOutputRequest(callback)` — fired each time the USB transfer needs the next PCM frames
  - `request.data` — buffer to fill with interleaved PCM samples
  - `request.frameCount` — number of frames requested
  - `request.writtenFrames` — set this to the number of frames actually written
  - `request.channels` — channel count of the selected stream
- `usb.audioOutputReady(address)` — true when a USB Audio OUT endpoint was found
- `usb.getAudioStreams(address, streams, maxStreams)` — return parsed audio stream candidates
- `espUsbHostSelectAudioOutputStream(streams, count, filter)` — score candidates and return the best match
- `usb.audioOutputStart(stream, sampleRate, address)` — start the selected output stream
- `AudioFileSourcePROGMEM(data, size)` — wrap embedded MP3 data as an ESP8266Audio source
- `AudioGeneratorMP3` / `mp3gen->begin(src, output)` / `mp3gen->loop()` — drive MP3 decoding; call `loop()` from `loop()`
- `RingBufferOutput::SetRate(hz)` — called by the decoder to report the MP3 source rate; resets the resampler
- `RingBufferOutput::ConsumeSample(sample)` — linear-interpolation resampler from source rate to 48 kHz; writes to the ring buffer

## Choosing a Codec Library

This repository includes two examples that play MP3 through USB Audio OUT.

| Example | Best fit |
| --- | --- |
| `EspUsbHostAudioOutputMP3PCMFlow` | Use this when you only need MP3 playback. It is lightweight, MIT licensed, and straightforward for USB Audio PCM conversion. |
| `EspUsbHostAudioOutputMP3ESP8266Audio` | Use this when you want to integrate with ESP8266Audio. It supports many formats, but it is GPL-3.0 licensed and needs more glue code for USB Audio. |

For MP3-only playback, start with the PCMFlow example. This ESP8266Audio example is useful when you already use ESP8266Audio in a project, or when you want to verify integration with ESP8266Audio's codec layer.

## Expected Serial output

```
EspUsbHost Audio Output MP3 ESP8266Audio example start
playing: /sfx_alarm_loop6.mp3 (5447 bytes)
connected: device: address=1 portId=0x01 vid=xxxx pid=xxxx class=0x00(Device) speed=full product="USB Speaker"
audio stream: addr=1 iface=1 alt=1 ep=0x03 dir=OUT channels=2 bytes=2 bits=16 rate=48000 rates=1 max_packet=192 interval=1
audio output ready: addr=1
playing: /sfx_coin_double1.mp3 (4265 bytes)
...
all files played
```

## Assets

The bundled sound effects are from [512 Sound Effects (8-bit style)](https://opengameart.org/content/512-sound-effects-8-bit-style)
by Juhani Junkala, published on [OpenGameArt.org](https://opengameart.org) under the
[CC0 1.0 Universal (Public Domain)](https://creativecommons.org/publicdomain/zero/1.0/) license.
