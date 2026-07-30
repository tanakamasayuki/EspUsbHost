// Draw on a USB graphics display adapter with LovyanGFX.
//
// The adapter is a DisplayLink DL-1xx device (VID 0x17e9). Everything specific to
// it lives in the headers next to this sketch, so this file only holds the parts
// you would change: what to draw, and at what resolution.
//
// LGFXVirtualCanvas renders the screen as a series of horizontal bands through a
// small sprite, so a Full HD surface needs no full-size frame buffer on the host.
// Its diff transfer then skips bands that did not change, which suits this
// adapter well because it keeps showing its own frame buffer with no USB traffic
// at all.

#include <LovyanGFX.hpp>

#include <LGFXVirtualCanvas.h>

#include "Panel_Dl1xx.hpp"

static EspUsbHost usb;
static dl1xx::Dl1xxDevice adapter(usb);
static dl1xx::LGFX_Dl1xx lcd(&adapter);
static LGFXVirtualScreen screen(lcd);

static bool displayReady = false;
static uint32_t frame = 0;

// Draw in full-screen coordinates. The callback runs once per band; the library
// hides the band offset and clipping.
static void drawScene(LGFXVirtualCanvas &g)
{
    g.fillScreen(TFT_NAVY);

    g.setTextColor(TFT_WHITE);
    g.setTextSize(4);
    g.drawString("EspUsbHost + LovyanGFX", 40, 40);
    g.setTextSize(2);
    g.drawString("USB graphics adapter, DL-1xx bulk protocol", 40, 110);

    g.drawRect(20, 20, g.width() - 40, g.height() - 40, TFT_WHITE);

    // Color bars, which the RLE encoder compresses heavily.
    static constexpr uint16_t BARS[] = {TFT_RED,     TFT_ORANGE, TFT_YELLOW, TFT_GREEN,
                                        TFT_CYAN,    TFT_BLUE,   TFT_MAGENTA, TFT_WHITE};
    const int barWidth = (g.width() - 80) / 8;
    for (int i = 0; i < 8; i++)
    {
        g.fillRect(40 + i * barWidth, 200, barWidth, 120, BARS[i]);
    }

    // Something that actually moves, so diff transfer has work to do.
    const int cx = 40 + ((frame * 8) % (g.width() - 160));
    g.fillCircle(cx + 40, g.height() / 2 + 100, 40, TFT_YELLOW);

    g.setTextSize(3);
    g.setCursor(40, g.height() - 80);
    g.printf("frame %lu  %dx%d", static_cast<unsigned long>(frame), g.width(), g.height());
}

static bool openDisplay()
{
    if (!adapter.begin())
    {
        return false;
    }

    // Pick the largest mode the table offers. Use dl1xx::findMode(w, h) to force
    // one, or adapter.readEdid() plus dl1xx::parseEdidPreferredTiming() to follow
    // what the monitor asks for.
    const dl1xx::Timing *mode = dl1xx::selectMode(0);
    if (!mode || !adapter.setMode(*mode))
    {
        return false;
    }

    if (!lcd.init())
    {
        return false;
    }
    lcd.fillScreen(TFT_BLACK);

    // Full HD at 16 bpp is 3840 bytes per row, so the default budget gives a band
    // of a few rows. Raise it for fewer, larger bands.
    screen.setMemoryLimit(32 * 1024);
    if (!screen.begin())
    {
        return false;
    }
    screen.setDiffMode(LGFXVirtualDiffMode::Tile);

    Serial.printf("display ready: %dx%d, mode %s\n", lcd.width(), lcd.height(), mode->name);
    return true;
}

// Once a second, report the frame rate and how much the diff transfer saved. The
// pixel count is what LGFXVirtualCanvas actually sent; the byte count is what
// went over USB after RLE compression.
static void reportRate()
{
    static uint32_t reportedAtMs = 0;
    static uint32_t reportedFrame = 0;
    const uint32_t now = millis();
    if (now - reportedAtMs < 1000)
    {
        return;
    }
    const EspUsbHostVendorWriteStats stats = adapter.stats();
    const uint32_t frames = frame - reportedFrame;
    Serial.printf("%lu fps  pushed %lu/%lu px  usb %lu bytes  errors %lu\n",
                  static_cast<unsigned long>(frames),
                  static_cast<unsigned long>(screen.diffPushedPixels()),
                  static_cast<unsigned long>(screen.diffTotalPixels()),
                  static_cast<unsigned long>(stats.bytes),
                  static_cast<unsigned long>(stats.errors));
    adapter.resetStats();
    reportedAtMs = now;
    reportedFrame = frame;
}

void setup()
{
    Serial.begin(115200);

    usb.onDeviceDisconnected([](const EspUsbHostDeviceInfo &device) {
        if (device.address == adapter.address())
        {
            adapter.end();
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
        if (adapter.findAdapter() != 0 && openDisplay())
        {
            displayReady = true;
        }
        else
        {
            delay(500);
            return;
        }
    }

    // A reopened adapter or a mode change means the screen no longer holds what
    // the diff cache believes, so every band has to be sent again.
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
