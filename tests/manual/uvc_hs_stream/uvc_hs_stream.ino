// Host side of the high-speed UVC measurement.
//
// Answers one question the full-speed pairing cannot: whether the isochronous IN
// limit that caps a full-speed host at about 120 bytes per packet applies to the
// ESP32-P4's high-speed port. The peer here asks for 1023-byte packets, so if the
// stream runs at all, it does not.
//
// The second question is what it is worth. Frames carry buffer[i] = i + n, so the
// reassembled bytes are checked rather than only counted, and the frame length is
// varied from the test so per-frame overhead can be separated from the packet
// rate.

#include "EspUsbHost.h"

EspUsbHost usb;

static bool hostStarted = false;
static uint8_t videoAddress = 0;

// Measurement state. Written from the USB client task in the frame callback and
// read from the loop task when a report is asked for; the counters are
// diagnostics, so a torn read costs a wrong digit in one report rather than
// anything the stream depends on.
//
// Complete frames are bucketed by their length, because the peer steps through
// several lengths on its own schedule and nothing here is told when. Bucketing by
// what arrived means neither side has to know the other's timing, and the first
// frame at a new length -- which spans the changeover -- lands in its own bucket
// instead of dragging down the previous one.
struct LengthBucket
{
    uint32_t length = 0;
    uint32_t frames = 0;
    uint32_t good = 0;
    uint64_t bytes = 0;
    uint32_t firstMs = 0;
    uint32_t lastMs = 0;
};
static constexpr size_t MAX_BUCKETS = 8;
// How much of each frame the callback checks; see onFrame().
static constexpr size_t VERIFY_BYTES = 4096;
static LengthBucket buckets[MAX_BUCKETS];
static size_t bucketCount = 0;

static uint32_t framesIncomplete = 0;
static uint32_t framesTotal = 0;
static bool mismatch = false;
static uint32_t mismatchOffset = 0;
static uint32_t mismatchLength = 0;

