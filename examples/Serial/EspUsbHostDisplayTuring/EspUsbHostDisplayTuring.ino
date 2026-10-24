// en: Render a scene on a compatible Turing USB serial display.
// ja: 対応するTuring USBシリアル画面へ描画する。
// en: Wiring, operation and adaptation notes: README.md.
// ja: 配線・操作・改造時の注意: README.ja.md。


#include <LovyanGFX.hpp>

#include <LGFXVirtualCanvas.h>

#include "Panel_Turing.hpp"

// en: Keep the USB owner and its function objects alive for the whole session.
// ja: USB全体と各機能のオブジェクトを、接続の使用中ずっと残す。
static EspUsbHost usb;
static turing::TuringDevice panel(usb);
static turing::LGFX_Turing lcd(&panel);
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

    // en: The panel rotates itself, so ask it rather than LovyanGFX. Portrait is the native 320x480; ORIENTATION_LANDSCAPE gives 480x320.
    // ja: 機器側の回転設定を使う。描画libraryだけを回転させる場合と区別する。
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

    // en: The screen memory budget controls rendering band height, not the USB device's protocol.
    // ja: 画面のmemory予算は描画bandの高さを決める。USB機器のprotocolとは別に調整する。
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

// en: Once a second, report the frame rate and how much the diff transfer saved. The pixel count is what LGFXVirtualCanvas actually sent; the byte count is what went over USB.
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
    // en: Serial is the diagnostic path; confirm its board-specific route separately from USB data.
    // ja: Serialは診断用。基板のどの経路から読めるかを、USBデータ接続とは別に確認する。
    Serial.begin(115200);

    // en: Use disconnect notifications to invalidate application state; a reconnect can have a new address.
    // ja: 切断通知を手元の状態の無効化に使う。再接続後のaddressは変わる場合がある。
    usb.onDeviceDisconnected([](const EspUsbHostDeviceInfo &device) {
        if (device.address == panel.address())
        {
            panel.end();
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

    // en: A dropped write leaves the panel counting pixels for a bitmap it never finished receiving, so everything after it lands in the wrong place. Resynchronise before drawing anything else.
    // ja: 途中の画像転送が欠けると相手の受信状態がずれる。次の描画前に同期を取り直す。
    if (panel.dropped() != 0)
    {
        Serial.printf("resync after %lu dropped write(s)\n",
                      static_cast<unsigned long>(panel.dropped()));
        panel.resync();
    }

    // en: A reopened panel or an orientation change means the screen no longer holds what the diff cache believes, so every band has to be sent again.
    // ja: 再開始・方向変更後は差分cacheの前提が変わるため、全体を描き直す。
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
