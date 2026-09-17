// Host side of the UVC peer test.
//
// Covers what the host can do today with a USB Video device: discover it during
// enumeration and report every format/frame pair it offers. Streaming is not
// part of this yet, so nothing here starts a transfer.
//
// The peer prints the numbers it built its descriptors from, and this sketch
// prints what the host decoded from those descriptors. The test compares the
// two, so a decoding mistake shows up as a disagreement between the two boards
// rather than against a constant copied into the test.

#include "EspUsbHost.h"

EspUsbHost usb;

// The USB host is not started in setup(). The peer board is flashed after this
// sketch has booted, so a host started here observes esptool resetting the peer
// and records the resulting enumerations as errors. The test starts it once the
// peer is in place, and stops it again in the fixture teardown so the board is
// not left hosting USB after the run.
static bool hostStarted = false;
static uint8_t videoAddress = 0;

// Frames the peer sends carry buffer[i] = i + n, so the first byte names the
// frame and every byte after it follows from it. Verifying that here is what
// distinguishes a host that assembled the frame correctly from one that merely
// counted the right number of bytes: a kept payload header, a dropped payload or
// two frames joined together all break the pattern.
static uint32_t framesSeen = 0;
static uint32_t framesGood = 0;
static uint32_t framesIncomplete = 0;
static uint32_t lastFrameLength = 0;
static uint32_t firstMismatchOffset = 0;
static bool haveMismatch = false;

static void checkFramePattern(const EspUsbHostVideoFrame &frame)
{
    framesSeen++;
    lastFrameLength = frame.length;
    if (!frame.complete)
    {
        framesIncomplete++;
        return;
    }
    const uint8_t seed = frame.data[0];
    for (size_t i = 0; i < frame.length; i++)
    {
        if (frame.data[i] != static_cast<uint8_t>(i + seed))
        {
            if (!haveMismatch)
            {
                haveMismatch = true;
                firstMismatchOffset = static_cast<uint32_t>(i);
            }
            return;
        }
    }
    framesGood++;
}

static void startHost()
{
    if (hostStarted)
    {
        return; // idempotent
    }
    if (!usb.begin())
    {
        Serial.printf("usb.begin() failed: %s\n", usb.lastErrorName());
        return;
    }
    hostStarted = true;
}

static void stopHost()
{
    usb.end();
    hostStarted = false;
    videoAddress = 0;
    Serial.println("HOST_STATE idle devices=0");
}

static bool handleLifecycle(char command)
{
    if (command == 'v')
    {
        // Answered whenever it is asked, so a test can wait for the camera by
        // polling this. VIDEO_READY below is printed once per enumeration and can
        // only be read by whichever test happens to see it.
        Serial.printf("VIDEO_DEVICE addr=%u streams=%u\n",
                      videoAddress,
                      static_cast<unsigned>(usb.getVideoStreamCount(videoAddress)));
        return true;
    }
    if (command == 'Q')
    {
        Serial.printf("HOST_STATE %s devices=%u\n",
                      hostStarted ? "running" : "idle",
                      static_cast<unsigned>(usb.deviceCount()));
        return true;
    }
    if (command == 'G')
    {
        startHost();
        return true;
    }
    if (command == 'H')
    {
        stopHost();
        return true;
    }
    return false;
}

