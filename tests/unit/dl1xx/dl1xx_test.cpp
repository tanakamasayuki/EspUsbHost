// Host tests for the DL-1xx protocol layer used by
// examples/Vendor/EspUsbHostDisplayDl1xx. The headers under test are pure byte
// formatting with no Arduino / USB dependencies, so they are included directly
// and the production code itself is exercised.

#include "Dl1xxModes.hpp"
#include "Dl1xxProtocol.hpp"

#include <cstdio>
#include <cstring>
#include <set>
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
  if (actualLength != expected.size())
  {
    printf("FAIL: %s (length actual=%zu expected=%zu)\n", what, actualLength, expected.size());
    failures++;
    return;
  }
  for (size_t i = 0; i < expected.size(); i++)
  {
    if (actual[i] != expected[i])
    {
      printf("FAIL: %s (byte %zu actual=0x%02x expected=0x%02x)\n", what, i, actual[i], expected[i]);
      failures++;
      return;
    }
  }
}

// ---------------------------------------------------------------------------
// LFSR
// ---------------------------------------------------------------------------

void testLfsr()
{
  // Reference values from an independent implementation of "step a 16-bit LFSR
  // with feedback = bit15 ^ bit4 ^ bit2 ^ bit1, starting from 0xFFFF". Steps 0-5
  // alone do not pin the tap set: moving tap 4 to tap 5 first diverges at step 6.
  struct Reference
  {
    uint16_t steps;
    uint16_t value;
  };
  static const Reference REFERENCE[] = {
      {0, 0xffff}, {1, 0xfffe},  {2, 0xfffc},  {3, 0xfff9},   {4, 0xfff2},
      {5, 0xffe5}, {6, 0xffca},  {7, 0xff94},  {8, 0xff29},   {16, 0x29f2},
      {41, 0x2a55}, {45, 0xa557}, {192, 0xeeca}, {1121, 0xa0ae},
      {1125, 0x0aea}, {2112, 0x1cd5}, {2199, 0xd8f4},
  };
  for (const Reference &r : REFERENCE)
  {
    if (dl1xx::lfsr16(r.steps) != r.value)
    {
      printf("FAIL: lfsr16(%u) actual=0x%04x expected=0x%04x\n", r.steps, dl1xx::lfsr16(r.steps),
             r.value);
      failures++;
    }
  }

  // Walking the whole period through lfsr16() would be quadratic, so step a local
  // copy instead -- but first prove the local copy agrees with the production
  // function over the range the mode table uses, otherwise the period check below
  // would only be testing itself.
  const auto step = [](uint16_t state) {
    const uint16_t feedback = ((state >> 15) ^ (state >> 4) ^ (state >> 2) ^ (state >> 1)) & 1u;
    return static_cast<uint16_t>((state << 1) | feedback);
  };
  uint16_t walked = 0xffff;
  bool agrees = true;
  for (uint16_t i = 0; i <= 2300; i++)
  {
    if (dl1xx::lfsr16(i) != walked)
    {
      printf("FAIL: lfsr16(%u)=0x%04x disagrees with a stepped reference 0x%04x\n", i,
             dl1xx::lfsr16(i), walked);
      failures++;
      agrees = false;
      break;
    }
    walked = step(walked);
  }

  // A maximal-length sequence is the property that pins the tap set: taps
  // 15/5/2/1 for instance only reach a period of 5115.
  if (agrees)
  {
    std::set<uint16_t> seen;
    uint16_t state = 0xffff;
    for (long i = 0; i < 65535; i++)
    {
      check(state != 0, "lfsr never reaches 0");
      if (!seen.insert(state).second)
      {
        printf("FAIL: lfsr repeated a state after %ld steps\n", i);
        failures++;
        break;
      }
      state = step(state);
    }
    checkEqual(seen.size(), 65535, "lfsr period is 65535 (maximal length)");
    checkEqual(state, 0xffff, "lfsr wraps back to the initial state");
  }
}

// ---------------------------------------------------------------------------
// Register writes
// ---------------------------------------------------------------------------

