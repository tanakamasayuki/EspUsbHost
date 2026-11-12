// UVC camera peer for tests/peer/usb_video.
//
// A single MJPEG format at 320x240, 15 fps, which is the smallest thing that is
// still a real camera: one Interface Association, a VideoControl interface with
// a Camera Terminal and an Output Terminal, and a VideoStreaming interface with
// one Format and one Frame descriptor beneath it.
//
// Small matters here. The host's configuration-descriptor read is bounded by the
// core's CONFIG_USB_HOST_CONTROL_TRANSFER_MAX_SIZE, which is 256 bytes up to and
// including arduino-esp32 3.3.x. A camera whose configuration descriptor exceeds
// that never enumerates far enough for the host to see its formats, so this peer
// deliberately advertises one format at one size and one rate.
//
// The peer prints the numbers it built its descriptors from. The test compares
// them against what the host decoded, so the assertions check that the two sides
// agree rather than that the host matched a constant written into the test.

// The streaming endpoint's isochronous payload is set to 112 bytes with
// configure() below. The bulk variant (usb_video_bulk) defines
// PEER_VIDEO_BULK before including this file and streams over bulk instead.
//
// The default (512 bytes at full speed) cannot be received by an ESP32 host on
// arduino-esp32 3.3.11, whose USB host stack is built with
// CONFIG_USB_HOST_HW_BUFFER_BIAS_PERIODIC_OUT and leaves the periodic IN FIFO
// too small. Measured against this pairing: 256 and above are refused outright
// by usb_host_interface_claim() with ESP_ERR_NOT_SUPPORTED; 128 is accepted but
// every packet then comes back USB_TRANSFER_STATUS_ERROR and no image data
// arrives at all; 120 is the largest that works. 112 leaves a margin below it.
//
// That is a property of the host under test, not of the camera, and it is why
// this pairing streams at roughly 110 KB/s rather than at what full speed
// allows. See docs/usb-host-advanced.md.

#ifndef PEER_VIDEO_BULK
#define PEER_VIDEO_BULK 0
#endif

#include "EspUsbDevice.h"
#include <atomic>

EspUsbDevice device;
EspUsbDeviceVideo camera(device, "EspUsb Test Camera");

// Frames carry a pattern the host can verify byte for byte: frame n is
// buffer[i] = i + n, so the first byte names the frame and every byte after it
// follows from that one. A host that dropped a payload, kept a payload header in
// the image data, or joined two frames together produces a mismatch rather than
// a plausible-looking image.
//
// 8192 bytes spans about 16 isochronous payloads at full speed, so a frame is
// assembled from many packets rather than arriving in one.
static constexpr size_t FRAME_BYTES = 8192;
static uint8_t frameBuffer[FRAME_BYTES];
static uint32_t framesSent = 0;
static uint32_t frameNumber = 0;
static bool streamingReported = false;

static void fillFrame()
{
    for (size_t i = 0; i < FRAME_BYTES; i++)
    {
        frameBuffer[i] = static_cast<uint8_t>(i + frameNumber);
    }
}

// Arming the next frame from the completion callback is the shape EspUsbDevice
// documents: it is what keeps the endpoint busy. loop() also arms when nothing
// is in flight, because a frame can end without completing -- a new
// Probe/Commit (the host restarting the stream) cancels the frame in flight,
// and nothing would arm the next one. Both paths go through this guard so the
// buffer is never refilled while the device still borrows it.
static std::atomic<bool> arming{false};

static void armNextFrame()
{
    bool expected = false;
    if (!arming.compare_exchange_strong(expected, true))
    {
        return;
    }
    if (camera.streaming() && !camera.frameInFlight())
    {
        frameNumber++;
        fillFrame();
        if (camera.sendFrame(frameBuffer, FRAME_BYTES))
        {
            framesSent++;
        }
    }
    arming.store(false);
}

