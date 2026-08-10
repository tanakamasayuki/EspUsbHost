// Wire format of the 3.5-inch USB smart screen ("USB35INCHIPSV2", the revision A
// Turing Smart Screen protocol).
//
// Pure byte formatting: no USB, no LovyanGFX, no Arduino. Everything here is a
// free function over caller-owned buffers, so it can be unit tested on a host
// compiler.
//
// See README.md for the protocol references and the trademark notice.

#pragma once

#include <stddef.h>
#include <stdint.h>

namespace turing
{

// WCH USB-serial bridge in front of the panel controller. The device presents a
// CDC ACM interface, so the protocol is a byte stream over a virtual COM port.
static constexpr uint16_t VID = 0x1a86;
static constexpr uint16_t PID = 0x5722;

// The line coding the reference drivers open the port with. A CDC bridge does
// not gate the USB side on it, but the panel firmware is only known to have been
// used this way, so the example sets it too.
static constexpr uint32_t BAUD = 115200;

// Native panel geometry, portrait. Landscape swaps these.
static constexpr uint16_t NATIVE_WIDTH = 320;
static constexpr uint16_t NATIVE_HEIGHT = 480;

// Every command is one byte at the end of a fixed 6-byte packet whose first five
// bytes carry a rectangle. Commands that take no rectangle send zeros.
enum Command : uint8_t
{
  COMMAND_RESET = 101,          // reboots the panel; it re-enumerates on USB
  COMMAND_CLEAR = 102,          // clears to white
  COMMAND_TO_BLACK = 103,       // clears to black
  COMMAND_SCREEN_OFF = 108,     // backlight off, frame buffer kept
  COMMAND_SCREEN_ON = 109,      // backlight on
  COMMAND_SET_BRIGHTNESS = 110, // level in the x field
  COMMAND_SET_ORIENTATION = 121, // followed by 5 more bytes
  COMMAND_DISPLAY_BITMAP = 197,  // followed by the pixels of the rectangle
};

enum Orientation : uint8_t
{
  ORIENTATION_PORTRAIT = 0,
  ORIENTATION_REVERSE_PORTRAIT = 1,
  ORIENTATION_LANDSCAPE = 2,
  ORIENTATION_REVERSE_LANDSCAPE = 3,
};

static constexpr size_t COMMAND_BYTES = 6;
static constexpr size_t ORIENTATION_COMMAND_BYTES = 11;

// Each coordinate is packed into 10 bits, so the addressable area tops out well
// above this panel's 320x480.
static constexpr uint16_t MAX_COORDINATE = 0x3ff;

// Packs four 10-bit coordinates and the command byte into 6 bytes:
//
//   bit  47..38  x
//        37..28  y
//        27..18  ex
//        17..8   ey
//         7..0   command
//
// The rectangle is inclusive on both ends, so a single pixel is x == ex.
inline void encodeCommand(uint8_t *out,
                          uint8_t command,
                          uint16_t x = 0,
                          uint16_t y = 0,
                          uint16_t ex = 0,
                          uint16_t ey = 0)
{
  out[0] = static_cast<uint8_t>(x >> 2);
  out[1] = static_cast<uint8_t>(((x & 0x03) << 6) | (y >> 4));
  out[2] = static_cast<uint8_t>(((y & 0x0f) << 4) | (ex >> 6));
  out[3] = static_cast<uint8_t>(((ex & 0x3f) << 2) | (ey >> 8));
  out[4] = static_cast<uint8_t>(ey & 0xff);
  out[5] = command;
}

// Rectangle covering w x h pixels at (x, y), the header a DISPLAY_BITMAP payload
// follows. The caller must then send exactly w * h pixels, row by row.
inline void encodeBitmapHeader(uint8_t *out, uint16_t x, uint16_t y, uint16_t w, uint16_t h)
{
  encodeCommand(out,
                COMMAND_DISPLAY_BITMAP,
                x,
                y,
                static_cast<uint16_t>(x + w - 1),
                static_cast<uint16_t>(y + h - 1));
}

// SET_ORIENTATION carries the resulting logical size after the 6-byte packet:
// orientation + 100, then width and height as big-endian 16-bit values. Writes
// ORIENTATION_COMMAND_BYTES bytes and returns that count.
inline size_t encodeOrientation(uint8_t *out, Orientation orientation, uint16_t width, uint16_t height)
{
  encodeCommand(out, COMMAND_SET_ORIENTATION);
  out[6] = static_cast<uint8_t>(static_cast<uint8_t>(orientation) + 100);
  out[7] = static_cast<uint8_t>(width >> 8);
  out[8] = static_cast<uint8_t>(width & 0xff);
  out[9] = static_cast<uint8_t>(height >> 8);
  out[10] = static_cast<uint8_t>(height & 0xff);
  return ORIENTATION_COMMAND_BYTES;
}

// The panel's brightness level runs the other way round: 0 is brightest and 255
// is darkest. Callers think in percent, so invert here.
inline uint8_t encodeBrightnessPercent(uint8_t percent)
{
  const uint32_t clamped = percent > 100 ? 100 : percent;
  return static_cast<uint8_t>(255 - (clamped * 255) / 100);
}

inline void encodeBrightness(uint8_t *out, uint8_t percent)
{
  encodeCommand(out, COMMAND_SET_BRIGHTNESS, encodeBrightnessPercent(percent));
}

// True when the rectangle is non-empty and fits both the packing and the panel.
inline bool rectFits(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t width, uint16_t height)
{
  if (w == 0 || h == 0)
  {
    return false;
  }
  const uint32_t ex = static_cast<uint32_t>(x) + w - 1;
  const uint32_t ey = static_cast<uint32_t>(y) + h - 1;
  return ex < width && ey < height && ex <= MAX_COORDINATE && ey <= MAX_COORDINATE;
}

// Pixels go out as RGB565 little-endian, which is LovyanGFX's rgb565_nonswapped
// memory layout. Converted pixels therefore need no byte swapping anywhere in
// this example; these helpers exist for code that builds colors by hand.
inline uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b)
{
  return static_cast<uint16_t>(((r & 0xf8) << 8) | ((g & 0xfc) << 3) | (b >> 3));
}

inline void encodePixel(uint8_t *out, uint16_t color)
{
  out[0] = static_cast<uint8_t>(color & 0xff);
  out[1] = static_cast<uint8_t>(color >> 8);
}

} // namespace turing
