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

// build_opt.h next to this sketch sets CFG_TUD_VIDEO_STREAMING_EP_BUFSIZE=112,
// which is the isochronous payload size of the streaming endpoint. It cannot be
// a #define here: the value has to reach the library's own translation units,
// and build_opt.h is a compiler-argument file, so it holds the flag alone with
// no comments in it.
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
// allows. Because it is a compiler flag, run this test with --clean after
// changing it. See docs/usb-host-advanced.md.

#include "EspUsbDevice.h"

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
// documents: it is what keeps the endpoint busy.
static void armNextFrame()
{
    if (!camera.streaming())
    {
        return;
    }
    frameNumber++;
    fillFrame();
    if (camera.sendFrame(frameBuffer, FRAME_BYTES))
    {
        framesSent++;
    }
}

void setup()
{
    Serial.begin(115200);
    delay(5000);

    camera.setFormat(EspUsbDeviceVideoFormat::Mjpeg);
    camera.setFrameSize(320, 240);
    camera.setFrameRate(15);

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

    camera.onFrameComplete(armNextFrame);
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
// dwMaxVideoFrameSize a Probe/Commit exchange settles on: for MJPEG, TinyUSB
// recomputes that as width * height * 2 and ignores the descriptor. Comparing
// the two would fail for every compressed format.
static void reportVideoFacts()
{
    Serial.printf("DEVICE_VIDEO format=MJPEG %ux%u fps=%u max_frame=%lu packet=%u bulk=%u\n",
                  camera.width(),
                  camera.height(),
                  camera.frameRate(),
                  static_cast<unsigned long>(camera.maxFrameSize()),
                  EspUsbDeviceVideo::isochronousPacketSize(false),
                  EspUsbDeviceVideo::bulkStreaming() ? 1 : 0);
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
    }
    // The host selecting a streaming alternate is what starts the stream. Prime
    // the first frame here: after that each one is armed from the completion of
    // the last.
    const bool streaming = camera.streaming();
    if (streaming && !streamingReported)
    {
        streamingReported = true;
        Serial.println("DEVICE_VIDEO_STREAMING 1");
        armNextFrame();
    }
    else if (!streaming && streamingReported)
    {
        streamingReported = false;
        Serial.println("DEVICE_VIDEO_STREAMING 0");
    }
    delay(5);
}
