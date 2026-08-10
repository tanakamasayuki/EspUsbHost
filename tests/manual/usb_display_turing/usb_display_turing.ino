// Bring-up test for a 3.5-inch USB smart screen (VID 0x1a86 PID 0x5722): open the
// CDC OUT queue, then put recognizable images on the panel. This is where the
// command packing, the pixel byte order and the orientation command are confirmed
// against real hardware, so the output has to be judged by eye.
//
// The protocol and device layers live with the example so there is one source of
// truth; tests/ is stripped from release archives while examples/ is not, so the
// dependency only ever points this way.
#include "../../../examples/Serial/EspUsbHostDisplayTuring/TuringDevice.hpp"

#include <esp_timer.h>

static constexpr uint32_t TEST_TIMEOUT_MS = 60000;
static constexpr uint32_t STEP_HOLD_MS = 2000;

static EspUsbHost usb;
static turing::TuringDevice display(usb);
static bool started = false;
static bool finished = false;
static uint32_t stepAtMs = 0;
static int step = 0;
static bool ok = true;

// One row of RGB565 little-endian pixels, the largest the panel can need.
static uint8_t row[turing::NATIVE_HEIGHT * 2];

static void fail(const char *what)
{
    Serial.printf("DISPLAY_ERROR %s last_error=%d\n", what, usb.lastError());
    ok = false;
}

static void reportStats(const char *what, int64_t startedAt)
{
    const int64_t elapsed = esp_timer_get_time() - startedAt;
    const EspUsbHostSerialWriteStats stats = display.stats();
    const double seconds = static_cast<double>(elapsed) / 1000000.0;
    const double mbps = seconds > 0.0 ? (static_cast<double>(stats.bytes) / seconds) / 1048576.0 : 0.0;
    Serial.printf("DISPLAY_PAINT what=%s elapsed_us=%lld tx_bytes=%llu mbps=%.3f errors=%u "
                  "queue_full=%u\n",
                  what,
                  static_cast<long long>(elapsed),
                  static_cast<unsigned long long>(stats.bytes),
                  mbps,
                  stats.errors,
                  stats.queueFullEvents);
}

// Vertical color bars, one rectangle for the whole screen with the rows streamed
// into it. The protocol has no compression, so this costs a full frame either
// way; sending it as one rectangle is what keeps the header count at one.
static bool paintColorBars()
{
    static const uint16_t COLORS[] = {
        turing::rgb565(255, 255, 255), turing::rgb565(255, 255, 0),
        turing::rgb565(0, 255, 255),   turing::rgb565(0, 255, 0),
        turing::rgb565(255, 0, 255),   turing::rgb565(255, 0, 0),
        turing::rgb565(0, 0, 255),     turing::rgb565(0, 0, 0),
    };
    static constexpr size_t BAR_COUNT = sizeof(COLORS) / sizeof(COLORS[0]);

    const uint16_t width = display.width();
    const uint16_t height = display.height();
    for (uint16_t x = 0; x < width; x++)
    {
        turing::encodePixel(row + x * 2, COLORS[(static_cast<size_t>(x) * BAR_COUNT) / width]);
    }

    if (!display.beginBitmap(0, 0, width, height))
    {
        return false;
    }
    for (uint16_t y = 0; y < height; y++)
    {
        if (!display.writePixelBytes(row, width))
        {
            return false;
        }
    }
    return display.flush();
}

// 1px checkerboard. Every pixel differs from its neighbours, which is where a
// wrong byte order or a dropped byte shows up as a visible seam or a color cast.
static bool paintCheckerboard()
{
    const uint16_t width = display.width();
    const uint16_t height = display.height();
    if (!display.beginBitmap(0, 0, width, height))
    {
        return false;
    }
    for (uint16_t y = 0; y < height; y++)
    {
        for (uint16_t x = 0; x < width; x++)
        {
            turing::encodePixel(row + x * 2,
                                   ((x + y) & 1) ? turing::rgb565(255, 255, 255)
                                                 : turing::rgb565(0, 0, 0));
        }
        if (!display.writePixelBytes(row, width))
        {
            return false;
        }
    }
    return display.flush();
}

