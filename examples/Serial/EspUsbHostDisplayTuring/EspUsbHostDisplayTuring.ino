// Draw on a 3.5-inch USB smart screen with LovyanGFX.
//
// The panel is a CDC serial device (VID 0x1a86, PID 0x5722) that takes 6-byte
// commands and raw RGB565 rectangles. Everything specific to it lives in the
// headers next to this sketch, so this file only holds the parts you would
// change: what to draw, and how the panel is set up.
//
// LGFXVirtualCanvas renders the screen as a series of horizontal bands through a
// small sprite, so no full-size frame buffer is needed on the host. Its diff
// transfer then skips bands that did not change, which matters a lot here: the
// protocol has no compression, so every pixel sent costs 2 bytes on a full-speed
// link. A full 320x480 repaint is 300 KB, roughly a third of a second; a typical
// UI update touches a few bands and finishes in milliseconds.

#include <LovyanGFX.hpp>

#include <LGFXVirtualCanvas.h>

#include "Panel_Turing.hpp"

static EspUsbHost usb;
static turing::TuringDevice panel(usb);
static turing::LGFX_Turing lcd(&panel);
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
    g.drawString("+ LovyanGFX on a USB smart screen", 16, 48);

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
    if (!panel.begin())
    {
        return false;
    }

    // The panel rotates itself, so ask it rather than LovyanGFX. Portrait is the
    // native 320x480; ORIENTATION_LANDSCAPE gives 480x320.
    if (!panel.setOrientation(turing::ORIENTATION_PORTRAIT))
    {
        return false;
    }
    panel.screenOn();
    panel.setBrightness(100);

    if (!lcd.init())
    {
        return false;
    }
    lcd.fillScreen(TFT_BLACK);

    // 320 pixels per row is 640 bytes, so this budget gives a band of about 25
    // rows. Raise it for fewer, larger bands.
    screen.setMemoryLimit(16 * 1024);
    if (!screen.begin())
    {
        return false;
    }
    screen.setDiffMode(LGFXVirtualDiffMode::Tile);

    Serial.printf("display ready: %dx%d\n", static_cast<int>(lcd.width()),
                  static_cast<int>(lcd.height()));
    return true;
}

// Once a second, report the frame rate and how much the diff transfer saved. The
// pixel count is what LGFXVirtualCanvas actually sent; the byte count is what
// went over USB.
static void reportRate()
{
    static uint32_t reportedAtMs = 0;
    static uint32_t reportedFrame = 0;
    const uint32_t now = millis();
    if (now - reportedAtMs < 1000)
    {
        return;
    }
    const EspUsbHostSerialWriteStats stats = panel.stats();
    const uint32_t frames = frame - reportedFrame;
    Serial.printf("%lu fps  pushed %lu/%lu px  usb %lu bytes  errors %lu  dropped %lu\n",
                  static_cast<unsigned long>(frames),
                  static_cast<unsigned long>(screen.diffPushedPixels()),
                  static_cast<unsigned long>(screen.diffTotalPixels()),
                  static_cast<unsigned long>(stats.bytes),
                  static_cast<unsigned long>(stats.errors),
                  static_cast<unsigned long>(panel.dropped()));
    panel.resetStats();
    reportedAtMs = now;
    reportedFrame = frame;
}

void setup()
{
    Serial.begin(115200);

    usb.onDeviceDisconnected([](const EspUsbHostDeviceInfo &device) {
        if (device.address == panel.address())
        {
            panel.end();
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
        if (panel.findDisplay() != 0 && openDisplay())
        {
            displayReady = true;
        }
        else
        {
            delay(500);
            return;
        }
    }

    // A dropped write leaves the panel counting pixels for a bitmap it never
    // finished receiving, so everything after it lands in the wrong place.
    // Resynchronise before drawing anything else.
    if (panel.dropped() != 0)
    {
        Serial.printf("resync after %lu dropped write(s)\n",
                      static_cast<unsigned long>(panel.dropped()));
        panel.resync();
    }

    // A reopened panel or an orientation change means the screen no longer holds
    // what the diff cache believes, so every band has to be sent again.
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
