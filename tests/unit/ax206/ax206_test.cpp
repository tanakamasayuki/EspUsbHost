// Host tests for the AX206 protocol layer used by
// examples/Vendor/EspUsbHostDisplayAx206. The header under test is pure byte
// formatting with no Arduino / USB dependencies, so it is included directly and
// the production code itself is exercised.

#include "Ax206Protocol.hpp"

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

// The three command blocks a bus capture of a working host contains, byte for
// byte. Everything else is derived from these, so they are the anchor of the
// whole protocol layer.
const std::vector<uint8_t> REFERENCE_INIT = {0xcd, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00,
                                             0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
const std::vector<uint8_t> REFERENCE_FRAME = {0xcd, 0x00, 0x00, 0x00, 0x00, 0x06, 0x12, 0x00,
                                              0x00, 0x00, 0x00, 0xdf, 0x01, 0x3f, 0x01, 0x00};
const std::vector<uint8_t> REFERENCE_BRIGHTNESS = {0xcd, 0x00, 0x00, 0x00, 0x00, 0x06, 0x01, 0x01,
                                                   0x00, 0x07, 0x00, 0xdf, 0x01, 0x3f, 0x01, 0x00};

void testReferenceCdbs()
{
  uint8_t cdb[ax206::CDB_BYTES];

  ax206::encodeInitCdb(cdb);
  checkBytes(cdb, sizeof(cdb), REFERENCE_INIT, "INIT matches the reference command block");

  // The reference frame command is a full-screen blit on a 480x320 panel.
  ax206::encodeBlitCdb(cdb, 0, 0, ax206::WIDTH, ax206::HEIGHT);
  checkBytes(cdb, sizeof(cdb), REFERENCE_FRAME, "full-screen blit matches the reference command block");

  ax206::encodeBrightnessCdb(cdb, ax206::BRIGHTNESS_MAX);
  checkBytes(cdb, sizeof(cdb), REFERENCE_BRIGHTNESS, "brightness matches the reference command block");

  // Out-of-range levels are clamped rather than written through: the byte is a
  // property value the device has no reason to range-check for us.
  ax206::encodeBrightnessCdb(cdb, 200);
  check(cdb[9] == ax206::BRIGHTNESS_MAX, "brightness above the maximum is clamped");
  ax206::encodeBrightnessCdb(cdb, 0);
  check(cdb[9] == 0, "brightness zero is passed through");
}

// INIT's data phase comes back from the device and carries the panel geometry.
// Reading it is mandatory, so a parse that silently accepted a short reply would
// hide the one failure that puts the endpoint out of step.
void testInitReply()
{
  const uint8_t reply[] = {0xe0, 0x01, 0x40, 0x01, 0xff};
  uint16_t width = 0;
  uint16_t height = 0;
  check(ax206::parseInitReply(reply, sizeof(reply), &width, &height), "INIT reply parses");
  check(width == ax206::WIDTH, "INIT reply reports the panel width");
  check(height == ax206::HEIGHT, "INIT reply reports the panel height");

  check(!ax206::parseInitReply(reply, 3, &width, &height), "a reply too short to hold the geometry is rejected");
  check(!ax206::parseInitReply(nullptr, sizeof(reply), &width, &height), "a missing reply is rejected");
}

// The corners are inclusive, so an off-by-one would leave the pixel count and the
// declared transfer length disagreeing -- which a Bulk-Only Transport device
// hangs on rather than reporting.
void testBlitRectangle()
{
  uint8_t cdb[ax206::CDB_BYTES];

  ax206::encodeBlitCdb(cdb, 40, 40, 160, 80);
  checkEqual(cdb[ax206::COMMAND_INDEX], ax206::COMMAND_BLIT, "blit command byte");
  checkEqual(static_cast<unsigned long>(cdb[7] | (cdb[8] << 8)), 40, "x0");
  checkEqual(static_cast<unsigned long>(cdb[9] | (cdb[10] << 8)), 40, "y0");
  checkEqual(static_cast<unsigned long>(cdb[11] | (cdb[12] << 8)), 199, "x1 is inclusive");
  checkEqual(static_cast<unsigned long>(cdb[13] | (cdb[14] << 8)), 119, "y1 is inclusive");

  ax206::encodeBlitCdb(cdb, 479, 319, 1, 1);
  checkEqual(static_cast<unsigned long>(cdb[11] | (cdb[12] << 8)), 479, "a single pixel has x1 == x0");
  checkEqual(static_cast<unsigned long>(cdb[13] | (cdb[14] << 8)), 319, "a single pixel has y1 == y0");

  checkEqual(ax206::blitDataLength(ax206::WIDTH, ax206::HEIGHT), 307200, "full-screen data length");
  checkEqual(ax206::blitDataLength(1, 1), 2, "one pixel is two bytes");
}

void testRectFits()
{
  check(ax206::rectFits(0, 0, 480, 320, 480, 320), "the whole panel fits");
  check(ax206::rectFits(479, 319, 1, 1, 480, 320), "the last pixel fits");
  check(!ax206::rectFits(0, 0, 481, 320, 480, 320), "one column too wide is rejected");
  check(!ax206::rectFits(0, 0, 480, 321, 480, 320), "one row too tall is rejected");
  check(!ax206::rectFits(480, 0, 1, 1, 480, 320), "a pixel past the right edge is rejected");
  check(!ax206::rectFits(0, 0, 0, 10, 480, 320), "a zero-width rectangle is rejected");
  check(!ax206::rectFits(0, 0, 10, 0, 480, 320), "a zero-height rectangle is rejected");
}

// The CBW is fixed-layout and little-endian; the reference builds it with
// struct.pack("<I", ...) and a literal b"USBC".
void testCbw()
{
  uint8_t cdb[ax206::CDB_BYTES];
  ax206::encodeInitCdb(cdb);
  uint8_t cbw[ax206::CBW_BYTES];
  ax206::encodeCbw(cbw, 0xdeadbeef, ax206::INIT_TRANSFER_LENGTH, cdb);

  std::vector<uint8_t> expected = {'U', 'S', 'B', 'C', 0xef, 0xbe, 0xad, 0xde,
                                   0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10};
  expected.insert(expected.end(), REFERENCE_INIT.begin(), REFERENCE_INIT.end());
  checkBytes(cbw, sizeof(cbw), expected, "INIT command block wrapper");
  checkEqual(sizeof(cbw), 31, "a command block wrapper is 31 bytes");

  ax206::encodeBlitCdb(cdb, 0, 0, ax206::WIDTH, ax206::HEIGHT);
  ax206::encodeCbw(cbw, 1, ax206::blitDataLength(ax206::WIDTH, ax206::HEIGHT), cdb);
  // 307,200 = 0x0004b000, little-endian.
  checkBytes(cbw + 8, 4, {0x00, 0xb0, 0x04, 0x00}, "the declared transfer length is little-endian");
  checkEqual(cbw[12], 0x00, "bmCBWFlags is host to device");
  checkEqual(cbw[13], 0x00, "bCBWLUN is zero");
  checkEqual(cbw[14], 0x10, "bCBWCBLength is the full 16 bytes");
}

// The status wrapper is found by signature rather than offset, which is what lets
// a caller hand over a whole packet.
void testCsw()
{
  uint8_t status = 0xff;
  const uint8_t passed[] = {'U', 'S', 'B', 'S', 0x01, 0x00, 0x00, 0x00,
                            0x00, 0x00, 0x00, 0x00, 0x00};
  check(ax206::parseCsw(passed, sizeof(passed), 1, &status), "a matching status wrapper is found");
  checkEqual(status, ax206::CSW_PASSED, "status byte");

  const uint8_t failedStatus[] = {'U', 'S', 'B', 'S', 0x01, 0x00, 0x00, 0x00,
                                  0x00, 0x00, 0x00, 0x00, 0x02};
  check(ax206::parseCsw(failedStatus, sizeof(failedStatus), 1, &status), "a failing wrapper is still found");
  checkEqual(status, 2, "a non-zero status is reported");

  check(!ax206::parseCsw(passed, sizeof(passed), 2, &status), "a wrapper for another tag is ignored");
  check(!ax206::parseCsw(passed, ax206::CSW_BYTES - 1, 1, &status), "a truncated wrapper is rejected");

  // Leading bytes from an earlier phase must not hide the wrapper.
  uint8_t offset[ax206::CSW_BYTES + 7] = {0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff, 0x99};
  memcpy(offset + 7, passed, sizeof(passed));
  status = 0xff;
  check(ax206::parseCsw(offset, sizeof(offset), 1, &status), "a wrapper after stray bytes is found");
  checkEqual(status, ax206::CSW_PASSED, "status byte after stray bytes");

  const uint8_t nothing[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13};
  check(!ax206::parseCsw(nothing, sizeof(nothing), 1, &status), "a packet with no wrapper is rejected");
}

// Pixels are RGB565 big-endian, which is LovyanGFX's rgb565_2Byte memory layout;
// that identity is what removes the byte swap from the whole example.
void testPixels()
{
  checkEqual(ax206::rgb565(255, 0, 0), 0xf800, "pure red");
  checkEqual(ax206::rgb565(0, 255, 0), 0x07e0, "pure green");
  checkEqual(ax206::rgb565(0, 0, 255), 0x001f, "pure blue");
  checkEqual(ax206::rgb565(255, 255, 255), 0xffff, "white");

  uint8_t pixel[2];
  ax206::encodePixel(pixel, 0xf800);
  checkBytes(pixel, sizeof(pixel), {0xf8, 0x00}, "red goes out high byte first");
  ax206::encodePixel(pixel, 0x001f);
  checkBytes(pixel, sizeof(pixel), {0x00, 0x1f}, "blue goes out high byte first");
}

} // namespace

int main()
{
  testReferenceCdbs();
  testInitReply();
  testBlitRectangle();
  testRectFits();
  testCbw();
  testCsw();
  testPixels();

  if (failures != 0)
  {
    printf("%d check(s) failed\n", failures);
    return 1;
  }
  printf("all AX206 protocol checks passed\n");
  return 0;
}