void testRegisterWrites()
{
  uint8_t buffer[64];
  dl1xx::CommandBuffer out(buffer, sizeof(buffer));

  out.registerWrite(0x1f, 0x00);
  checkBytes(buffer, out.length(), {0xaf, 0x20, 0x1f, 0x00}, "registerWrite");

  out.reset();
  out.registerWrite16(0x0f, 0x0780); // 1920, high byte first
  checkBytes(buffer, out.length(), {0xaf, 0x20, 0x0f, 0x07, 0xaf, 0x20, 0x10, 0x80},
             "registerWrite16 is high byte first");

  out.reset();
  out.registerWrite16LowFirst(0x1b, 0x7404); // 29700
  checkBytes(buffer, out.length(), {0xaf, 0x20, 0x1b, 0x04, 0xaf, 0x20, 0x1c, 0x74},
             "registerWrite16LowFirst is low byte first");

  out.reset();
  out.registerWrite24(0x26, 0x3f4800);
  checkBytes(buffer, out.length(),
             {0xaf, 0x20, 0x26, 0x3f, 0xaf, 0x20, 0x27, 0x48, 0xaf, 0x20, 0x28, 0x00},
             "registerWrite24 is most significant byte first");

  out.reset();
  out.lockRegisters();
  out.unlockRegisters();
  checkBytes(buffer, out.length(), {0xaf, 0x20, 0xff, 0x00, 0xaf, 0x20, 0xff, 0xff},
             "lock / unlock");

  out.reset();
  out.flush();
  checkBytes(buffer, out.length(), {0xaf, 0xa0}, "flush");

  out.reset();
  out.pad(3);
  checkBytes(buffer, out.length(), {0xaf, 0xaf, 0xaf}, "pad");
}

// ---------------------------------------------------------------------------
// RLE pixel writes
// ---------------------------------------------------------------------------

// Independent decoder, written from the format description rather than from the
// encoder, so a round trip is a real check. Returns false on a malformed stream.
bool decodeRle(const uint8_t *data, size_t length, uint32_t &address,
               std::vector<uint16_t> &pixels)
{
  if (length < dl1xx::RLE_HEADER_BYTES + 1)
  {
    return false;
  }
  if (data[0] != 0xaf || data[1] != 0x6b)
  {
    return false;
  }
  address = (static_cast<uint32_t>(data[2]) << 16) | (static_cast<uint32_t>(data[3]) << 8) | data[4];
  const size_t declared = data[5] == 0 ? 256 : data[5];

  size_t at = 6;
  bool rawPhase = true;
  pixels.clear();
  while (pixels.size() < declared)
  {
    if (at >= length)
    {
      return false;
    }
    const size_t count = data[at] == 0 ? 256 : data[at];
    at++;
    if (rawPhase)
    {
      if (at + count * 2 > length)
      {
        return false;
      }
      for (size_t i = 0; i < count; i++)
      {
        pixels.push_back(static_cast<uint16_t>((data[at] << 8) | data[at + 1]));
        at += 2;
      }
    }
    else
    {
      if (pixels.empty())
      {
        return false;
      }
      const uint16_t previous = pixels.back();
      for (size_t i = 0; i < count; i++)
      {
        pixels.push_back(previous);
      }
    }
    rawPhase = !rawPhase;
  }
  return pixels.size() == declared && at == length;
}

void testRleSolidRun()
{
  uint8_t buffer[64];
  dl1xx::CommandBuffer out(buffer, sizeof(buffer));

  std::vector<uint16_t> red(256, 0xf800);
  const size_t consumed = out.writePixelsRle(0, red.data(), red.size());
  checkEqual(consumed, 256, "a solid run consumes all 256 pixels");
  // The documented example: 256 red pixels at address 0 in a 10-byte command.
  checkBytes(buffer, out.length(), {0xaf, 0x6b, 0x00, 0x00, 0x00, 0x00, 0x01, 0xf8, 0x00, 0xff},
             "solid 256-pixel command matches the documented 10-byte form");

  // Address 0 is symmetric, so it cannot reveal a byte-order error. Use the last
  // Full HD pixel address instead.
  out.reset();
  out.writePixelsRle(0x3f47fe, red.data(), red.size());
  checkBytes(buffer, out.length(), {0xaf, 0x6b, 0x3f, 0x47, 0xfe, 0x00, 0x01, 0xf8, 0x00, 0xff},
             "the 24-bit address goes out most significant byte first");
}

