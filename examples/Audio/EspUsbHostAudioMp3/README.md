# EspUsbHostAudioMp3

Plays embedded MP3 files sequentially through a USB speaker or audio interface.

## Hardware

- ESP32-S3 (or another board supported by Arduino-ESP32 USB Host)
- USB Audio output device, such as a USB speaker

## Dependencies

- [ESP8266Audio](https://github.com/earlephilhower/ESP8266Audio) (available in Arduino Library Manager) — MP3 decoding

## Notes

- MP3 files are embedded as C arrays via `assets_embed.h` and played back in order. Non-MP3 files are skipped.
- The decoder reports the MP3 sample rate via `SetRate()`. A linear interpolation resampler converts it to the USB output rate (48 kHz).
- USB output is 48 kHz, 16-bit stereo PCM. The connected device must support this format.
- The sketch reads the requested frame count from its ring buffer in `onAudioOutputRequest()` and lets the library drive transfer timing.
- Playback loops through all files once, then stops.

## Expected Serial output

```
EspUsbHost Audio MP3 example start
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
