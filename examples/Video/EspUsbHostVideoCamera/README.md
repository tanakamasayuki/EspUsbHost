# EspUsbHostVideoCamera

> 日本語版: [README.ja.md](README.ja.md)

Demonstrates the USB Video Class APIs: reading the format/frame pairs a camera advertises, negotiating Probe/Commit, streaming over the isochronous endpoint, and receiving whole frames reassembled from the payload headers.

## Hardware

- ESP32-P4 on its OTG HS port — the default profile. A full-speed host cannot receive isochronous IN packets much above 120 bytes, which is far below what a camera asks for
- A UVC camera whose **configuration descriptor fits in 256 bytes**. An ordinary webcam does not: a Logitech C920's is 1,974 bytes, and Arduino-ESP32 3.3.11 builds its host stack with `CONFIG_USB_HOST_CONTROL_TRANSFER_MAX_SIZE=256`, so it fails to enumerate before any class driver runs. Verified with [`tests/peer/usb_video/peer_device`](../../../tests/peer/usb_video/peer_device/), a camera built with the sibling `EspUsbDevice` library

Both limits are measured in [docs/usb-host-advanced.md](../../../docs/usb-host-advanced.md#53-isochronous-in-on-a-full-speed-port); neither can be changed from a sketch.

## What it does

- Prints every format/frame pair the camera advertises on connect (`getVideoStreams`)
- Selects the largest frame it offers (`espUsbHostSelectVideoStream`)
- Allocates a frame buffer from the format's `dwMaxVideoFrameBufferSize`
- Negotiates Probe/Commit, selects the streaming alternate and arms the transfers (`videoStart`)
- Copies each frame out of the callback and checks its JPEG markers in `loop()`
- Prints the frame rate once per second alongside the counters that say whether the stream is losing data

## Key APIs

- `usb.getVideoStreamCount(address)` — whether the device enumerated as a camera this host can read
- `usb.getVideoStreams(address, streams, max)` — every format/frame pair, flattened out of the two-level Format/Frame descriptor tree
- `espUsbHostSelectVideoStream(streams, count, format, width, height, fps)` — picks one; `0` means no preference for that argument
- `usb.videoStart(stream, fps, address)` — Probe/Commit, alternate selection and transfer arming. Also takes format/size/rate directly
- `usb.onVideoFrame(callback)` — one reassembled frame, called on the USB task
- `usb.videoCommitted(control, address)` — what the camera actually committed to, which is not always what was asked for
- `usb.videoStats(stats, address)` — what was lost; see Notes
- `usb.videoStop(address)` — returns the interface to its idle alternate, which is how the camera is told to stop reserving bandwidth
- `espUsbHostVideoFrameIntervalToFps(interval)` / `espUsbHostVideoFormatName(format)` — UVC counts in 100 ns units and numbers its formats

## Notes

- **The frame callback runs on the USB task, which also resubmits the streaming transfers.** Four transfers of eight packets is 4 ms of queue at high speed, and an isochronous packet arriving with nothing queued is gone — there is no retry. Time spent in the callback comes off the stream *silently*: no stall, no failed packet, just frames with less in them. An earlier version of the throughput measurement verified all 128 KB of each frame inside the callback and reported 3.47 MB/s instead of 7.06, with frames arriving 6 KB short. This sketch does one `memcpy` and returns.
- A frame arriving while the previous one is still being worked on is dropped and counted. A queue of buffers is the next step if the drops matter.
- An ESP32-P4's hardware JPEG decoder (`driver/jpeg_decode.h`) belongs in `loop()` for the same reason. The frame handed over is one contiguous MJPEG image, which is what the decoder takes.
- `videoStart()` cannot be called from a callback: it waits on control transfers the USB task is the one to complete. This sketch sets a flag and starts from `loop()`.
- Naming a format, size or rate the camera does not offer **fails** rather than substituting another, so a frame buffer sized for what was asked for cannot be handed something larger:
  ```cpp
  usb.videoStart(ESP_USB_HOST_VIDEO_FORMAT_MJPEG, 640, 480, 30);
  ```
- `EspUsbHostVideoStats` is the only sign of a stream losing data, because isochronous transfers are never retried. On a good link everything but `frames` and `payloads` stays at zero: `packetErrors` counts packets the controller reported as failed (a steady stream of them means a packet size it cannot receive), `headerErrors` payload headers that would not decode, `payloadErrors` payloads the camera itself flagged bad, and `overflows` frames that ran past the buffer.
- The frame buffer is sized from `dwMaxVideoFrameBufferSize`, the camera's own bound for that format. `width * height * bytes` is wrong for MJPEG.

## Expected Serial output

```
EspUsbHost Video Camera example start
connected: device: address=1 portId=0x01 vid=303a pid=4028 class=0xef(Unknown) speed=high-speed product="EspUsbDevice UVC Camera"
video stream: addr=1 iface=1 ep=0x81 isoc MJPEG 320x240 format=1 frame=1 bpp=0 default=15fps rates=15-15fps step=0 max_frame=19200 payload=112 startable=1
video selected: addr=1 iface=1 format=MJPEG 320x240 fps=15 payload=112 frame_max=19200
video ready: addr=1
video: addr=1 fps=  0 dropped=0 incomplete=0 packet_errors=0 header_errors=0 overflows=0
frames carry no JPEG SOI/EOI markers (an uncompressed format, perhaps)
video: addr=1 fps=105 dropped=0 incomplete=0 packet_errors=0 header_errors=0 overflows=0
video: addr=1 fps=105 dropped=0 incomplete=0 packet_errors=0 header_errors=0 overflows=0
```

Taken on an ESP32-P4 against `tests/peer/usb_video/peer_device`. Three numbers in it are that camera's doing rather than this host's: `payload=112` because its `build_opt.h` pins the isochronous payload for a full-speed pairing, `fps=105` because it sends frames back to back instead of pacing them at the 15 fps it declares, and the missing JPEG markers because its frames are a test pattern rather than an image.