void testRleWorstCase()
{
  uint8_t buffer[1024];
  dl1xx::CommandBuffer out(buffer, sizeof(buffer));

  std::vector<uint16_t> unique;
  for (size_t i = 0; i < 256; i++)
  {
    unique.push_back(static_cast<uint16_t>(i * 257 + 1));
  }
  const size_t consumed = out.writePixelsRle(0, unique.data(), unique.size());
  checkEqual(consumed, 256, "all-different pixels still consume 256");
  checkEqual(out.length(), 519, "worst case is 519 bytes for 256 pixels");
  checkEqual(out.length(), dl1xx::maxRleCommandBytes(256), "worst case matches maxRleCommandBytes");
  checkEqual(buffer[5], 0, "a count of 256 is encoded as 0");
  checkEqual(buffer[6], 0, "a raw run of 256 is encoded as 0");
}

void testRleRoundTrip()
{
  // Deterministic pseudo-random patterns with varying run lengths.
  uint32_t seed = 12345;
  const auto next = [&seed]() {
    seed = seed * 1103515245u + 12345u;
    return (seed >> 16) & 0x7fff;
  };

  for (int pattern = 0; pattern < 200; pattern++)
  {
    std::vector<uint16_t> source;
    const size_t target = 1 + (next() % 900);
    while (source.size() < target)
    {
      const uint16_t value = static_cast<uint16_t>(next() & 0xffff);
      // Mix single pixels with runs so both encoder phases are exercised, and
      // include runs longer than the 255-extras limit.
      size_t run = 1;
      const uint32_t roll = next() % 100;
      if (roll < 40)
      {
        run = 1;
      }
      else if (roll < 80)
      {
        run = 2 + (next() % 8);
      }
      else
      {
        run = 200 + (next() % 400);
      }
      for (size_t i = 0; i < run && source.size() < target; i++)
      {
        source.push_back(value);
      }
    }

    // Encode the whole surface as a chain of commands, then decode each one and
    // confirm the concatenation equals the source and the addresses advance.
    uint8_t buffer[4096];
    std::vector<uint16_t> decodedAll;
    size_t offset = 0;
    uint32_t baseAddress = 0x0a13f5;
    int guard = 0;
    while (offset < source.size())
    {
      dl1xx::CommandBuffer out(buffer, sizeof(buffer));
      const uint32_t address = baseAddress + static_cast<uint32_t>(offset * 2);
      const size_t consumed =
          out.writePixelsRle(address, source.data() + offset, source.size() - offset);
      if (consumed == 0)
      {
        printf("FAIL: encoder made no progress at offset %zu\n", offset);
        failures++;
        break;
      }
      check(out.length() <= dl1xx::maxRleCommandBytes(consumed),
            "command stays within maxRleCommandBytes");

      uint32_t decodedAddress = 0;
      std::vector<uint16_t> decoded;
      if (!decodeRle(buffer, out.length(), decodedAddress, decoded))
      {
        printf("FAIL: stream did not decode at offset %zu (pattern %d)\n", offset, pattern);
        failures++;
        break;
      }
      checkEqual(decodedAddress, address, "decoded address matches");
      checkEqual(decoded.size(), consumed, "decoded pixel count matches the consumed count");
      decodedAll.insert(decodedAll.end(), decoded.begin(), decoded.end());
      offset += consumed;

      if (++guard > 2000)
      {
        printf("FAIL: encoder did not terminate (pattern %d)\n", pattern);
        failures++;
        break;
      }
    }

    if (decodedAll.size() != source.size() ||
        memcmp(decodedAll.data(), source.data(), source.size() * sizeof(uint16_t)) != 0)
    {
      printf("FAIL: round trip mismatch (pattern %d, %zu source vs %zu decoded)\n", pattern,
             source.size(), decodedAll.size());
      failures++;
      return;
    }
  }
}

