// Tuning sweep for a DL-1xx USB graphics adapter driven through LovyanGFX and
// LGFXVirtualCanvas. Each condition renders the same scene for a fixed time and
// reports the frame rate, what the diff transfer skipped and what actually went
// over USB, so the knobs can be compared against each other on real hardware.
//
// Groups:
//   A  tile geometry     -- does reducing the split count help?
//   B  double buffering  -- does overlapping transfer with draw help?
//   C  diff transfer     -- what it saves, and what it costs
//   D  auto clear        -- the per-tile background fill
//   E  draw scope        -- whole screen vs a sprite over the changing part only
//   F  direct to panel   -- no tiling at all, including the full-clear flicker case
//   G  scene content     -- how much the RLE encoder depends on what is drawn
//
// The protocol and device layers come from the example so there is one source of
// truth; tests/ is stripped from release archives while examples/ is not.
#include <LovyanGFX.hpp>

#include <LGFXVirtualCanvas.h>

#include "../../../examples/Vendor/EspUsbHostDisplayDl1xx/Panel_Dl1xx.hpp"

#include <esp_timer.h>

static constexpr uint32_t TEST_TIMEOUT_MS = 60000;
// Long enough to average out scheduling noise, short enough that 19 conditions
// stay under a minute and a half.
static constexpr uint32_t CONDITION_MS = 3000;

static EspUsbHost usb;
static dl1xx::Dl1xxDevice adapter(usb);
static dl1xx::LGFX_Dl1xx lcd(&adapter);
static LGFXVirtualScreen screen(lcd);

// The moving sprite covers the circle plus a margin, so group E can redraw only
// the part of the screen that actually changes.
static constexpr int SPRITE_W = 240;
static constexpr int SPRITE_H = 120;
static LGFXVirtualSprite movingArea(lcd, SPRITE_W, SPRITE_H, 40, 600);

enum Scene : uint8_t
{
    SCENE_SOLID,
    SCENE_GRADIENT,
    SCENE_UI,
    SCENE_NOISE,
};

enum Mode : uint8_t
{
    MODE_SCREEN,   // LGFXVirtualScreen over the whole panel
    MODE_SPRITE,   // LGFXVirtualSprite over the changing area only
    MODE_DIRECT_FULL,
    MODE_DIRECT_INCREMENTAL,
};

struct Condition
{
    const char *id;
    const char *label;
    Mode mode;
    size_t memoryLimit;  // 0 = library default
    bool doubleBuffer;   // always explicit: "auto" would leak between conditions
    bool diff;
    bool autoClear;
    Scene scene;
};

static const Condition CONDITIONS[] = {
    // A: tile geometry, single-buffered so the geometry is the only variable.
    // Fewer, larger tiles mean fewer draw-callback re-runs.
    {"A1", "mem=default", MODE_SCREEN, 0, false, true, true, SCENE_UI},
    {"A2", "mem=32k", MODE_SCREEN, 32 * 1024, false, true, true, SCENE_UI},
    {"A3", "mem=64k", MODE_SCREEN, 64 * 1024, false, true, true, SCENE_UI},
    {"A4", "mem=96k", MODE_SCREEN, 96 * 1024, false, true, true, SCENE_UI},
    {"A5", "mem=128k", MODE_SCREEN, 128 * 1024, false, true, true, SCENE_UI},

    // B: double buffering. 32 KB is used because two 64 KB tile buffers do not
    // fit in internal RAM on an ESP32-S3.
    {"B1", "32k dbuf=off", MODE_SCREEN, 32 * 1024, false, true, true, SCENE_UI},
    {"B2", "32k dbuf=on", MODE_SCREEN, 32 * 1024, true, true, true, SCENE_UI},

    // C: diff transfer.
    {"C1", "diff=on", MODE_SCREEN, 64 * 1024, false, true, true, SCENE_UI},
    {"C2", "diff=off", MODE_SCREEN, 64 * 1024, false, false, true, SCENE_UI},

    // D: the per-tile background clear.
    {"D1", "clear=on", MODE_SCREEN, 64 * 1024, false, true, true, SCENE_UI},
    {"D2", "clear=off", MODE_SCREEN, 64 * 1024, false, true, false, SCENE_UI},

    // E: redraw scope.
    {"E1", "whole screen", MODE_SCREEN, 64 * 1024, false, true, true, SCENE_UI},
    {"E2", "sprite only", MODE_SPRITE, 64 * 1024, false, true, true, SCENE_UI},

    // F: straight to the panel, no tiling and no buffering.
    {"F1", "direct full redraw", MODE_DIRECT_FULL, 0, false, false, true, SCENE_UI},
    {"F2", "direct incremental", MODE_DIRECT_INCREMENTAL, 0, false, false, true, SCENE_UI},

    // G: how much the scene content matters to the encoder.
    {"G1", "scene=solid", MODE_SCREEN, 64 * 1024, false, true, true, SCENE_SOLID},
    {"G2", "scene=gradient", MODE_SCREEN, 64 * 1024, false, true, true, SCENE_GRADIENT},
    {"G3", "scene=ui", MODE_SCREEN, 64 * 1024, false, true, true, SCENE_UI},
    {"G4", "scene=noise", MODE_SCREEN, 64 * 1024, false, true, true, SCENE_NOISE},
};