// Three primaries as separate rectangles. A wrong red/blue order or a swapped
// byte pair is unmistakable here, unlike on a gradient.
static bool paintPrimaryPatches()
{
    const uint16_t width = display.width();
    const uint16_t height = display.height();
    const uint16_t band = static_cast<uint16_t>(height / 3);
    return display.fillRect(0, 0, width, band, turing::rgb565(255, 0, 0)) &&
           display.fillRect(0, band, width, band, turing::rgb565(0, 255, 0)) &&
           display.fillRect(0, static_cast<uint16_t>(band * 2), width,
                            static_cast<uint16_t>(height - band * 2), turing::rgb565(0, 0, 255)) &&
           display.flush();
}

void setup()
{
    Serial.begin(115200);
    delay(5000);
    Serial.println("usb_display_turing test start");
    Serial.println("Connect a 3.5-inch USB smart screen (VID 0x1a86 PID 0x5722).");

    usb.onDeviceConnected([](const EspUsbHostDeviceInfo &device) {
        Serial.printf("connected address=%u vid=%04x pid=%04x product=\"%s\"\n",
                      device.address, device.vid, device.pid, device.product);
    });
    usb.onDeviceDisconnected([](const EspUsbHostDeviceInfo &device) {
        Serial.printf("disconnected address=%u\n", device.address);
        if (device.address == display.address())
        {
            display.end();
        }
    });

    usb.begin();
}