// The big-endian entry point must produce byte-identical output to the host-order
// one, because that is the whole point: a LovyanGFX rgb565_2Byte buffer can go
// out without per-pixel swapping.
void testRleBigEndianSource()
{
  uint32_t seed = 999;
  const auto next = [&seed]() {
    seed = seed * 1103515245u + 12345u;
    return (seed >> 16) & 0x7fff;
  };

  for (int pattern = 0; pattern < 100; pattern++)
  {
    std::vector<uint16_t> host;
    const size_t target = 1 + (next() % 700);
    while (host.size() < target)
    {
      const uint16_t value = static_cast<uint16_t>(next() & 0xffff);
      const size_t run = (next() % 3 == 0) ? 1 + (next() % 300) : 1;
      for (size_t i = 0; i < run && host.size() < target; i++)
      {
        host.push_back(value);
      }
    }

    // Same pixels, stored as big-endian byte pairs.
    std::vector<uint8_t> bigEndian;
    for (uint16_t pixel : host)
    {
      bigEndian.push_back(static_cast<uint8_t>(pixel >> 8));
      bigEndian.push_back(static_cast<uint8_t>(pixel & 0xff));
    }

    uint8_t bufferA[4096];
    uint8_t bufferB[4096];
    dl1xx::CommandBuffer outA(bufferA, sizeof(bufferA));
    dl1xx::CommandBuffer outB(bufferB, sizeof(bufferB));

    size_t offset = 0;
    while (offset < host.size())
    {
      const uint32_t address = 0x0a13f5 + static_cast<uint32_t>(offset * 2);
      const size_t consumedA = outA.writePixelsRle(address, host.data() + offset, host.size() - offset);
      const size_t consumedB = outB.writePixelsRleBigEndian(address, bigEndian.data() + offset * 2,
                                                            host.size() - offset);
      checkEqual(consumedB, consumedA, "both pixel sources consume the same count");
      if (consumedA == 0 || consumedA != consumedB)
      {
        failures++;
        return;
      }
      offset += consumedA;
    }
    checkEqual(outB.length(), outA.length(), "both pixel sources emit the same length");
    if (outA.length() != outB.length() || memcmp(bufferA, bufferB, outA.length()) != 0)
    {
      printf("FAIL: big-endian source produced different bytes (pattern %d)\n", pattern);
      failures++;
      return;
    }

    // And the stream must still decode back to the original pixels.
    uint32_t address = 0;
    std::vector<uint16_t> decoded;
    std::vector<uint16_t> all;
    size_t at = 0;
    while (at < outB.length())
    {
      // Each command declares its own length, so find it by decoding greedily.
      size_t tryLength = outB.length() - at;
      bool decodedOne = false;
      while (tryLength >= dl1xx::RLE_HEADER_BYTES + 1)
      {
        if (decodeRle(bufferB + at, tryLength, address, decoded))
        {
          all.insert(all.end(), decoded.begin(), decoded.end());
          at += tryLength;
          decodedOne = true;
          break;
        }
        tryLength--;
      }
      if (!decodedOne)
      {
        printf("FAIL: big-endian stream did not decode (pattern %d)\n", pattern);
        failures++;
        return;
      }
    }
    if (all.size() != host.size() || memcmp(all.data(), host.data(), host.size() * 2) != 0)
    {
      printf("FAIL: big-endian round trip mismatch (pattern %d)\n", pattern);
      failures++;
      return;
    }
  }
}