static void dumpVideoStreams()
{
    EspUsbHostVideoStreamInfo streams[ESP_USB_HOST_MAX_VIDEO_STREAMS];
    const size_t count = usb.getVideoStreams(videoAddress, streams, ESP_USB_HOST_MAX_VIDEO_STREAMS);
    for (size_t i = 0; i < count; i++)
    {
        const EspUsbHostVideoStreamInfo &stream = streams[i];
        // Both interval forms are printed. A camera uses one or the other: a
        // discrete list, or a min/max/step range. EspUsbDevice expresses its one
        // fixed rate as a range with min == max and step 0, so a test that only
        // looked at the discrete count would see nothing.
        Serial.printf("VIDEO_STREAM iface=%u ep=0x%02x %s format=%s %ux%u formatIndex=%u frameIndex=%u "
                      "fps=%lu rates=%u min=%lu max=%lu step=%lu max_frame=%lu payload=%lu startable=%u\n",
                      stream.interfaceNumber,
                      stream.endpointAddress,
                      stream.isochronous ? "isoc" : "bulk",
                      espUsbHostVideoFormatName(stream.format),
                      stream.width,
                      stream.height,
                      stream.formatIndex,
                      stream.frameIndex,
                      static_cast<unsigned long>(espUsbHostVideoFrameIntervalToFps(stream.frameInterval)),
                      stream.frameIntervalCount,
                      static_cast<unsigned long>(stream.frameIntervalMin),
                      static_cast<unsigned long>(stream.frameIntervalMax),
                      static_cast<unsigned long>(stream.frameIntervalStep),
                      static_cast<unsigned long>(stream.maxVideoFrameBufferSize),
                      static_cast<unsigned long>(stream.maxPayloadSize),
                      stream.startable ? 1 : 0);
        for (uint8_t r = 0; r < stream.frameIntervalCount && r < ESP_USB_HOST_MAX_VIDEO_FRAME_INTERVALS; r++)
        {
            Serial.printf("VIDEO_RATE frameIndex=%u fps=%lu interval=%lu\n",
                          stream.frameIndex,
                          static_cast<unsigned long>(espUsbHostVideoFrameIntervalToFps(stream.frameIntervals[r])),
                          static_cast<unsigned long>(stream.frameIntervals[r]));
        }
    }
    Serial.printf("VIDEO_STREAM_COUNT %u\n", static_cast<unsigned>(count));
}

// Runs the selection helper against whatever the device actually offers, so the
// test can check that a caller's request resolves to the same stream the dump
// above describes -- and that a request the camera cannot serve is refused
// instead of being quietly substituted.
static void selectVideoStream(uint8_t format, uint16_t width, uint16_t height, uint32_t fps, const char *label)
{
    EspUsbHostVideoStreamInfo streams[ESP_USB_HOST_MAX_VIDEO_STREAMS];
    const size_t count = usb.getVideoStreams(videoAddress, streams, ESP_USB_HOST_MAX_VIDEO_STREAMS);
    const EspUsbHostVideoStreamSelection selection =
        espUsbHostSelectVideoStream(streams, count, format, width, height, fps);
    if (!selection)
    {
        Serial.printf("VIDEO_SELECT %s found=0\n", label);
        return;
    }
    const EspUsbHostVideoStreamInfo &stream = streams[selection.index];
    Serial.printf("VIDEO_SELECT %s found=1 format=%s %ux%u frameIndex=%u fps=%lu\n",
                  label,
                  espUsbHostVideoFormatName(stream.format),
                  stream.width,
                  stream.height,
                  stream.frameIndex,
                  static_cast<unsigned long>(espUsbHostVideoFrameIntervalToFps(selection.frameInterval)));
}

void setup()
{
    Serial.setTxBufferSize(4096);
    Serial.begin(115200);
    delay(500);

    // Deliberately does no work beyond the pattern check: this runs on the USB
    // client task, which is also the task that resubmits the streaming transfer,
    // and an isochronous packet missed while it is busy cannot be retried.
    usb.onVideoFrame(checkFramePattern);

    // The address is latched only for a device that actually has video formats.
    // The peer board enumerates twice: an ESP32-S3 presents its ROM
    // USB-Serial-JTAG unit (303a:1001) on the same connector until the sketch
    // calls device.begin(), and latching that one would point every command here
    // at the wrong device.
    usb.onDeviceConnected([](const EspUsbHostDeviceInfo &device)
                          {
                              const size_t count = usb.getVideoStreamCount(device.address);
                              Serial.printf("DEVICE_CONNECTED addr=%u vid=0x%04x pid=0x%04x streams=%u\n",
                                            device.address,
                                            device.vid,
                                            device.pid,
                                            static_cast<unsigned>(count));
                              if (count > 0)
                              {
                                  videoAddress = device.address;
                                  Serial.printf("VIDEO_READY addr=%u streams=%u\n",
                                                device.address,
                                                static_cast<unsigned>(count));
                              }
                          });

    usb.onDeviceDisconnected([](const EspUsbHostDeviceInfo &device)
                             {
                                 if (device.address == videoAddress)
                                 {
                                     videoAddress = 0;
                                 }
                                 Serial.printf("DEVICE_DISCONNECTED addr=%u\n", device.address);
                             });

    Serial.println("HOST_SKETCH_READY");
}

