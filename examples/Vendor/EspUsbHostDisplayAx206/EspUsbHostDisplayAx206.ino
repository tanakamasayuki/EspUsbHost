// en: Render a scene on a compatible AX206 USB display.
// ja: 対応するAX206 USB画面へ描画する。
// en: Wiring, operation and adaptation notes: README.md.
// ja: 配線・操作・改造時の注意: README.ja.md。


#include <LovyanGFX.hpp>

#include <LGFXVirtualCanvas.h>

#include "Panel_Ax206.hpp"

// en: Keep the USB owner and its function objects alive for the whole session.
// ja: USB全体と各機能のオブジェクトを、接続の使用中ずっと残す。
static EspUsbHost usb;
static ax206::Ax206Device display(usb);
static ax206::LGFX_Ax206 lcd(&display);
static LGFXVirtualScreen screen(lcd);

static bool displayReady = false;
static uint32_t frame = 0;

// en: Draw in full-screen coordinates. The callback runs once per band; the library hides the band offset and clipping.
// ja: 描画座標は画面全体で指定する。bandの位置とclippingは描画library側が扱う。
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
    // en: Serial is the diagnostic path; confirm its board-specific route separately from USB data.
    // ja: Serialは診断用。基板のどの経路から読めるかを、USBデータ接続とは別に確認する。
    Serial.begin(115200);

    // en: Use disconnect notifications to invalidate application state; a reconnect can have a new address.
    // ja: 切断通知を手元の状態の無効化に使う。再接続後のaddressは変わる場合がある。
    usb.onDeviceDisconnected([](const EspUsbHostDeviceInfo &device) {
        if (device.address == display.address())
        {
            display.end();
            displayReady = false;
            Serial.println("display disconnected");
        }
    });

    // en: Start host processing; success does not mean a peripheral has finished enumeration.
    // ja: Hostの処理を開始する。成功しても相手機器の列挙完了を意味しない。
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

    // en: A reopened display starts blank, so let the canvas know its idea of what is on screen is stale.
    // ja: 画面を再開始すると相手の内容が変わるため、差分状態を無効にして全体を送り直す。
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
