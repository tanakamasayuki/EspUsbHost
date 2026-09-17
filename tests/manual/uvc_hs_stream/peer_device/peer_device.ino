// Camera peer for tests/manual/uvc_hs_stream.
//
// Deliberately *not* the peer from tests/peer/usb_video: that one pins its
// isochronous payload to 112 bytes with a build_opt.h, because a full-speed host
// cannot receive more. This one takes the library default, which is 1023 bytes on
// an ESP32-P4, so the measurement exercises the size a real camera would ask for.
//
// The frames carry buffer[i] = i + n, the same pattern the correctness pairing
// uses, so the host can check the bytes it reassembled rather than only counting
// them. Frame length is settable at run time ('1'/'2'/'3') to see how much of the
// throughput is per-frame overhead; the declared maximum covers the largest.

#include "EspUsbDevice.h"

EspUsbDevice device;
EspUsbDeviceVideo camera(device, "EspUsb HS Camera");

static constexpr size_t MAX_FRAME_BYTES = 131072;
static uint8_t frameBuffer[MAX_FRAME_BYTES];
// Cycled automatically while streaming rather than driven from the test. The
// device board's console is a native USB CDC, and opening one resets the board,
// so the measurement deliberately needs no serial connection to this side: the
// host buckets what it receives by frame length and works out the schedule from
// the frames themselves.
static constexpr size_t FRAME_SIZES[] = {8192, 32768, 131072};
static constexpr uint32_t SIZE_HOLD_MS = 4000;
static uint8_t frameSizeIndex = 1;
static size_t frameBytes = FRAME_SIZES[1];
static uint32_t frameSizeChangedMs = 0;
static uint32_t framesSent = 0;
static uint32_t armFailures = 0;
static uint32_t frameNumber = 0;
static bool streamingReported = false;

static void fillFrame()
{
    for (size_t i = 0; i < frameBytes; i++)
    {
        frameBuffer[i] = static_cast<uint8_t>(i + frameNumber);
    }
}

// Arming the next frame from the completion callback is the shape EspUsbDevice
// documents: it is what keeps the endpoint busy. Nothing else belongs in here --
// this runs on the USB device task.
static void armNextFrame()
{
    if (!camera.streaming())
    {
        return;
    }
    frameNumber++;
    fillFrame();
    if (camera.sendFrame(frameBuffer, frameBytes))
    {
        framesSent++;
    }
    else
    {
        armFailures++;
    }
}

void setup()
{
    Serial.begin(115200);
    delay(5000);

    camera.setFormat(EspUsbDeviceVideoFormat::Mjpeg);
    camera.setFrameSize(640, 480);
    camera.setFrameRate(30);
    // Declared maximum, so the host sizes its frame buffer for the largest length
    // this sketch can be asked to send. Frames shorter than this are legal.
    camera.setMaxFrameSize(MAX_FRAME_BYTES);

    EspUsbDeviceConfig config;
    config.vid = 0x303a;
    config.pid = 0x4029;
    config.manufacturer = "EspUsb";
    config.product = "EspUsbDevice UVC HS Camera";
    config.serialNumber = "espusb-uvc-hs";

    if (!device.begin(config))
    {
        // ESP_ERR_NO_MEM here is the device's own transmit FIFO check: an
        // isochronous IN endpoint it cannot serve is refused at begin() rather
        // than enumerating and sending nothing.
        Serial.printf("USB_BEGIN_FAILED %s\n", device.lastErrorName());
        return;
    }

    camera.onFrameComplete(armNextFrame);
    Serial.println("VIDEO_DEVICE_READY");
}

static void reportFacts()
{
    Serial.printf("DEVICE_VIDEO format=MJPEG %ux%u fps=%u max_frame=%lu packet=%u bulk=%u\n",
                  camera.width(),
                  camera.height(),
                  camera.frameRate(),
                  static_cast<unsigned long>(camera.maxFrameSize()),
                  EspUsbDeviceVideo::isochronousPacketSize(true),
                  EspUsbDeviceVideo::bulkStreaming() ? 1 : 0);
    Serial.printf("DEVICE_VIDEO_DESC len=%u staging=%u\n",
                  camera.descriptorLength(true),
                  EspUsbDeviceVideo::payloadBufferSize());
    Serial.printf("DEVICE_VIDEO_FRAME bytes=%u\n", static_cast<unsigned>(frameBytes));
}

void loop()
{
    if (Serial.available() > 0)
    {
        const char command = Serial.read();
        switch (command)
        {
        case 'v':
            reportFacts();
            break;
        case 'q':
            Serial.printf("DEVICE_VIDEO_STATE streaming=%u sent=%lu arm_failures=%lu bytes=%u\n",
                          camera.streaming() ? 1 : 0,
                          static_cast<unsigned long>(framesSent),
                          static_cast<unsigned long>(armFailures),
                          static_cast<unsigned>(frameBytes));
            break;
        case 'r':
            framesSent = 0;
            armFailures = 0;
            Serial.println("DEVICE_VIDEO_RESET");
            break;
        case '1':
        case '2':
        case '3':
            frameSizeIndex = static_cast<uint8_t>(command - '1');
            frameBytes = FRAME_SIZES[frameSizeIndex];
            Serial.printf("DEVICE_VIDEO_FRAME bytes=%u\n", static_cast<unsigned>(frameBytes));
            break;
        default:
            break;
        }
    }

    const bool streaming = camera.streaming();
    if (streaming && !streamingReported)
    {
        streamingReported = true;
        frameSizeIndex = 0;
        frameBytes = FRAME_SIZES[0];
        frameSizeChangedMs = millis();
        Serial.println("DEVICE_VIDEO_STREAMING 1");
        armNextFrame();
    }
    else if (!streaming && streamingReported)
    {
        streamingReported = false;
        Serial.println("DEVICE_VIDEO_STREAMING 0");
    }

    // Step through the frame lengths so one run produces a table. The next frame
    // armed by the completion callback picks the new length up; the one already
    // in flight finishes at the old one, which is why the host buckets by length
    // rather than by time.
    if (streaming &&
        frameSizeIndex + 1 < sizeof(FRAME_SIZES) / sizeof(FRAME_SIZES[0]) &&
        millis() - frameSizeChangedMs >= SIZE_HOLD_MS)
    {
        frameSizeIndex++;
        frameBytes = FRAME_SIZES[frameSizeIndex];
        frameSizeChangedMs = millis();
        Serial.printf("DEVICE_VIDEO_FRAME bytes=%u\n", static_cast<unsigned>(frameBytes));
    }
    delay(2);
}