void loop()
{
    if (finished)
    {
        delay(10);
        return;
    }

    if (!started)
    {
        if (display.findDisplay() == 0)
        {
            if (millis() > TEST_TIMEOUT_MS)
            {
                finished = true;
                Serial.println("[FAIL] no 3.5-inch USB smart screen (1a86:5722) found before the timeout");
            }
            delay(10);
            return;
        }
        // Let enumeration and the CDC line-coding request settle before writing.
        delay(500);
        started = true;
        stepAtMs = millis();

        if (!display.begin())
        {
            finished = true;
            fail("begin");
            Serial.println("[FAIL] could not open the panel");
            return;
        }
        Serial.printf("DISPLAY_OPEN address=%u out_mps=%u queue_ready=%u\n",
                      display.address(),
                      usb.serialOutPacketSize(display.address()),
                      usb.serialWriteQueueReady(display.address()) ? 1 : 0);
        return;
    }

    // Hold each image long enough for the operator to judge it.
    if (millis() - stepAtMs < STEP_HOLD_MS)
    {
        delay(10);
        return;
    }
    stepAtMs = millis();

    switch (step++)
    {
    case 0:
        if (!display.setOrientation(turing::ORIENTATION_PORTRAIT))
        {
            fail("setOrientation portrait");
            break;
        }
        display.screenOn();
        display.setBrightness(100);
        Serial.printf("DISPLAY_ORIENTATION name=portrait %ux%u\n", display.width(), display.height());
        break;
    case 1:
    case 2:
    case 3:
    {
        static const uint16_t FILLS[] = {turing::rgb565(255, 0, 0), turing::rgb565(0, 255, 0),
                                         turing::rgb565(0, 0, 255)};
        static const char *NAMES[] = {"fill_red", "fill_green", "fill_blue"};
        const int index = step - 2;
        display.resetStats();
        const int64_t startedAt = esp_timer_get_time();
        if (!display.fillScreen(FILLS[index]) || !display.flush())
        {
            fail(NAMES[index]);
            break;
        }
        reportStats(NAMES[index], startedAt);
        break;
    }
    case 4:
        display.resetStats();
        {
            const int64_t startedAt = esp_timer_get_time();
            if (!paintPrimaryPatches())
            {
                fail("primary_patches");
                break;
            }
            reportStats("primary_patches", startedAt);
        }
        Serial.println("DISPLAY_PROMPT three horizontal bands: red on top, green, blue at the bottom");
        break;
    case 5:
        display.resetStats();
        {
            const int64_t startedAt = esp_timer_get_time();
            if (!paintColorBars())
            {
                fail("color_bars");
                break;
            }
            reportStats("color_bars", startedAt);
        }
        Serial.println("DISPLAY_PROMPT eight vertical color bars should be visible");
        break;
    case 6:
        display.resetStats();
        {
            const int64_t startedAt = esp_timer_get_time();
            if (!paintCheckerboard())
            {
                fail("checkerboard");
                break;
            }
            reportStats("checkerboard", startedAt);
        }
        Serial.println("DISPLAY_PROMPT a fine 1px checkerboard should be visible");
        break;
    case 7:
        // A partial rectangle is what a diff-transfer canvas sends, so this is
        // the update shape the LovyanGFX example depends on.
        display.resetStats();
        {
            const int64_t startedAt = esp_timer_get_time();
            const uint16_t w = static_cast<uint16_t>(display.width() / 2);
            const uint16_t h = static_cast<uint16_t>(display.height() / 4);
            if (!display.fillRect(static_cast<uint16_t>(display.width() / 4),
                                  static_cast<uint16_t>(display.height() / 2 - h / 2), w, h,
                                  turing::rgb565(255, 255, 0)) ||
                !display.flush())
            {
                fail("partial_rect");
                break;
            }
            reportStats("partial_rect", startedAt);
        }
        Serial.println("DISPLAY_PROMPT one yellow rectangle over the middle of the checkerboard");
        break;
    case 8:
        // Same full screen, painted as 1, 3, 8 and 24 stacked rectangles. The
        // byte count is identical every time, so any difference is the panel's
        // per-rectangle behavior -- which is what decides how a tiled canvas
        // should be banded.
        for (uint16_t bands : {1, 3, 8, 24, 48, 96})
        {
            const uint16_t height = display.height();
            const uint16_t band = static_cast<uint16_t>(height / bands);
            display.resetStats();
            const int64_t startedAt = esp_timer_get_time();
            bool painted = true;
            for (uint16_t i = 0; i < bands && painted; i++)
            {
                const uint16_t y = static_cast<uint16_t>(i * band);
                const uint16_t rows =
                    (i + 1 == bands) ? static_cast<uint16_t>(height - y) : band;
                painted = display.fillRect(0, y, display.width(), rows,
                                           (i & 1) ? turing::rgb565(40, 40, 40)
                                                   : turing::rgb565(200, 200, 200));
            }
            if (!painted || !display.flush())
            {
                fail("split");
                break;
            }
            const int64_t elapsed = esp_timer_get_time() - startedAt;
            const EspUsbHostSerialWriteStats stats = display.stats();
            const double seconds = static_cast<double>(elapsed) / 1000000.0;
            Serial.printf("DISPLAY_SPLIT bands=%u elapsed_us=%lld tx_bytes=%llu mbps=%.3f\n",
                          bands,
                          static_cast<long long>(elapsed),
                          static_cast<unsigned long long>(stats.bytes),
                          seconds > 0.0 ? (static_cast<double>(stats.bytes) / seconds) / 1048576.0
                                        : 0.0);
        }
        break;
    case 9:
        // Persistence: the panel scans out of its own frame buffer, so the image
        // must stay put with no USB traffic at all.
        Serial.println("DISPLAY_PROMPT holding with zero USB traffic for 3 seconds");
        delay(3000);
        Serial.println("DISPLAY_PERSIST the image should be unchanged");
        break;
    case 10:
        display.setBrightness(20);
        Serial.println("DISPLAY_PROMPT the backlight should dim");
        break;
    case 11:
        display.setBrightness(100);
        Serial.println("DISPLAY_PROMPT the backlight should return to full");
        break;
    case 12:
        if (!display.setOrientation(turing::ORIENTATION_LANDSCAPE))
        {
            fail("setOrientation landscape");
            break;
        }
        Serial.printf("DISPLAY_ORIENTATION name=landscape %ux%u\n", display.width(), display.height());
        display.resetStats();
        {
            const int64_t startedAt = esp_timer_get_time();
            if (!paintColorBars())
            {
                fail("color_bars_landscape");
                break;
            }
            reportStats("color_bars_landscape", startedAt);
        }
        Serial.println("DISPLAY_PROMPT the color bars should now run along the long edge");
        break;
    case 13:
        if (!display.setOrientation(turing::ORIENTATION_PORTRAIT))
        {
            fail("setOrientation portrait again");
            break;
        }
        Serial.printf("DISPLAY_ORIENTATION name=portrait %ux%u\n", display.width(), display.height());
        if (!paintColorBars())
        {
            fail("color_bars_portrait");
        }
        break;
    default:
        Serial.printf("DISPLAY_GENERATION %lu underfilled=%lu dropped=%lu\n",
                      static_cast<unsigned long>(display.generation()),
                      static_cast<unsigned long>(display.underfilled()),
                      static_cast<unsigned long>(display.dropped()));
        finished = true;
        Serial.println(ok ? "[PASS]" : "[FAIL]");
        break;
    }
}