void loop()
{
    // Nothing to pump here: EspUsbHost runs the USB client on its own task.
    if (Serial.available() > 0)
    {
        const char command = Serial.read();
        if (handleLifecycle(command))
        {
            return;
        }
        switch (command)
        {
        case 'd':
            dumpVideoStreams();
            break;
        case 'a':
            // No preference at all: take whatever the camera offers.
            selectVideoStream(ESP_USB_HOST_VIDEO_FORMAT_UNKNOWN, 0, 0, 0, "any");
            break;
        case 'm':
            selectVideoStream(ESP_USB_HOST_VIDEO_FORMAT_MJPEG, 320, 240, 15, "mjpeg-320x240-15");
            break;
        case 'x':
            // A size this camera does not offer must be refused.
            selectVideoStream(ESP_USB_HOST_VIDEO_FORMAT_UNKNOWN, 1920, 1080, 0, "1920x1080");
            break;
        case 'y':
            // The right size at a rate the camera does not list.
            selectVideoStream(ESP_USB_HOST_VIDEO_FORMAT_MJPEG, 320, 240, 60, "mjpeg-320x240-60");
            break;
        case 'z':
            // A format this camera does not offer.
            selectVideoStream(ESP_USB_HOST_VIDEO_FORMAT_YUY2, 0, 0, 0, "yuy2");
            break;
        case 'S':
        {
            framesSeen = 0;
            framesGood = 0;
            framesIncomplete = 0;
            haveMismatch = false;
            firstMismatchOffset = 0;
            const bool started = usb.videoStart(ESP_USB_HOST_VIDEO_FORMAT_UNKNOWN, 0, 0, 0);
            EspUsbHostVideoProbeControl commit;
            const bool have = usb.videoCommitted(commit, videoAddress);
            Serial.printf("VIDEO_START started=%u committed=%u format=%u frame=%u interval=%lu "
                          "frame_size=%lu payload=%lu error=%s\n",
                          started ? 1 : 0,
                          have ? 1 : 0,
                          commit.formatIndex,
                          commit.frameIndex,
                          static_cast<unsigned long>(commit.frameInterval),
                          static_cast<unsigned long>(commit.maxVideoFrameSize),
                          static_cast<unsigned long>(commit.maxPayloadTransferSize),
                          usb.lastErrorName());
            break;
        }
        case 'T':
            Serial.printf("VIDEO_STOP stopped=%u streaming=%u\n",
                          usb.videoStop(videoAddress) ? 1 : 0,
                          usb.videoStreaming(videoAddress) ? 1 : 0);
            break;
        case 'F':
        {
            EspUsbHostVideoStats stats;
            const bool have = usb.videoStats(stats, videoAddress);
            Serial.printf("VIDEO_FRAMES have=%u streaming=%u seen=%lu good=%lu incomplete=%lu "
                          "last_len=%lu mismatch=%u at=%lu\n",
                          have ? 1 : 0,
                          usb.videoStreaming(videoAddress) ? 1 : 0,
                          static_cast<unsigned long>(framesSeen),
                          static_cast<unsigned long>(framesGood),
                          static_cast<unsigned long>(framesIncomplete),
                          static_cast<unsigned long>(lastFrameLength),
                          haveMismatch ? 1 : 0,
                          static_cast<unsigned long>(firstMismatchOffset));
            Serial.printf("VIDEO_STATS frames=%lu incomplete=%lu payloads=%lu header_errors=%lu "
                          "payload_errors=%lu packet_errors=%lu overflows=%lu bytes=%llu\n",
                          static_cast<unsigned long>(stats.frames),
                          static_cast<unsigned long>(stats.framesIncomplete),
                          static_cast<unsigned long>(stats.payloads),
                          static_cast<unsigned long>(stats.headerErrors),
                          static_cast<unsigned long>(stats.payloadErrors),
                          static_cast<unsigned long>(stats.packetErrors),
                          static_cast<unsigned long>(stats.overflows),
                          static_cast<unsigned long long>(stats.bytes));
            break;
        }
        case 'i':
            usb.printDeviceInfo(videoAddress);
            break;
        default:
            break;
        }
    }
}
