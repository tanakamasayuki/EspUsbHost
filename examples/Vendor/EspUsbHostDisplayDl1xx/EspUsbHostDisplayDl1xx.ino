// en: Render a scene through a compatible DL-1xx USB graphics adapter.
// ja: 対応するDL-1xx USB映像アダプタへ描画する。
// en: Wiring, operation and adaptation notes: README.md.
// ja: 配線・操作・改造時の注意: README.ja.md。


#include <LovyanGFX.hpp>

#include <LGFXVirtualCanvas.h>

#include "Panel_Dl1xx.hpp"

// en: Keep the USB owner and its function objects alive for the whole session.
// ja: USB全体と各機能のオブジェクトを、接続の使用中ずっと残す。
static EspUsbHost usb;
static dl1xx::Dl1xxDevice adapter(usb);
static dl1xx::LGFX_Dl1xx lcd(&adapter);
static LGFXVirtualScreen screen(lcd);

static bool displayReady = false;
static uint32_t frame = 0;

// en: Draw in full-screen coordinates. The callback runs once per band; the library hides the band offset and clipping.
// ja: 描画座標は画面全体で指定する。bandの位置とclippingは描画library側が扱う。
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

    // en: Pick the largest mode the table offers. Use dl1xx::findMode(w, h) to force one, or adapter.readEdid() plus dl1xx::parseEdidPreferredTiming() to follow what the monitor asks for.
    // ja: 表示modeを選ぶ。解像度を固定する場合も機器の対応timingを確認する。
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

    // en: A larger rendering budget can reduce band overhead but consumes more RAM; verify the memory cost before changing it.
    // ja: 描画の予算を増やすとband処理の負担を減らせるがRAMを多く使う。変更前に使用量を確認する。
    screen.setMemoryLimit(32 * 1024);
    if (!screen.begin())
    {
        return false;
    }
    screen.setDiffMode(LGFXVirtualDiffMode::Tile);

    Serial.printf("display ready: %dx%d, mode %s\n", lcd.width(), lcd.height(), mode->name);
    return true;
}

// en: Once a second, report the frame rate and how much the diff transfer saved. The pixel count is what LGFXVirtualCanvas actually sent; the byte count is what went over USB after RLE compression.
// ja: 定期的に描画と転送の量を比較する。描いたpixel数とUSBの実byte数は別に数える。
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
    // en: Serial is the diagnostic path; confirm its board-specific route separately from USB data.
    // ja: Serialは診断用。基板のどの経路から読めるかを、USBデータ接続とは別に確認する。
    Serial.begin(115200);

    // en: Use disconnect notifications to invalidate application state; a reconnect can have a new address.
    // ja: 切断通知を手元の状態の無効化に使う。再接続後のaddressは変わる場合がある。
    usb.onDeviceDisconnected([](const EspUsbHostDeviceInfo &device) {
        if (device.address == adapter.address())
        {
            adapter.end();
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

    // en: A reopened adapter or a mode change means the screen no longer holds what the diff cache believes, so every band has to be sent again.
    // ja: 映像接続を再開始したら差分状態も無効化し、全体を描き直す。
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
