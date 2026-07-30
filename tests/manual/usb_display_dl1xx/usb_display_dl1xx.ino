// Bring-up test for a DL-1xx USB graphics adapter: read EDID, program a mode and
// put recognizable images on the monitor. This is where the register byte order
// and the mode-set derivation are confirmed against real hardware, so the output
// has to be judged by eye.
//
// The protocol and device layers live with the example so there is one source of
// truth; tests/ is stripped from release archives while examples/ is not, so the
// dependency only ever points this way.
#include "../../../examples/Vendor/EspUsbHostDisplayDl1xx/Dl1xxDevice.hpp"

#include <esp_timer.h>

static constexpr uint32_t TEST_TIMEOUT_MS = 60000;
static constexpr uint32_t STEP_HOLD_MS = 2000;

static EspUsbHost usb;
static dl1xx::Dl1xxDevice display(usb);
static bool started = false;
static bool finished = false;
static uint32_t stepAtMs = 0;
static int step = 0;
static bool ok = true;

static uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b)
{
    return static_cast<uint16_t>(((r & 0xf8) << 8) | ((g & 0xfc) << 3) | (b >> 3));
}

static void fail(const char *what)
{
    Serial.printf("DISPLAY_ERROR %s last_error=%d\n", what, usb.lastError());
    ok = false;
}

static void reportStats(const char *what, int64_t startedAt)
{
    const int64_t elapsed = esp_timer_get_time() - startedAt;
    const EspUsbHostVendorWriteStats stats = display.stats();
    const double seconds = static_cast<double>(elapsed) / 1000000.0;
    const double mbps = seconds > 0.0 ? (static_cast<double>(stats.bytes) / seconds) / 1048576.0 : 0.0;
    Serial.printf("DISPLAY_PAINT what=%s elapsed_us=%lld tx_bytes=%llu mbps=%.3f errors=%u\n",
                  what,
                  static_cast<long long>(elapsed),
                  static_cast<unsigned long long>(stats.bytes),
                  mbps,
                  stats.errors);
}

// Vertical color bars. Each row is identical, so the encoder sees long runs and
// the whole screen costs very little on the wire.
static bool paintColorBars()
{
    static const uint16_t COLORS[] = {
        rgb565(255, 255, 255), rgb565(255, 255, 0), rgb565(0, 255, 255), rgb565(0, 255, 0),
        rgb565(255, 0, 255),   rgb565(255, 0, 0),   rgb565(0, 0, 255),   rgb565(0, 0, 0),
    };
    static constexpr size_t BAR_COUNT = sizeof(COLORS) / sizeof(COLORS[0]);

    const uint16_t width = display.width();
    const uint16_t height = display.height();
    if (width == 0 || height == 0)
    {
        return false;
    }

    // One row, reused for every line: the frame buffer is linear, so the whole
    // screen could be one long span, but a row at a time keeps the buffer small.
    static uint16_t row[1920];
    const size_t rowPixels = width <= 1920 ? width : 1920;
    for (size_t x = 0; x < rowPixels; x++)
    {
        row[x] = COLORS[(x * BAR_COUNT) / rowPixels];
    }
    for (uint16_t y = 0; y < height; y++)
    {
        if (!display.writePixels(0, y, row, rowPixels))
        {
            return false;
        }
    }
    return display.flush();
}

// 1px checkerboard: the RLE worst case, so this measures the floor of the
// achievable update rate.
static bool paintCheckerboard()
{
    const uint16_t width = display.width();
    const uint16_t height = display.height();
    static uint16_t row[1920];
    const size_t rowPixels = width <= 1920 ? width : 1920;

    for (uint16_t y = 0; y < height; y++)
    {
        for (size_t x = 0; x < rowPixels; x++)
        {
            row[x] = ((x + y) & 1) ? rgb565(255, 255, 255) : rgb565(0, 0, 0);
        }
        if (!display.writePixels(0, y, row, rowPixels))
        {
            return false;
        }
    }
    return display.flush();
}