static constexpr size_t CONDITION_COUNT = sizeof(CONDITIONS) / sizeof(CONDITIONS[0]);

// The draw callbacks must be plain function pointers, so what to draw travels
// through globals rather than a capture.
static Scene scene = SCENE_UI;
static uint32_t frame = 0;
static bool ok = true;
static bool started = false;
static bool finished = false;

static uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b)
{
    return static_cast<uint16_t>(((r & 0xf8) << 8) | ((g & 0xfc) << 3) | (b >> 3));
}

// One row of pseudo-random pixels, the RLE worst case. Drawing it with pushImage
// keeps the draw cost near a memcpy so the condition measures the encoder and the
// bus rather than the drawing primitives.
//
// It is rebuilt once per frame, and it has to be: a row that never changes makes
// every tile hash identical after the first frame, so diff transfer skips the
// whole screen and the condition measures nothing. The seed is derived from the
// frame counter so the content is both incompressible and new every frame.
static constexpr int NOISE_ROW_PIXELS = 1920;
static uint16_t noiseRow[NOISE_ROW_PIXELS];

static void buildNoiseRow(uint32_t seed)
{
    uint32_t state = 0x12345678u ^ (seed * 2654435761u);
    for (int i = 0; i < NOISE_ROW_PIXELS; i++)
    {
        state = state * 1103515245u + 12345u;
        noiseRow[i] = static_cast<uint16_t>((state >> 16) & 0xffff);
    }
}

static void drawMovingCircle(LGFXVirtualCanvas &g, int cx, int cy)
{
    g.fillCircle(cx, cy, 40, TFT_YELLOW);
}

static void drawScene(LGFXVirtualCanvas &g)
{
    switch (scene)
    {
    case SCENE_SOLID:
        g.fillScreen(TFT_NAVY);
        break;

    case SCENE_GRADIENT:
        // Vertical gradient: every row is one color, which the encoder turns into
        // long runs.
        for (int y = 0; y < g.height(); y++)
        {
            g.drawFastHLine(0, y, g.width(), rgb565(y * 255 / g.height(), 64, 255 - y * 255 / g.height()));
        }
        break;

    case SCENE_NOISE:
        for (int y = 0; y < g.height(); y++)
        {
            g.pushImage(0, y, g.width(), 1, noiseRow);
        }
        break;

    case SCENE_UI:
    default:
    {
        g.fillScreen(TFT_NAVY);
        g.drawRect(20, 20, g.width() - 40, g.height() - 40, TFT_WHITE);
        g.setTextColor(TFT_WHITE);
        g.setTextSize(4);
        g.drawString("EspUsbHost + LovyanGFX", 40, 40);
        g.setTextSize(2);
        g.drawString("USB graphics adapter, DL-1xx bulk protocol", 40, 110);

        static const uint16_t BARS[] = {TFT_RED,  TFT_ORANGE, TFT_YELLOW,  TFT_GREEN,
                                        TFT_CYAN, TFT_BLUE,   TFT_MAGENTA, TFT_WHITE};
        const int barWidth = (g.width() - 80) / 8;
        for (int i = 0; i < 8; i++)
        {
            g.fillRect(40 + i * barWidth, 200, barWidth, 120, BARS[i]);
        }

        g.setTextSize(3);
        g.setCursor(40, g.height() - 80);
        g.printf("frame %lu", static_cast<unsigned long>(frame));
        break;
    }
    }

    if (scene == SCENE_UI)
    {
        const int cx = 80 + static_cast<int>((frame * 8) % (g.width() - 240));
        drawMovingCircle(g, cx, 660);
    }
}