void testRleBufferLimits()
{
  // A buffer too small for even one literal pixel must emit nothing and latch
  // overflow rather than write a truncated command.
  uint8_t small[dl1xx::RLE_HEADER_BYTES + 2];
  memset(small, 0x5a, sizeof(small));
  dl1xx::CommandBuffer tiny(small, sizeof(small));
  const uint16_t pixel = 0x1234;
  checkEqual(tiny.writePixelsRle(0, &pixel, 1), 0, "a too-small buffer consumes no pixels");
  checkEqual(tiny.length(), 0, "a too-small buffer stays empty");
  check(tiny.overflow(), "a too-small buffer latches overflow");

  // A buffer that fits the header plus a few pixels must consume only what fits
  // and never write past the end. The canary catches an overrun.
  uint8_t buffer[32 + 8];
  memset(buffer, 0xa5, sizeof(buffer));
  dl1xx::CommandBuffer out(buffer, 32);
  std::vector<uint16_t> unique;
  for (size_t i = 0; i < 100; i++)
  {
    unique.push_back(static_cast<uint16_t>(i * 7919 + 3));
  }
  const size_t consumed = out.writePixelsRle(0, unique.data(), unique.size());
  check(consumed > 0 && consumed < unique.size(), "a partial write consumes some but not all");
  check(out.length() <= 32, "a partial write stays inside the buffer");
  for (size_t i = 32; i < sizeof(buffer); i++)
  {
    checkEqual(buffer[i], 0xa5, "no write past the buffer end");
  }

  uint32_t address = 0;
  std::vector<uint16_t> decoded;
  check(decodeRle(buffer, out.length(), address, decoded), "a partial write is still a valid command");
  checkEqual(decoded.size(), consumed, "a partial write declares the count it consumed");

  // Every other writer must refuse rather than truncate.
  uint8_t two[2] = {0, 0};
  dl1xx::CommandBuffer narrow(two, sizeof(two));
  check(!narrow.registerWrite(0x00, 0x00), "registerWrite refuses when it does not fit");
  checkEqual(narrow.length(), 0, "a refused registerWrite appends nothing");
  check(narrow.overflow(), "a refused registerWrite latches overflow");
  narrow.reset();
  check(!narrow.registerWrite16(0x00, 0x0000), "registerWrite16 refuses when it does not fit");
  checkEqual(narrow.length(), 0, "a refused registerWrite16 appends nothing");
  narrow.reset();
  check(!narrow.registerWrite24(0x00, 0), "registerWrite24 refuses when it does not fit");
  checkEqual(narrow.length(), 0, "a refused registerWrite24 appends nothing");
  narrow.reset();
  check(narrow.flush(), "flush fits in two bytes");
}

// ---------------------------------------------------------------------------
// Modes
// ---------------------------------------------------------------------------

