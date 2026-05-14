# EspUsbHostAudioInput

Receives USB Audio isochronous IN data from a USB microphone or audio interface and prints the received byte rate.

## Hardware

- ESP32-S3 (or another board supported by Arduino-ESP32 USB Host)
- USB Audio input device, such as a USB microphone

## Notes

- This example prints detected USB Audio stream format information and reports raw audio payload bytes.
- It does not decode, play, or store audio.
- Devices that require explicit sample-rate or feature-unit control may need additional support.

## Expected Serial output

```
EspUsbHost Audio Input example start
connected: addr=1 portId=0x01 vid=1234 pid=5678 product=USB Microphone
audio stream: iface=1 alt=1 ep=0x81 dir=IN channels=1 bytes=2 bits=16 rate=48000 rates=1 max=96 interval=1
audio: addr=1 iface=1 bytes_per_sec=96000 last_chunk=96
```
