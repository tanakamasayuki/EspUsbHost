# EspUsbHostAudioInput

> 日本語版: [README.ja.md](README.ja.md)

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
- It does not decode, play, or store audio.
- Devices that require explicit sample-rate or feature-unit control may need additional support.

## Flow

1. `setup()` registers `usb.onDeviceConnected()`, `usb.onDeviceDisconnected()`, and `usb.onAudioData()`.
2. `usb.begin()` starts USB Host.
3. When a USB Audio device is connected, `onDeviceConnected()` is called.
4. `usb.audioInputReady(info.address)` checks whether the device has a USB Audio IN endpoint.
5. `usb.getAudioStreams()` returns the parsed Audio stream candidates.
6. Each candidate is printed with `espUsbHostPrint(streams[i])`.
7. `isSupportedInputStream(sampleRate, channels, bitsPerSample)` defines the accepted IN stream formats.
8. `espUsbHostSelectAudioInputStream()` scores the accepted candidates and selects the best stream and sample rate.
9. The selected candidate is stored in `selectedStream`.
10. `usb.audioInputStart(selectedStream, selected.sampleRate, info.address)` starts the selected input stream. This call passes the sample rate, interface, alternate setting, and endpoint to the library.
11. When audio data is received, `onAudioData()` is called.
12. `onAudioData()` counts only the data that matches the selected device address and interface number.
13. While receiving, the per-channel peak amplitude (absolute int16 value) is updated.
14. Once per second, the selected `channels`, `bits`, `rate`, the received byte count, and each channel's `ch{N}_peak` with dBFS are printed, then the peaks are reset.

## `audioInputReady()` and `audioOutputReady()`

This input example calls `audioInputReady()` inside `onDeviceConnected()`. `audioInputReady()` only confirms that an Audio IN endpoint was detected; it does not select a format or start any transfer.

In the current library implementation, `audioInputReady()` checks whether a USB Audio IN endpoint has been found on the target device. At that point no format has been selected yet. Therefore this example looks at `streams[i].input` in the results of `getAudioStreams()`, picks an IN-direction stream that matches the desired format, and starts it with `audioInputStart()`.

The output-side counterpart is `audioOutputReady()`, which checks whether an Audio OUT endpoint was found. The correspondence is:

| API | Meaning |
| --- | --- |
| `audioInputReady(address)` | A USB Audio IN endpoint was found |
| `audioInputStart(stream, sampleRate, address)` | Start the specified input stream |
| `audioInputStop(address)` | Drain capture transfers and callbacks, then select alternate 0; true on success |
| `audioInputRunning(address)` | Capture has been started and has not been stopped |
| `audioOutputReady(address)` | A USB Audio OUT endpoint was found |
| `audioOutputStart(stream, sampleRate, address)` | Start the specified output stream |
| `streams[i].input` | Data flows from the USB device to the ESP32 |
| `streams[i].output` | Data flows from the ESP32 to the USB device |

Input is detected with `audioInputReady()` and started with `audioInputStart()` with an explicit format. Output is detected with `audioOutputReady()`, and the output examples call `audioOutputStart()`.

## Stopping, restarting and capture rate

Capture queues up to four transfers of eight packets each to cover the resubmission gap. Call `audioInputStop()` from `loop()` or another application task, never from a USB callback: it waits for the client task to drain transfers and callbacks. Restart the same stream with `audioInputStart()`. Starting from a connection callback remains supported; in that case success means the control request was queued.

Slow callbacks can exhaust the queue and lose isochronous packets. Record measured throughput separately from the configured sample rate. For 32 kHz, 16-bit mono, the expected rate is 64,000 B/s; a continuous sample sequence alone does not prove capture occurred every USB frame.

## Expected Serial output

```
EspUsbHost Audio Input example start
connected: device: address=1 portId=0x01 vid=1234 pid=5678 class=0x00(Device) speed=full product="USB Microphone"
audio stream: addr=1 iface=1 alt=1 ep=0x81 dir=IN channels=1 bytes=2 bits=16 rate=48000 rates=1 max_packet=96 interval=1 proto=UAC1 clock=0 startable=1
audio input selected: addr=1 iface=1 alt=1 channels=1 bytes=2 bits=16 rate=48000
audio input ready: addr=1
audio: addr=1 iface=1 channels=1 bits=16 rate=48000 bytes_per_sec=96000 ch0_peak=12345( -8.5dBFS)
```

## Walkthrough and checks

### Before running

Receive USB microphone PCM and inspect signal peaks. Follow the hardware and dependency requirements above, select the board and the example's profile, and locate the diagnostic Serial connection. [Example workflow](../../../docs/example-workflow.md) explains installation, connectors, USB roles and the difference between startup and a usable peer.

### Follow the code

List the input streams and start a selected supported format, then follow `onAudioData()`. Speak or tap near the microphone and compare peaks and transferred bytes. A growing byte count with silence may still be valid input; interpret samples using the selected channel count and sample width.

### API use and adaptation

USB audio directions are named from the device/host relationship. Confirm direction, sample rate, channels, bytes per sample and valid bits before interpreting PCM. A frame is one sample for every channel; a byte count is not a frame count. Interleaved stereo alternates channels rather than sending two independent byte streams.

Streaming must be enabled by the host. Track stream state and underflow/overflow statistics separately from successful USB enumeration. Buffers and queues are finite, and an example that ignores a write's returned count does not promise lossless audio. Keep USB callbacks short; perform decoding, storage and expensive analysis elsewhere. Start with a known test signal before adding a codec or external hardware.

### Verify one change at a time

Run the unchanged example first and compare the operation above with its logs or host application. Test the peer absent and then connected; note which stage succeeds instead of expecting the final result immediately. Reset to the original configuration before changing one pin, format or command. The demonstration's error logging is not a general retry or recovery policy.

- [Sketch](EspUsbHostAudioInput.ino)
- [All examples](../../README.md)
- [Example workflow](../../../docs/example-workflow.md)
