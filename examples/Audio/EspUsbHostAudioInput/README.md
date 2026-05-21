# EspUsbHostAudioInput

Receives USB Audio isochronous IN data from a USB microphone or audio interface and prints the received byte rate.

## Hardware

- ESP32-S3 (or another board supported by Arduino-ESP32 USB Host)
- USB Audio input device, such as a USB microphone

## Notes

- This example prints detected USB Audio stream format information and reports raw audio payload bytes.
- Select the input format with `PREFERRED_SAMPLE_RATE`, `PREFERRED_CHANNELS`, and `PREFERRED_BITS_PER_SAMPLE` at the top of the sketch.
- By default, the sketch selects the first input stream that supports `48000 Hz`; channel count and bit depth come from the selected device stream.
- `audioInputReady()` detects a USB Audio IN endpoint, and `audioInputStart()` starts the selected input stream.
- The selected stream is started with `audioInputStart()`, so the preferred channel count, bit depth, and sample rate are passed to the library.
- Set a preferred value to `0` to leave it unconstrained. Set all three to `0` to use the first input stream format.
- It does not decode, play, or store audio.
- Devices that require explicit sample-rate or feature-unit control may need additional support.

## Expected Serial output

```
EspUsbHost Audio Input example start
connected: device: address=1 portId=0x01 vid=1234 pid=5678 class=0x00(Device) speed=full product="USB Microphone"
audio stream: addr=1 iface=1 alt=1 ep=0x81 dir=IN channels=1 bytes=2 bits=16 rate=48000 rates=1 max_packet=96 interval=1
audio input selected: addr=1 iface=1 alt=1 channels=1 bytes=2 bits=16 rate=48000
audio input ready: addr=1
audio: addr=1 iface=1 channels=1 bits=16 rate=48000 bytes_per_sec=96000 last_chunk=96
```
