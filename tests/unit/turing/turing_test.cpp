// Host tests for the 3.5-inch USB smart screen protocol layer used by
// examples/Serial/EspUsbHostDisplayTuring. The header under test is pure byte
// formatting with no Arduino / USB dependencies, so it is included directly and
// the production code itself is exercised.

#include "TuringProtocol.hpp"

#include <cstdio>
#include <cstring>
#include <vector>

namespace
{

int failures = 0;

void check(bool condition, const char *what)
{
  if (!condition)
  {
    printf("FAIL: %s\n", what);
    failures++;
  }
}

void checkEqual(unsigned long actual, unsigned long expected, const char *what)
{
  if (actual != expected)
  {
    printf("FAIL: %s (actual=0x%lx %lu, expected=0x%lx %lu)\n", what, actual, actual, expected,
           expected);
    failures++;
  }
}

void checkBytes(const uint8_t *actual, size_t actualLength, const std::vector<uint8_t> &expected,
                const char *what)
{
  if (actualLength != expected.size() || memcmp(actual, expected.data(), expected.size()) != 0)
  {
    printf("FAIL: %s\n  actual  :", what);
    for (size_t i = 0; i < actualLength; i++)
    {
      printf(" %02x", actual[i]);
    }
    printf("\n  expected:");
    for (uint8_t byte : expected)
    {
      printf(" %02x", byte);
    }
    printf("\n");
    failures++;
  }
}

// Independent decoder for the 6-byte packet, written from the field layout
// rather than from encodeCommand(), so a shared bug cannot hide.
struct Decoded
{
  uint16_t x, y, ex, ey;
  uint8_t command;
};

Decoded decodeCommand(const uint8_t *packet)
{
  const uint64_t bits = (static_cast<uint64_t>(packet[0]) << 32) |
                        (static_cast<uint64_t>(packet[1]) << 24) |
                        (static_cast<uint64_t>(packet[2]) << 16) |
                        (static_cast<uint64_t>(packet[3]) << 8) |
                        static_cast<uint64_t>(packet[4]);
  Decoded out;
  out.x = static_cast<uint16_t>((bits >> 30) & 0x3ff);
  out.y = static_cast<uint16_t>((bits >> 20) & 0x3ff);
  out.ex = static_cast<uint16_t>((bits >> 10) & 0x3ff);
  out.ey = static_cast<uint16_t>(bits & 0x3ff);
  out.command = packet[5];
  return out;
}

// The packing is the part with no second source: get a bit position wrong and the
// panel paints somewhere else. Round-tripping every coordinate against the
// independent decoder is what pins it.
void testCommandPacking()
{
  uint8_t packet[turing::COMMAND_BYTES];

  turing::encodeCommand(packet, turing::COMMAND_DISPLAY_BITMAP, 0, 0, 319, 479);
  const Decoded full = decodeCommand(packet);
  checkEqual(full.x, 0, "x of a full-screen rectangle");
  checkEqual(full.y, 0, "y of a full-screen rectangle");
  checkEqual(full.ex, 319, "ex of a full-screen rectangle");
  checkEqual(full.ey, 479, "ey of a full-screen rectangle");
  checkEqual(full.command, turing::COMMAND_DISPLAY_BITMAP, "command byte is last");

  // All-ones in every field: catches a mask that is one bit too wide as well as
  // a field that bleeds into its neighbour.
  turing::encodeCommand(packet, 0xff, 0x3ff, 0x3ff, 0x3ff, 0x3ff);
  checkBytes(packet, sizeof(packet), {0xff, 0xff, 0xff, 0xff, 0xff, 0xff},
             "every coordinate at its maximum fills the packet");

  // One field at a time, so a swapped pair cannot pass.
  for (int field = 0; field < 4; field++)
  {
    const uint16_t values[4] = {static_cast<uint16_t>(field == 0 ? 0x2a9 : 0),
                                static_cast<uint16_t>(field == 1 ? 0x2a9 : 0),
                                static_cast<uint16_t>(field == 2 ? 0x2a9 : 0),
                                static_cast<uint16_t>(field == 3 ? 0x2a9 : 0)};
    turing::encodeCommand(packet, 0, values[0], values[1], values[2], values[3]);
    const Decoded one = decodeCommand(packet);
    const uint16_t back[4] = {one.x, one.y, one.ex, one.ey};
    for (int i = 0; i < 4; i++)
    {
      checkEqual(back[i], values[i], "one coordinate at a time round trips");
    }
  }

  // Exhaustive round trip over the panel's own coordinate space.
  for (uint16_t v = 0; v <= turing::MAX_COORDINATE; v++)
  {
    turing::encodeCommand(packet, turing::COMMAND_DISPLAY_BITMAP, v,
                             static_cast<uint16_t>(turing::MAX_COORDINATE - v), v,
                             static_cast<uint16_t>(turing::MAX_COORDINATE - v));
    const Decoded d = decodeCommand(packet);
    if (d.x != v || d.y != turing::MAX_COORDINATE - v || d.ex != v ||
        d.ey != turing::MAX_COORDINATE - v)
    {
      checkEqual(d.x, v, "exhaustive coordinate round trip");
      break;
    }
  }
}

// The rectangle is inclusive on both ends, so an off-by-one here would paint one
// row or column short and leave the pixel stream out of step with the panel.
void testBitmapHeader()
{
  uint8_t packet[turing::COMMAND_BYTES];

  turing::encodeBitmapHeader(packet, 0, 0, 320, 480);
  Decoded d = decodeCommand(packet);
  checkEqual(d.ex, 319, "a full-width rectangle ends at width - 1");
  checkEqual(d.ey, 479, "a full-height rectangle ends at height - 1");
  checkEqual(d.command, turing::COMMAND_DISPLAY_BITMAP, "bitmap header carries DISPLAY_BITMAP");

  turing::encodeBitmapHeader(packet, 17, 23, 1, 1);
  d = decodeCommand(packet);
  checkEqual(d.x, 17, "single pixel x");
  checkEqual(d.y, 23, "single pixel y");
  checkEqual(d.ex, 17, "a single pixel has ex == x");
  checkEqual(d.ey, 23, "a single pixel has ey == y");
}

void testRectFits()
{
  check(turing::rectFits(0, 0, 320, 480, 320, 480), "the whole panel fits");
  check(turing::rectFits(319, 479, 1, 1, 320, 480), "the last pixel fits");
  check(!turing::rectFits(0, 0, 321, 480, 320, 480), "one column too wide is rejected");
  check(!turing::rectFits(0, 0, 320, 481, 320, 480), "one row too tall is rejected");
  check(!turing::rectFits(320, 0, 1, 1, 320, 480), "a pixel past the right edge is rejected");
  check(!turing::rectFits(0, 0, 0, 10, 320, 480), "a zero-width rectangle is rejected");
  check(!turing::rectFits(0, 0, 10, 0, 320, 480), "a zero-height rectangle is rejected");
  // A rectangle can fit the panel and still overflow the 10-bit packing only if
  // the panel is larger than 1024, which no orientation of this one is; the
  // guard is checked here so it survives a future larger panel.
  check(!turing::rectFits(1000, 0, 100, 1, 2000, 480), "a rectangle past the packing limit is rejected");
}

void testOrientation()
{
  uint8_t packet[turing::ORIENTATION_COMMAND_BYTES];

  size_t length = turing::encodeOrientation(packet, turing::ORIENTATION_PORTRAIT, 320, 480);
  checkEqual(length, turing::ORIENTATION_COMMAND_BYTES, "orientation packet length");
  checkBytes(packet, length, {0x00, 0x00, 0x00, 0x00, 0x00, 121, 100, 0x01, 0x40, 0x01, 0xe0},
             "portrait orientation packet");

  length = turing::encodeOrientation(packet, turing::ORIENTATION_LANDSCAPE, 480, 320);
  checkBytes(packet, length, {0x00, 0x00, 0x00, 0x00, 0x00, 121, 102, 0x01, 0xe0, 0x01, 0x40},
             "landscape orientation packet");

  // The size is big-endian; a swapped pair would still be 11 bytes, so check a
  // value whose halves differ.
  length = turing::encodeOrientation(packet, turing::ORIENTATION_REVERSE_PORTRAIT, 0x1234, 0x5678);
  checkEqual(packet[6], 101, "reverse portrait is orientation + 100");
  checkEqual(packet[7], 0x12, "width high byte first");
  checkEqual(packet[8], 0x34, "width low byte second");
  checkEqual(packet[9], 0x56, "height high byte first");
  checkEqual(packet[10], 0x78, "height low byte second");
}

// Brightness runs backwards on the wire: 0 is brightest. Getting this the wrong
// way round produces a dark panel that still looks like it is working.
void testBrightness()
{
  checkEqual(turing::encodeBrightnessPercent(0), 255, "0 percent is the darkest level");
  checkEqual(turing::encodeBrightnessPercent(100), 0, "100 percent is the brightest level");
  check(turing::encodeBrightnessPercent(50) > turing::encodeBrightnessPercent(75),
        "a higher percentage encodes a lower level");
  checkEqual(turing::encodeBrightnessPercent(200), 0, "out-of-range percentages clamp");

  uint8_t packet[turing::COMMAND_BYTES];
  turing::encodeBrightness(packet, 100);
  const Decoded d = decodeCommand(packet);
  checkEqual(d.command, turing::COMMAND_SET_BRIGHTNESS, "brightness command byte");
  checkEqual(d.x, 0, "the level travels in the x field");
  checkEqual(d.y, 0, "brightness leaves the other coordinates at zero");
}

// The panel takes RGB565 little-endian, which is also LovyanGFX's
// rgb565_nonswapped memory layout; that identity is what removes the byte swap
// from the whole example, so it is pinned here.
void testPixels()
{
  checkEqual(turing::rgb565(255, 0, 0), 0xf800, "pure red");
  checkEqual(turing::rgb565(0, 255, 0), 0x07e0, "pure green");
  checkEqual(turing::rgb565(0, 0, 255), 0x001f, "pure blue");
  checkEqual(turing::rgb565(255, 255, 255), 0xffff, "white");
  checkEqual(turing::rgb565(0, 0, 0), 0x0000, "black");

  uint8_t pixel[2];
  turing::encodePixel(pixel, 0xf800);
  checkBytes(pixel, sizeof(pixel), {0x00, 0xf8}, "red goes out low byte first");
  turing::encodePixel(pixel, 0x001f);
  checkBytes(pixel, sizeof(pixel), {0x1f, 0x00}, "blue goes out low byte first");

  // A row of pixels is exactly two bytes each with no padding or alignment, so a
  // conversion buffer can be handed to USB as it is.
  const uint16_t row[] = {0xf800, 0x07e0, 0x001f};
  uint8_t bytes[sizeof(row)];
  for (size_t i = 0; i < 3; i++)
  {
    turing::encodePixel(bytes + i * 2, row[i]);
  }
  checkBytes(bytes, sizeof(bytes), {0x00, 0xf8, 0xe0, 0x07, 0x1f, 0x00}, "a row packs contiguously");
}

} // namespace

int main()
{
  testCommandPacking();
  testBitmapHeader();
  testRectFits();
  testOrientation();
  testBrightness();
  testPixels();

  if (failures != 0)
  {
    printf("%d check(s) failed\n", failures);
    return 1;
  }
  printf("all 3.5-inch USB smart screen protocol checks passed\n");
  return 0;
}