// Sprite-local drawing for group E: only the small area around the circle.
static void drawSpriteScene(LGFXVirtualCanvas &g)
{
    g.fillScreen(TFT_NAVY);
    g.fillCircle(g.width() / 2, g.height() / 2, 40, TFT_YELLOW);
}

// Direct-to-panel variants for group F, bypassing LGFXVirtualCanvas entirely.
static void drawDirectFull()
{
    lcd.startWrite();
    lcd.fillScreen(TFT_NAVY);
    lcd.drawRect(20, 20, lcd.width() - 40, lcd.height() - 40, TFT_WHITE);
    lcd.setTextColor(TFT_WHITE);
    lcd.setTextSize(4);
    lcd.drawString("EspUsbHost + LovyanGFX", 40, 40);
    static const uint16_t BARS[] = {TFT_RED,  TFT_ORANGE, TFT_YELLOW,  TFT_GREEN,
                                    TFT_CYAN, TFT_BLUE,   TFT_MAGENTA, TFT_WHITE};
    const int barWidth = (lcd.width() - 80) / 8;
    for (int i = 0; i < 8; i++)
    {
        lcd.fillRect(40 + i * barWidth, 200, barWidth, 120, BARS[i]);
    }
    const int cx = 80 + static_cast<int>((frame * 8) % (lcd.width() - 240));
    lcd.fillCircle(cx, 660, 40, TFT_YELLOW);
    lcd.endWrite();
}

static void drawDirectIncremental()
{
    // Erase only where the circle was, then draw it in its new place.
    static int previousCx = -1;
    const int cx = 80 + static_cast<int>((frame * 8) % (lcd.width() - 240));
    lcd.startWrite();
    if (previousCx >= 0)
    {
        lcd.fillCircle(previousCx, 660, 41, TFT_NAVY);
    }
    lcd.fillCircle(cx, 660, 40, TFT_YELLOW);
    lcd.endWrite();
    previousCx = cx;
}

static void runCondition(const Condition &c)
{
    scene = c.scene;

    LGFXVirtualTiledBase *surface = nullptr;
    if (c.mode == MODE_SCREEN || c.mode == MODE_SPRITE)
    {
        surface = (c.mode == MODE_SCREEN) ? static_cast<LGFXVirtualTiledBase *>(&screen)
                                          : static_cast<LGFXVirtualTiledBase *>(&movingArea);
        if (c.memoryLimit != 0)
        {
            screen.setMemoryLimit(c.memoryLimit);
            movingArea.setMemoryLimit(c.memoryLimit);
        }
        screen.setDiffMode(c.diff ? LGFXVirtualDiffMode::Tile : LGFXVirtualDiffMode::Off);
        movingArea.setDiffMode(c.diff ? LGFXVirtualDiffMode::Tile : LGFXVirtualDiffMode::Off);
        screen.setAutoClear(c.autoClear);
        movingArea.setAutoClear(c.autoClear);
        screen.setDoubleBuffer(c.doubleBuffer);
        movingArea.setDoubleBuffer(c.doubleBuffer);
        if (!screen.begin() || !movingArea.begin())
        {
            // Not a failure of the code under test: the requested tile buffer
            // simply does not fit. Record it, because the memory ceiling is one
            // of the things this sweep is meant to establish.
            Serial.printf("DISPLAY_TUNE_SKIP id=%s label=\"%s\" reason=alloc mem=%u dbuf=%u\n",
                          c.id, c.label, static_cast<unsigned>(c.memoryLimit),
                          c.doubleBuffer ? 1 : 0);
            return;
        }
        // Start from a known screen so the first frame's diff has a baseline.
        screen.invalidate();
        movingArea.invalidate();
    }

    // Repaint the full scene once so every condition starts from the same image.
    if (c.mode == MODE_SPRITE)
    {
        screen.render(drawScene);
    }
    adapter.flush();
    adapter.resetStats();

    uint32_t frames = 0;
    const int64_t startedAt = esp_timer_get_time();
    const int64_t deadline = startedAt + static_cast<int64_t>(CONDITION_MS) * 1000;
    while (esp_timer_get_time() < deadline)
    {
        if (scene == SCENE_NOISE)
        {
            // Per frame, not per band: every band of one frame shares the row, so
            // the encoder still sees the same worst case within a frame.
            buildNoiseRow(frame);
        }
        switch (c.mode)
        {
        case MODE_SCREEN:
            if (!screen.render(drawScene))
            {
                Serial.printf("DISPLAY_TUNE_FAIL id=%s reason=render\n", c.id);
                ok = false;
                return;
            }
            break;
        case MODE_SPRITE:
        {
            const int x = 40 + static_cast<int>((frame * 8) % (lcd.width() - SPRITE_W - 80));
            if (!movingArea.render(drawSpriteScene, x, 600))
            {
                Serial.printf("DISPLAY_TUNE_FAIL id=%s reason=render\n", c.id);
                ok = false;
                return;
            }
            break;
        }
        case MODE_DIRECT_FULL:
            drawDirectFull();
            break;
        case MODE_DIRECT_INCREMENTAL:
            drawDirectIncremental();
            break;
        }
        frame++;
        frames++;
    }
    adapter.flush();
    const int64_t elapsed = esp_timer_get_time() - startedAt;

    const EspUsbHostVendorWriteStats stats = adapter.stats();
    const double seconds = static_cast<double>(elapsed) / 1000000.0;
    const double fps = seconds > 0.0 ? frames / seconds : 0.0;
    const double bytesPerSecond = seconds > 0.0 ? static_cast<double>(stats.bytes) / seconds : 0.0;

    Serial.printf("DISPLAY_TUNE id=%s label=\"%s\" mode=%u tiles=%d tile_h=%d diff=%u clear=%u "
                  "dbuf=%u frames=%lu seconds=%.2f fps=%.2f pushed_px=%lu total_px=%lu "
                  "usb_bytes=%llu usb_bps=%.0f errors=%lu\n",
                  c.id,
                  c.label,
                  static_cast<unsigned>(c.mode),
                  surface ? surface->tileCount() : 0,
                  surface ? surface->tileHeight() : 0,
                  c.diff ? 1 : 0,
                  c.autoClear ? 1 : 0,
                  c.doubleBuffer ? 1 : 0,
                  static_cast<unsigned long>(frames),
                  seconds,
                  fps,
                  static_cast<unsigned long>(surface ? surface->diffPushedPixels() : 0),
                  static_cast<unsigned long>(surface ? surface->diffTotalPixels() : 0),
                  static_cast<unsigned long long>(stats.bytes),
                  bytesPerSecond,
                  static_cast<unsigned long>(stats.errors));

    if (stats.errors != 0)
    {
        ok = false;
    }
}