void testModeTable()
{
  for (size_t i = 0; i < dl1xx::MODE_COUNT; i++)
  {
    const dl1xx::Timing &t = dl1xx::MODES[i];
    check(t.hTotal > t.width + t.hSyncWidth + t.hBackPorch, "hTotal leaves a front porch");
    check(t.vTotal > t.height + t.vSyncWidth + t.vBackPorch, "vTotal leaves a front porch");
    // The refresh rate implied by the timing must be about 60 Hz.
    const double refresh = (static_cast<double>(t.pixelClockKhz) * 1000.0) /
                           (static_cast<double>(t.hTotal) * t.vTotal);
    check(refresh > 59.0 && refresh < 61.0, "the implied refresh rate is about 60 Hz");
    // The pixel clock must be representable in 5 kHz units in 16 bits.
    check(t.pixelClockKhz % dl1xx::PIXEL_CLOCK_UNIT_KHZ == 0, "the pixel clock is a 5 kHz multiple");
    check(t.pixelClockKhz / dl1xx::PIXEL_CLOCK_UNIT_KHZ <= 0xffff,
          "the pixel clock fits the 16-bit register");
    // The frame buffer must be addressable by the 24-bit RLE write address.
    check(dl1xx::base16PlaneBytes(t) <= 0x01000000u, "the frame buffer fits the 24-bit address");
  }

  const dl1xx::Timing *fullHd = dl1xx::findMode(1920, 1080);
  check(fullHd != nullptr, "1920x1080 is in the table");
  if (fullHd)
  {
    checkEqual(dl1xx::base16PlaneBytes(*fullHd), 4147200, "Full HD needs 4,147,200 bytes");
    checkEqual(dl1xx::base16PlaneBytes(*fullHd), 0x3f4800, "Full HD frame buffer ends at 0x3F4800");
    checkEqual(dl1xx::hDisplayStart(*fullHd), 192, "Full HD hDisplayStart = hbp + hsync");
    checkEqual(dl1xx::hDisplayEnd(*fullHd), 2112, "Full HD hDisplayEnd");
    checkEqual(dl1xx::vDisplayStart(*fullHd), 41, "Full HD vDisplayStart = vbp + vsync");
    checkEqual(dl1xx::vDisplayEnd(*fullHd), 1121, "Full HD vDisplayEnd");
    checkEqual(fullHd->pixelClockKhz / dl1xx::PIXEL_CLOCK_UNIT_KHZ, 29700,
               "Full HD pixel clock is 29700 units of 5 kHz");

    // The bottom-right pixel address must still fit 24 bits.
    const uint32_t last =
        (static_cast<uint32_t>(fullHd->height - 1) * fullHd->width + (fullHd->width - 1)) * 2u;
    checkEqual(last, 0x3f47fe, "the last Full HD pixel address");
    check(last <= 0xffffff, "the last Full HD pixel address fits 24 bits");
  }

  check(dl1xx::selectMode(0) == fullHd, "no pixel limit selects the largest mode");
  const dl1xx::Timing *limited = dl1xx::selectMode(1024 * 768);
  check(limited == dl1xx::findMode(1024, 768), "a 1024x768 pixel limit selects 1024x768");
  check(dl1xx::selectMode(100) == nullptr, "an impossibly small limit selects nothing");
}