void setup()
{
    Serial.begin(115200);
    delay(5000);

    camera.setFormat(EspUsbDeviceVideoFormat::Mjpeg);
    camera.setFrameSize(320, 240);
    camera.setFrameRate(15);

    EspUsbVideoConfig videoConfig;
#if PEER_VIDEO_BULK
    videoConfig.transport = EspUsbVideoTransport::Bulk;
    videoConfig.payloadBytes = 128;
#else
    videoConfig.payloadBytes = 112;
    videoConfig.isochronousPacketBytes = 112;
#endif
    if (camera.configure(videoConfig) != ESP_OK)
    {
        Serial.println("VIDEO_CONFIGURE_FAILED");
        return;
    }
    // Registered while stopped: the device refuses callbacks after begin().
    if (camera.onFrameComplete(armNextFrame) != ESP_OK)
    {
        Serial.println("VIDEO_CALLBACK_FAILED");
        return;
    }

    EspUsbDeviceConfig config;
    config.vid = 0x303a;
    config.pid = 0x4028;
    config.manufacturer = "EspUsb";
    config.product = "EspUsbDevice UVC Camera";
    config.serialNumber = "espusb-uvc";

    if (!device.begin(config))
    {
        Serial.printf("USB_BEGIN_FAILED %s\n", device.lastErrorName());
        return;
    }

    Serial.println("VIDEO_DEVICE_READY");
}

// Everything the host should be able to rediscover from the descriptors.
//
// Answered on demand rather than printed once at boot: the tests run against one
// enumeration, and a line printed only at startup can be read by whichever test
// happens to run first and by no other.
//
// maxFrameSize() here is the descriptor's dwMaxVideoFrameBufferSize, which is
// what the host decodes. It is deliberately not the same number as the
// dwMaxVideoFrameSize a Probe/Commit exchange settles on: for MJPEG, the device
// may recompute that as width * height * 2 and ignores the descriptor. Comparing
// the two would fail for every compressed format.
static void reportVideoFacts()
{
    Serial.printf("DEVICE_VIDEO format=MJPEG %ux%u fps=%u max_frame=%lu packet=%u bulk=%u\n",
                  camera.width(),
                  camera.height(),
                  camera.frameRate(),
                  static_cast<unsigned long>(camera.maxFrameSize()),
                  camera.bulkStreaming() ? 64 : camera.isochronousPacketSize(false),
                  camera.bulkStreaming() ? 1 : 0);
    Serial.printf("DEVICE_VIDEO_DESC len=%u\n", camera.descriptorLength(false));
    Serial.printf("DEVICE_VIDEO_FRAME bytes=%u\n", static_cast<unsigned>(FRAME_BYTES));
}

void loop()
{
    if (Serial.available() > 0)
    {
        const char command = Serial.read();
        if (command == 'v')
        {
            reportVideoFacts();
        }
        else if (command == 'q')
        {
            Serial.printf("DEVICE_VIDEO_STATE streaming=%u sent=%lu\n",
                          camera.streaming() ? 1 : 0,
                          static_cast<unsigned long>(framesSent));
        }
        else if (command == 'r')
        {
            framesSent = 0;
            Serial.println("DEVICE_VIDEO_RESET");
        }
        else if (command == 'X')
        {
            // Detach from the bus. The host sees an ordinary disconnect, which is
            // the only way a test can take the camera away mid-stream.
            device.end();
            streamingReported = false;
            Serial.println("DEVICE_VIDEO_DETACHED");
        }
        else if (command == 'Y')
        {
            EspUsbDeviceConfig config;
            config.vid = 0x303a;
            config.pid = 0x4028;
            config.manufacturer = "EspUsb";
            config.product = "EspUsbDevice UVC Camera";
            config.serialNumber = "espusb-uvc";
            Serial.printf("DEVICE_VIDEO_ATTACHED %u\n", device.begin(config) ? 1 : 0);
        }
    }
    // The host selecting a streaming alternate (or, for bulk, committing) is what
    // starts the stream. The first frame is armed here; after that each one is
    // armed from the completion of the last, and here again if one was cancelled.
    const bool streaming = camera.streaming();
    if (streaming && !streamingReported)
    {
        streamingReported = true;
        Serial.println("DEVICE_VIDEO_STREAMING 1");
    }
    else if (!streaming && streamingReported)
    {
        streamingReported = false;
        Serial.println("DEVICE_VIDEO_STREAMING 0");
    }
    if (streaming)
    {
        armNextFrame();
    }
    delay(5);
}
