// DL-1xx video modes: standard VESA/CEA timings and the register sequence that
// programs them.
//
// Like Dl1xxProtocol.hpp this header is host-compilable and has no Arduino,
// LovyanGFX or USB dependencies.

#pragma once

#include "Dl1xxProtocol.hpp"

namespace dl1xx
{

// Standard display timing. Front porch is implied by the total, so it is not
// stored: hTotal = width + hFrontPorch + hSyncWidth + hBackPorch.
struct Timing
{
  const char *name;
  uint16_t width;
  uint16_t height;
  uint16_t hSyncWidth;
  uint16_t hBackPorch;
  uint16_t hTotal;
  uint16_t vSyncWidth;
  uint16_t vBackPorch;
  uint16_t vTotal;
  uint32_t pixelClockKhz;
};

// VESA DMT / CEA-861 timings. 1920x1080p60 is the Full HD target; the smaller
// modes are for bring-up and for DL-1x0 parts that cannot reach Full HD.
static constexpr Timing MODES[] = {
    // name           w     h    hsw  hbp  htot   vsw  vbp  vtot  clock kHz
    {"800x600@60", 800, 600, 128, 88, 1056, 4, 23, 628, 40000},
    {"1024x768@60", 1024, 768, 136, 160, 1344, 6, 29, 806, 65000},
    {"1280x720@60", 1280, 720, 40, 220, 1650, 5, 20, 750, 74250},
    {"1920x1080@60", 1920, 1080, 44, 148, 2200, 5, 36, 1125, 148500},
};

static constexpr size_t MODE_COUNT = sizeof(MODES) / sizeof(MODES[0]);

// Color depth register values. 24 bpp additionally needs the base8 plane, which
// this example does not use.
static constexpr uint8_t COLOR_DEPTH_16BPP = 0x00;
static constexpr uint8_t COLOR_DEPTH_24BPP = 0x01;

static constexpr uint8_t REG_COLOR_DEPTH = 0x00;
static constexpr uint8_t REG_H_DISPLAY_START = 0x01;
static constexpr uint8_t REG_H_DISPLAY_END = 0x03;
static constexpr uint8_t REG_V_DISPLAY_START = 0x05;
static constexpr uint8_t REG_V_DISPLAY_END = 0x07;
static constexpr uint8_t REG_H_TOTAL = 0x09;
static constexpr uint8_t REG_H_SYNC_START = 0x0b;
static constexpr uint8_t REG_H_SYNC_END = 0x0d;
static constexpr uint8_t REG_H_PIXELS = 0x0f;
static constexpr uint8_t REG_V_TOTAL = 0x11;
static constexpr uint8_t REG_V_SYNC_START = 0x13;
static constexpr uint8_t REG_V_SYNC_END = 0x15;
static constexpr uint8_t REG_V_LINES = 0x17;
static constexpr uint8_t REG_PIXEL_CLOCK = 0x1b;
static constexpr uint8_t REG_BLANK = 0x1f;
static constexpr uint8_t REG_BASE16_ADDRESS = 0x20;
static constexpr uint8_t REG_BASE8_ADDRESS = 0x26;

static constexpr uint8_t BLANK_DISPLAY_ON = 0x00;

// The pixel clock register counts 5 kHz units.
static constexpr uint16_t PIXEL_CLOCK_UNIT_KHZ = 5;

// Horizontal display start is measured from the start of sync.
static constexpr uint16_t hDisplayStart(const Timing &t)
{
  return static_cast<uint16_t>(t.hBackPorch + t.hSyncWidth);
}

static constexpr uint16_t hDisplayEnd(const Timing &t)
{
  return static_cast<uint16_t>(hDisplayStart(t) + t.width);
}

static constexpr uint16_t vDisplayStart(const Timing &t)
{
  return static_cast<uint16_t>(t.vBackPorch + t.vSyncWidth);
}

static constexpr uint16_t vDisplayEnd(const Timing &t)
{
  return static_cast<uint16_t>(vDisplayStart(t) + t.height);
}

// Bytes the base16 plane occupies, which is also where a base8 plane would start.
static constexpr uint32_t base16PlaneBytes(const Timing &t)
{
  return static_cast<uint32_t>(t.width) * static_cast<uint32_t>(t.height) * 2u;
}

// Emits the full mode-set sequence: lock, every timing register, unlock. The
// unlock write is what applies the mode.
//
// Re-sending this exact sequence is also how the display recovers after a
// monitor-side HPD event blanks the output.
static inline bool writeModeSet(CommandBuffer &out, const Timing &timing)
{
  bool ok = out.lockRegisters();

  ok = ok && out.registerWrite(REG_COLOR_DEPTH, COLOR_DEPTH_16BPP);
  ok = ok && out.registerWrite16(REG_H_DISPLAY_START, lfsr16(hDisplayStart(timing)));
  ok = ok && out.registerWrite16(REG_H_DISPLAY_END, lfsr16(hDisplayEnd(timing)));
  ok = ok && out.registerWrite16(REG_V_DISPLAY_START, lfsr16(vDisplayStart(timing)));
  ok = ok && out.registerWrite16(REG_V_DISPLAY_END, lfsr16(vDisplayEnd(timing)));
  ok = ok && out.registerWrite16(REG_H_TOTAL, lfsr16(static_cast<uint16_t>(timing.hTotal - 1)));
  ok = ok && out.registerWrite16(REG_H_SYNC_START, lfsr16(1));
  ok = ok && out.registerWrite16(REG_H_SYNC_END,
                                 lfsr16(static_cast<uint16_t>(timing.hSyncWidth + 1)));
  ok = ok && out.registerWrite16(REG_H_PIXELS, timing.width);
  ok = ok && out.registerWrite16(REG_V_TOTAL, lfsr16(timing.vTotal));
  ok = ok && out.registerWrite16(REG_V_SYNC_START, lfsr16(0));
  ok = ok && out.registerWrite16(REG_V_SYNC_END, lfsr16(timing.vSyncWidth));
  ok = ok && out.registerWrite16(REG_V_LINES, timing.height);
  ok = ok && out.registerWrite16LowFirst(
                 REG_PIXEL_CLOCK,
                 static_cast<uint16_t>(timing.pixelClockKhz / PIXEL_CLOCK_UNIT_KHZ));
  ok = ok && out.registerWrite24(REG_BASE16_ADDRESS, 0);
  ok = ok && out.registerWrite24(REG_BASE8_ADDRESS, base16PlaneBytes(timing));
  ok = ok && out.registerWrite(REG_BLANK, BLANK_DISPLAY_ON);

  ok = ok && out.unlockRegisters();
  return ok;
}

// Every register writeModeSet() programs, in the order it writes them. Keeping
// the list here lets a test assert that none is missed: an omitted timing
// register still transfers fine but leaves the monitor without a valid signal.
static constexpr uint8_t MODE_SET_REGISTERS[] = {
    0xff,                                     // lock
    0x00,                                     // color depth
    0x01, 0x02, 0x03, 0x04,                   // horizontal display start / end
    0x05, 0x06, 0x07, 0x08,                   // vertical display start / end
    0x09, 0x0a,                               // horizontal total - 1
    0x0b, 0x0c, 0x0d, 0x0e,                   // horizontal sync start / end
    0x0f, 0x10,                               // horizontal pixel count
    0x11, 0x12,                               // vertical total
    0x13, 0x14, 0x15, 0x16,                   // vertical sync start / end
    0x17, 0x18,                               // vertical line count
    0x1b, 0x1c,                               // pixel clock
    0x20, 0x21, 0x22,                         // base16 plane address
    0x26, 0x27, 0x28,                         // base8 plane address
    0x1f,                                     // blanking off
    0xff,                                     // unlock (applies the mode)
};

static constexpr size_t MODE_SET_REGISTER_COUNT =
    sizeof(MODE_SET_REGISTERS) / sizeof(MODE_SET_REGISTERS[0]);

// Four bytes per register write.
static constexpr size_t MODE_SET_BYTES = MODE_SET_REGISTER_COUNT * 4;

// Largest mode whose frame buffer stays inside the 24-bit address space and that
// the caller's pixel limit allows. maxPixels of 0 means no limit.
static inline const Timing *selectMode(uint32_t maxPixels)
{
  const Timing *best = nullptr;
  for (size_t i = 0; i < MODE_COUNT; i++)
  {
    const Timing &candidate = MODES[i];
    const uint32_t pixels = static_cast<uint32_t>(candidate.width) * candidate.height;
    if (maxPixels != 0 && pixels > maxPixels)
    {
      continue;
    }
    // The RLE write command carries a 24-bit byte address.
    if (base16PlaneBytes(candidate) > 0x01000000u)
    {
      continue;
    }
    if (!best || pixels > static_cast<uint32_t>(best->width) * best->height)
    {
      best = &candidate;
    }
  }
  return best;
}

static inline const Timing *findMode(uint16_t width, uint16_t height)
{
  for (size_t i = 0; i < MODE_COUNT; i++)
  {
    if (MODES[i].width == width && MODES[i].height == height)
    {
      return &MODES[i];
    }
  }
  return nullptr;
}

} // namespace dl1xx
