# EspUsbHostAudioInput

Receives USB Audio isochronous IN data from a USB microphone or audio interface and prints the received byte rate along with per-channel peak amplitude and dBFS.

## Hardware

- ESP32-S3 (or another board supported by Arduino-ESP32 USB Host)
- USB Audio input device, such as a USB microphone

## Notes

- This example prints detected USB Audio stream format information, the raw audio payload byte rate, and per-channel peak amplitude (absolute value of int16 PCM) with dBFS.
- Peak amplitude is computed only for 16-bit PCM streams (`bytesPerSample == 2`). For other formats only the byte count is printed.
- Up to `MAX_PEAK_CHANNELS` (default 8) channels are tracked. Peaks are reset every second after printing.
- Select accepted input formats by editing `isSupportedInputStream(sampleRate, channels, bitsPerSample)`.
- By default, the sketch accepts `48000 Hz` or `44100 Hz` input streams. The shared scoring helper prefers `48000 Hz`, 16-bit, and stereo when available.
- `audioInputReady()` detects a USB Audio IN endpoint, and `audioInputStart()` starts the selected input stream.
- The selected stream is started with `audioInputStart()`, so the selected sample rate, interface, alternate setting, and endpoint are passed to the library.
- It does not decode, play, or store audio.
- Devices that require explicit sample-rate or feature-unit control may need additional support.

## Expected Serial output

```
EspUsbHost Audio Input example start
connected: device: address=1 portId=0x01 vid=1234 pid=5678 class=0x00(Device) speed=full product="USB Microphone"
audio stream: addr=1 iface=1 alt=1 ep=0x81 dir=IN channels=1 bytes=2 bits=16 rate=48000 rates=1 max_packet=96 interval=1
audio input selected: addr=1 iface=1 alt=1 channels=1 bytes=2 bits=16 rate=48000
audio input ready: addr=1
audio: addr=1 iface=1 channels=1 bits=16 rate=48000 bytes_per_sec=96000 ch0_peak=12345( -8.5dBFS)
```