static void startHost()
{
    if (hostStarted)
    {
        return;
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

static void resetMeasurement()
{
    for (size_t i = 0; i < MAX_BUCKETS; i++)
    {
        buckets[i] = LengthBucket();
    }
    bucketCount = 0;
    framesIncomplete = 0;
    framesTotal = 0;
    mismatch = false;
    mismatchOffset = 0;
    mismatchLength = 0;
}

static LengthBucket *bucketFor(uint32_t length)
{
    for (size_t i = 0; i < bucketCount; i++)
    {
        if (buckets[i].length == length)
        {
            return &buckets[i];
        }
    }
    if (bucketCount >= MAX_BUCKETS)
    {
        return nullptr;
    }
    LengthBucket &bucket = buckets[bucketCount++];
    bucket.length = length;
    return &bucket;
}

// Runs on the USB client task, which is also the task that resubmits the
// streaming transfers, so this stays as short as a byte check can be: an
// isochronous packet missed while it is busy cannot be retried.
static void onFrame(const EspUsbHostVideoFrame &frame)
{
    framesTotal++;
    if (!frame.complete)
    {
        framesIncomplete++;
        return;
    }

    const uint32_t now = millis();
    LengthBucket *bucket = bucketFor(static_cast<uint32_t>(frame.length));
    if (bucket)
    {
        if (bucket->frames == 0)
        {
            bucket->firstMs = now;
        }
        bucket->lastMs = now;
        bucket->frames++;
        bucket->bytes += frame.length;
    }

    // Only a prefix is verified, and that is a measurement decision rather than a
    // shortcut. This runs on the USB client task, which is also the task that
    // resubmits the streaming transfers, and four transfers of eight packets is
    // 4 ms of queue at high speed. Checking a whole 128 KB frame here took longer
    // than that and silently lost the microframes that arrived while it ran: the
    // 128 KB row came out at half the throughput of the 32 KB one, with frames
    // arriving 6 KB short. The bucket table catches that anyway -- a short frame
    // lands in a bucket of its own -- so the prefix check is what stays, and the
    // full byte-for-byte check lives in tests/peer/usb_video where throughput is
    // not being measured.
    const size_t verifyBytes = frame.length < VERIFY_BYTES ? frame.length : VERIFY_BYTES;
    const uint8_t seed = frame.data[0];
    for (size_t i = 0; i < verifyBytes; i++)
    {
        if (frame.data[i] != static_cast<uint8_t>(i + seed))
        {
            if (!mismatch)
            {
                mismatch = true;
                mismatchOffset = static_cast<uint32_t>(i);
                mismatchLength = static_cast<uint32_t>(frame.length);
            }
            return;
        }
    }
    if (bucket)
    {
        bucket->good++;
    }
}

static void report()
{
    EspUsbHostVideoStats stats;
    usb.videoStats(stats, videoAddress);

    for (size_t i = 0; i < bucketCount; i++)
    {
        const LengthBucket &bucket = buckets[i];
        // The window is first-to-last frame in this bucket rather than wall
        // clock, so the changeover between lengths is not charged to either. A
        // bucket with one frame cannot be timed, hence the guard.
        const uint32_t elapsedMs = bucket.lastMs > bucket.firstMs ? bucket.lastMs - bucket.firstMs : 0;
        const double seconds = elapsedMs / 1000.0;
        const double mbps = seconds > 0 ? (static_cast<double>(bucket.bytes) / 1000000.0) / seconds : 0.0;
        const double fps = seconds > 0 ? (bucket.frames - 1) / seconds : 0.0;
        Serial.printf("UVC_HS_ROW frame_len=%lu frames=%lu good=%lu bytes=%llu elapsed_ms=%lu "
                      "mbps=%.3f fps=%.2f\n",
                      static_cast<unsigned long>(bucket.length),
                      static_cast<unsigned long>(bucket.frames),
                      static_cast<unsigned long>(bucket.good),
                      static_cast<unsigned long long>(bucket.bytes),
                      static_cast<unsigned long>(elapsedMs),
                      mbps,
                      fps);
    }

    Serial.printf("UVC_HS frames=%lu incomplete=%lu lengths=%u mismatch=%u at=%lu len=%lu\n",
                  static_cast<unsigned long>(framesTotal),
                  static_cast<unsigned long>(framesIncomplete),
                  static_cast<unsigned>(bucketCount),
                  mismatch ? 1u : 0u,
                  static_cast<unsigned long>(mismatchOffset),
                  static_cast<unsigned long>(mismatchLength));
    Serial.printf("UVC_HS_STATS payloads=%lu header_errors=%lu payload_errors=%lu "
                  "packet_errors=%lu overflows=%lu\n",
                  static_cast<unsigned long>(stats.payloads),
                  static_cast<unsigned long>(stats.headerErrors),
                  static_cast<unsigned long>(stats.payloadErrors),
                  static_cast<unsigned long>(stats.packetErrors),
                  static_cast<unsigned long>(stats.overflows));
}

void setup()
{
    Serial.setTxBufferSize(4096);
    Serial.begin(115200);
    delay(500);

    usb.onVideoFrame(onFrame);

    // Latched only for a device that actually has video formats: an ESP32-P4
    // presents its ROM USB-Serial/JTAG unit on the same connector until the peer
    // sketch calls device.begin().
    usb.onDeviceConnected([](const EspUsbHostDeviceInfo &device)
                          {
                              const size_t count = usb.getVideoStreamCount(device.address);
                              Serial.printf("DEVICE_CONNECTED addr=%u vid=0x%04x pid=0x%04x streams=%u\n",
                                            device.address, device.vid, device.pid,
                                            static_cast<unsigned>(count));
                              if (count > 0)
                              {
                                  videoAddress = device.address;
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
    if (Serial.available() <= 0)
    {
        delay(2);
        return;
    }
    const char command = Serial.read();
    switch (command)
    {
    case 'Q':
        Serial.printf("HOST_STATE %s devices=%u\n",
                      hostStarted ? "running" : "idle",
                      static_cast<unsigned>(usb.deviceCount()));
        break;
    case 'G':
        startHost();
        break;
    case 'H':
        stopHost();
        break;
    case 'v':
        Serial.printf("VIDEO_DEVICE addr=%u streams=%u\n",
                      videoAddress,
                      static_cast<unsigned>(usb.getVideoStreamCount(videoAddress)));
        break;
    case 'd':
    {
        EspUsbHostVideoStreamInfo streams[ESP_USB_HOST_MAX_VIDEO_STREAMS];
        const size_t count = usb.getVideoStreams(videoAddress, streams, ESP_USB_HOST_MAX_VIDEO_STREAMS);
        for (size_t i = 0; i < count; i++)
        {
            Serial.print("  ");
            espUsbHostPrint(streams[i]);
        }
        Serial.printf("VIDEO_STREAM_COUNT %u\n", static_cast<unsigned>(count));
        break;
    }
    case 'S':
    {
        resetMeasurement();
        const bool started = usb.videoStart(ESP_USB_HOST_VIDEO_FORMAT_UNKNOWN, 0, 0, 0);
        EspUsbHostVideoProbeControl commit;
        usb.videoCommitted(commit, videoAddress);
        Serial.printf("UVC_HS_START started=%u payload=%lu frame_size=%lu interval=%lu error=%s\n",
                      started ? 1u : 0u,
                      static_cast<unsigned long>(commit.maxPayloadTransferSize),
                      static_cast<unsigned long>(commit.maxVideoFrameSize),
                      static_cast<unsigned long>(commit.frameInterval),
                      usb.lastErrorName());
        break;
    }
    case 'T':
        Serial.printf("UVC_HS_STOP stopped=%u\n", usb.videoStop(videoAddress) ? 1u : 0u);
        break;
    case 'R':
        resetMeasurement();
        Serial.println("UVC_HS_RESET");
        break;
    case 'M':
        report();
        break;
    default:
        break;
    }
}
