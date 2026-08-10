// Draw on an AX206 USB display with LovyanGFX.
//
// The display is a 480x320 USB photo frame (VID 0x1908, PID 0x0102) that speaks
// USB mass-storage Bulk-Only Transport with vendor commands. Everything specific
// to it lives in the headers next to this sketch, so this file only holds the
// parts you would change: what to draw.
//
// LGFXVirtualCanvas renders the screen as a series of horizontal bands through a
// small sprite, so no full-size frame buffer is needed on the host. That suits
// this display exactly: it accepts only whole-screen blits, and the bands arrive
// top to bottom, which is the order the pixels have to go out on the wire. The
// panel streams each band straight to USB, so one rendered screen is one USB
// transaction and neither side ever holds a full frame.
//
// Diff transfer is deliberately not enabled. A frame has to carry all 307,200
// bytes whatever changed, so a skipped band would leave a hole in the stream
// rather than save anything.

#include <LovyanGFX.hpp>

#include <LGFXVirtualCanvas.h>

#include "Panel_Ax206.hpp"

static EspUsbHost usb;
static ax206::Ax206Device display(usb);
static ax206::LGFX_Ax206 lcd(&display);
static LGFXVirtualScreen screen(lcd);

static bool displayReady = false;
static uint32_t frame = 0;

// Draw in full-screen coordinates. The callback runs once per band; the library
// hides the band offset and clipping.
static void drawScene(LGFXVirtualCanvas &g)
{
    g.fillScreen(TFT_NAVY);

    g.setTextColor(TFT_WHITE);
    g.setTextSize(2);
    g.drawString("EspUsbHost", 16, 20);
    g.setTextSize(1);
    g.drawString("+ LovyanGFX on an AX206 USB display", 16, 48);

    g.drawRect(8, 8, g.width() - 16, g.height() - 16, TFT_WHITE);

    static constexpr uint16_t BARS[] = {TFT_RED,  TFT_ORANGE, TFT_YELLOW,  TFT_GREEN,
                                        TFT_CYAN, TFT_BLUE,   TFT_MAGENTA, TFT_WHITE};
    const int barWidth = (g.width() - 32) / 8;
    for (int i = 0; i < 8; i++)
    {
        g.fillRect(16 + i * barWidth, 80, barWidth, 60, BARS[i]);
    }

    // Something that actually moves, so diff transfer has work to do.
    const int cx = 16 + ((frame * 6) % (g.width() - 64));
    g.fillCircle(cx + 24, g.height() / 2 + 40, 24, TFT_YELLOW);

    g.setTextSize(2);
    g.setCursor(16, g.height() - 40);
    g.printf("frame %lu  %dx%d", static_cast<unsigned long>(frame), static_cast<int>(g.width()),
             static_cast<int>(g.height()));
}

static bool openDisplay()
{
    if (!display.begin())
    {
        return false;
    }
    if (!lcd.init())
    {
        return false;
    }
    lcd.fillScreen(TFT_BLACK);

    // 480 pixels per row is 960 bytes, so this budget gives a band of about 17
    // rows. Band size is a RAM trade-off only: the USB cost of a frame is fixed
    // at 307,200 bytes however it is cut up, and at 0.53 s on the wire the
    // drawing time does not show. setUsePsram(true) with a budget of
    // 480 * 320 * 2 would render the screen as a single tile in one pass, which
    // is worth doing where the RAM is there and worth no measurable frame rate
    // here.
    screen.setMemoryLimit(16 * 1024);
    if (!screen.begin())
    {
        return false;
    }

    Serial.printf("display ready: %dx%d (device reports %ux%u)\n", static_cast<int>(lcd.width()),
                  static_cast<int>(lcd.height()), static_cast<unsigned>(display.reportedWidth()),
                  static_cast<unsigned>(display.reportedHeight()));
    return true;
}

// Once a second, report the frame rate and how much the diff transfer saved.
static void reportRate()
{
    static uint32_t reportedAtMs = 0;
    static uint32_t reportedFrame = 0;
    const uint32_t now = millis();
    if (now - reportedAtMs < 1000)
    {
        return;
    }
    const EspUsbHostVendorWriteStats stats = display.stats();
    const uint32_t frames = frame - reportedFrame;
    Serial.printf("%lu fps  usb %lu bytes  errors %lu  failed %lu  underfilled %lu  backward %lu\n",
                  static_cast<unsigned long>(frames),
                  static_cast<unsigned long>(stats.bytes),
                  static_cast<unsigned long>(stats.errors),
                  static_cast<unsigned long>(display.failures()),
                  static_cast<unsigned long>(display.underfilled()),
                  static_cast<unsigned long>(display.backward()));
    display.resetStats();
    reportedAtMs = now;
    reportedFrame = frame;
}

void setup()
{
    Serial.begin(115200);

    usb.onDeviceDisconnected([](const EspUsbHostDeviceInfo &device) {
        if (device.address == display.address())
        {
            display.end();
            displayReady = false;
            Serial.println("display disconnected");
        }
    });

    if (!usb.begin())
    {
        Serial.printf("usb.begin() failed: %s\n", usb.lastErrorName());
    }
}

void loop()
{
    if (!displayReady)
    {
        if (display.findDisplay() != 0 && openDisplay())
        {
            displayReady = true;
        }
        else
        {
            delay(500);
            return;
        }
    }

    // A reopened display starts blank, so let the canvas know its idea of what is
    // on screen is stale.
    if (lcd.panel().invalidated())
    {
        screen.invalidate();
        lcd.panel().acknowledgeInvalidation();
    }

    if (!screen.render(drawScene))
    {
        Serial.println("render failed");
        delay(1000);
        return;
    }
    frame++;
    reportRate();
}