static void dumpEdid()
{
    uint8_t edid[dl1xx::EDID_BLOCK_SIZE];
    if (!display.readEdid(edid, sizeof(edid)))
    {
        Serial.println("DISPLAY_EDID read=0");
        return;
    }

    dl1xx::EdidInfo info;
    dl1xx::parseEdid(edid, sizeof(edid), info);
    Serial.printf("DISPLAY_EDID read=1 valid=%u checksum=%u manufacturer=%s product=0x%04x "
                  "version=%u.%u preferred=%ux%u clock_khz=%lu\n",
                  info.valid ? 1 : 0,
                  info.checksumOk ? 1 : 0,
                  info.manufacturer,
                  info.productCode,
                  info.version,
                  info.revision,
                  info.preferredWidth,
                  info.preferredHeight,
                  static_cast<unsigned long>(info.preferredPixelClockKhz));

    for (size_t i = 0; i < sizeof(edid); i += 16)
    {
        Serial.printf("DISPLAY_EDID_RAW %02x:", static_cast<unsigned>(i));
        for (size_t j = 0; j < 16; j++)
        {
            Serial.printf(" %02x", edid[i + j]);
        }
        Serial.println();
    }
}

static void listModes()
{
    for (size_t i = 0; i < dl1xx::MODE_COUNT; i++)
    {
        const dl1xx::Timing &t = dl1xx::MODES[i];
        Serial.printf("DISPLAY_MODE name=%s %ux%u clock_khz=%lu fb_bytes=%lu\n",
                      t.name, t.width, t.height,
                      static_cast<unsigned long>(t.pixelClockKhz),
                      static_cast<unsigned long>(dl1xx::base16PlaneBytes(t)));
    }
}

void setup()
{
    Serial.begin(115200);
    delay(5000);
    Serial.println("usb_display_dl1xx test start");
    Serial.println("Connect a DL-1xx USB graphics adapter (VID 0x17e9) with a monitor attached.");

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
        if (display.findAdapter() == 0)
        {
            if (millis() > TEST_TIMEOUT_MS)
            {
                finished = true;
                Serial.println("[FAIL] no DL-1xx adapter (VID 0x17e9) found before the timeout");
            }
            delay(10);
            return;
        }
        // Let enumeration settle before claiming.
        delay(500);
        started = true;
        stepAtMs = millis();

        if (!display.begin())
        {
            finished = true;
            fail("begin");
            Serial.println("[FAIL] could not open the adapter");
            return;
        }
        Serial.printf("DISPLAY_OPEN address=%u out_ep=0x%02x out_mps=%u\n",
                      display.address(),
                      usb.vendorOutEndpoint(display.address()),
                      usb.vendorOutPacketSize(display.address()));
        listModes();
        dumpEdid();
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
    {
        const dl1xx::Timing *mode = dl1xx::findMode(1920, 1080);
        if (!mode)
        {
            fail("no 1920x1080 mode");
            break;
        }
        display.resetStats();
        const int64_t startedAt = esp_timer_get_time();
        if (!display.setMode(*mode))
        {
            fail("setMode");
            break;
        }
        Serial.printf("DISPLAY_MODE_SET name=%s %ux%u\n", mode->name, mode->width, mode->height);
        reportStats("mode_set", startedAt);
        break;
    }
    case 1:
    case 2:
    case 3:
    {
        static const uint16_t FILLS[] = {rgb565(255, 0, 0), rgb565(0, 255, 0), rgb565(0, 0, 255)};
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
            if (!paintColorBars())
            {
                fail("color_bars");
                break;
            }
            reportStats("color_bars", startedAt);
        }
        Serial.println("DISPLAY_PROMPT eight vertical color bars should be visible");
        break;
    case 5:
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
        Serial.println("DISPLAY_PROMPT a fine 1px checkerboard should be visible (RLE worst case)");
        break;
    case 6:
        // Persistence: the chip scans out of its own frame buffer, so the image
        // must stay put with no USB traffic at all.
        Serial.println("DISPLAY_PROMPT holding with zero USB traffic for 3 seconds");
        delay(3000);
        Serial.println("DISPLAY_PERSIST the image should be unchanged");
        break;
    case 7:
    {
        // A mode resend is the documented recovery from a monitor-side HPD event.
        const int64_t startedAt = esp_timer_get_time();
        display.resetStats();
        if (!display.resendMode())
        {
            fail("resendMode");
            break;
        }
        reportStats("mode_resend", startedAt);
        Serial.println("DISPLAY_PROMPT the checkerboard should still be visible after a mode resend");
        break;
    }
    default:
        finished = true;
        Serial.printf("DISPLAY_GENERATION %lu\n", static_cast<unsigned long>(display.generation()));
        Serial.println(ok ? "[PASS]" : "[FAIL]");
        break;
    }
}
