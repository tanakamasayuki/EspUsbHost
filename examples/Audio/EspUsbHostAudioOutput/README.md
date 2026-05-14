# EspUsbHostAudioOutput

Sends raw USB Audio isochronous OUT payloads to a USB speaker or audio interface.

## Hardware

- ESP32-S3 (or another board supported by Arduino-ESP32 USB Host)
- USB Audio output device, such as a USB speaker

## Notes

- This example sends 48 kHz, 16-bit, mono PCM chunks.
- It prints detected USB Audio stream format information and only starts output when a compatible OUT stream is found.
- Adjust `SAMPLE_RATE` and the sample format constants in the sketch to match your device.
- Some devices may require additional feature-unit or clock control support.

## Expected Serial output

```
EspUsbHost Audio Output example start
connected: addr=1 portId=0x01 vid=1234 pid=5678 product=USB Speaker
audio stream: iface=1 alt=1 ep=0x01 dir=OUT channels=1 bytes=2 bits=16 rate=48000 max=98 interval=1
audio output ready: addr=1
```