void testModeSetSequence()
{
  const dl1xx::Timing *t = dl1xx::findMode(1920, 1080);
  check(t != nullptr, "1920x1080 is in the table");
  if (!t)
  {
    return;
  }

  uint8_t buffer[512];
  dl1xx::CommandBuffer out(buffer, sizeof(buffer));
  check(dl1xx::writeModeSet(out, *t), "writeModeSet succeeds");
  check(!out.overflow(), "writeModeSet does not overflow a 512-byte buffer");
  checkEqual(out.length(), dl1xx::MODE_SET_BYTES, "writeModeSet matches MODE_SET_BYTES");

  // Rebuild the expected stream from the documented derivation.
  std::vector<uint8_t> expected;
  const auto reg = [&expected](uint8_t r, uint8_t v) {
    expected.push_back(0xaf);
    expected.push_back(0x20);
    expected.push_back(r);
    expected.push_back(v);
  };
  const auto reg16 = [&reg](uint8_t r, uint16_t v) {
    reg(r, static_cast<uint8_t>(v >> 8));
    reg(static_cast<uint8_t>(r + 1), static_cast<uint8_t>(v & 0xff));
  };

  reg(0xff, 0x00); // lock
  reg(0x00, 0x00); // 16 bpp
  reg16(0x01, dl1xx::lfsr16(192));
  reg16(0x03, dl1xx::lfsr16(2112));
  reg16(0x05, dl1xx::lfsr16(41));
  reg16(0x07, dl1xx::lfsr16(1121));
  reg16(0x09, dl1xx::lfsr16(2199));
  reg16(0x0b, dl1xx::lfsr16(1));
  reg16(0x0d, dl1xx::lfsr16(45));
  reg16(0x0f, 1920);
  reg16(0x11, dl1xx::lfsr16(1125));
  reg16(0x13, dl1xx::lfsr16(0));
  reg16(0x15, dl1xx::lfsr16(5));
  reg16(0x17, 1080);
  reg(0x1b, 0x04); // 29700 low byte first
  reg(0x1c, 0x74);
  reg(0x20, 0x00); // base16 at 0
  reg(0x21, 0x00);
  reg(0x22, 0x00);
  reg(0x26, 0x3f); // base8 right after base16
  reg(0x27, 0x48);
  reg(0x28, 0x00);
  reg(0x1f, 0x00); // display on
  reg(0xff, 0xff); // unlock (applies the mode)

  checkBytes(buffer, out.length(), expected, "the Full HD mode-set register stream");

  // Structural check: walk the emitted stream and confirm it programs exactly the
  // registers MODE_SET_REGISTERS lists, in order. A hand-written expected stream
  // cannot catch a register that was left out of both the code and the
  // expectation -- and an omitted timing register still transfers fine, it just
  // leaves the monitor without a valid signal.
  {
    std::vector<uint8_t> written;
    for (size_t at = 0; at + 4 <= out.length(); at += 4)
    {
      checkEqual(buffer[at], 0xaf, "every mode-set element is a command");
      checkEqual(buffer[at + 1], 0x20, "every mode-set element is a register write");
      written.push_back(buffer[at + 2]);
    }
    checkEqual(written.size(), dl1xx::MODE_SET_REGISTER_COUNT,
               "the stream holds one write per listed register");
    for (size_t i = 0; i < written.size() && i < dl1xx::MODE_SET_REGISTER_COUNT; i++)
    {
      if (written[i] != dl1xx::MODE_SET_REGISTERS[i])
      {
        printf("FAIL: mode-set register %zu is 0x%02x, expected 0x%02x\n", i, written[i],
               dl1xx::MODE_SET_REGISTERS[i]);
        failures++;
      }
    }
    // Both halves of every 16-bit timing register must be present. The high half
    // is listed, so its neighbour must be too.
    static const uint8_t TIMING_REGISTERS[] = {0x01, 0x03, 0x05, 0x07, 0x09,
                                               0x0b, 0x0d, 0x0f, 0x11, 0x13,
                                               0x15, 0x17, 0x1b};
    for (uint8_t reg : TIMING_REGISTERS)
    {
      size_t low = 0;
      size_t high = 0;
      for (uint8_t seen : written)
      {
        if (seen == reg)
        {
          high++;
        }
        if (seen == static_cast<uint8_t>(reg + 1))
        {
          low++;
        }
      }
      if (high != 1 || low != 1)
      {
        printf("FAIL: register 0x%02x written %zu time(s) and 0x%02x %zu time(s); each 16-bit "
               "timing register needs both halves exactly once\n",
               reg, high, static_cast<uint8_t>(reg + 1), low);
        failures++;
      }
    }
  }

  // Every mode must fit the same buffer, and the last write must be the unlock.
  for (size_t i = 0; i < dl1xx::MODE_COUNT; i++)
  {
    dl1xx::CommandBuffer each(buffer, sizeof(buffer));
    check(dl1xx::writeModeSet(each, dl1xx::MODES[i]), "writeModeSet succeeds for every mode");
    checkEqual(each.length(), dl1xx::MODE_SET_BYTES, "every mode-set has the same length");
    checkEqual(buffer[each.length() - 2], 0xff, "the sequence ends with the lock register");
    checkEqual(buffer[each.length() - 1], 0xff, "the sequence ends by unlocking");
  }

  // A buffer one byte short must fail cleanly instead of emitting a partial
  // sequence that would leave the registers locked.
  dl1xx::CommandBuffer tight(buffer, dl1xx::MODE_SET_BYTES - 1);
  check(!dl1xx::writeModeSet(tight, *t), "writeModeSet fails when it does not fit");
  check(tight.overflow(), "a failed writeModeSet latches overflow");
}

} // namespace

int main()
{
  testLfsr();
  testRegisterWrites();
  testRleSolidRun();
  testRleWorstCase();
  testRleRoundTrip();
  testRleBigEndianSource();
  testRleBufferLimits();
  testModeTable();
  testModeSetSequence();

  if (failures != 0)
  {
    printf("%d check(s) failed\n", failures);
    return 1;
  }
  printf("all DL-1xx protocol checks passed\n");
  return 0;
}