void setup()
{
    Serial.begin(115200);
    delay(5000);
    Serial.println("usb_display_throughput test start");
    Serial.println("Connect a DL-1xx USB graphics adapter (VID 0x17e9) with a monitor attached.");
    buildNoiseRow(0);

    usb.begin();
}

void loop()
{
    if (finished)
    {
        delay(100);
        return;
    }

    if (!started)
    {
        if (adapter.findAdapter() == 0)
        {
            if (millis() > TEST_TIMEOUT_MS)
            {
                finished = true;
                Serial.println("[FAIL] no DL-1xx adapter (VID 0x17e9) found before the timeout");
            }
            delay(10);
            return;
        }
        delay(500);
        started = true;

        if (!adapter.begin())
        {
            finished = true;
            Serial.println("[FAIL] could not open the adapter");
            return;
        }
        const dl1xx::Timing *mode = dl1xx::selectMode(0);
        if (!mode || !adapter.setMode(*mode) || !lcd.init())
        {
            finished = true;
            Serial.println("[FAIL] could not program a mode");
            return;
        }
        // The bus ceiling is a property of the board (full speed on an ESP32-S3,
        // high speed on an ESP32-P4), so the test picks it from the profile
        // instead of the sketch claiming one number for every target.
        Serial.printf("DISPLAY_TUNE_READY %dx%d mode=%s\n", lcd.width(), lcd.height(), mode->name);
        return;
    }

    for (size_t i = 0; i < CONDITION_COUNT; i++)
    {
        const Condition &c = CONDITIONS[i];
        if (c.mode == MODE_DIRECT_FULL)
        {
            Serial.println("DISPLAY_PROMPT the next condition clears and redraws the whole screen "
                           "with no buffering; expect visible flicker");
        }
        else if (c.mode == MODE_DIRECT_INCREMENTAL)
        {
            Serial.println("DISPLAY_PROMPT the next condition only repaints the moving circle; "
                           "expect no flicker");
        }
        runCondition(c);
    }

    finished = true;
    Serial.println(ok ? "[PASS]" : "[FAIL]");
}
