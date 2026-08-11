// ESC/POS command builder.
//
// ESC/POS is the print data language most receipt printers speak; the USB Printer
// Class only carries it. It is a byte stream, so this header is a writer over a
// caller-owned buffer with no Arduino or USB dependencies - tests/unit/escpos
// compiles it directly with g++ and checks the bytes.
//
// One buffer holds a whole receipt and goes out as a single bulk transfer, which
// matters: a printer starts printing as soon as it has a full line, so splitting a
// receipt across transfers that the host might delay produces visible stutter and,
// on some models, a partial line before a timeout.
//
// Commands are named after the ESC/POS mnemonic in the comment so they can be
// looked up in the Epson command reference. Where a model-specific difference is
// known it is noted at the call.

#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace escpos
{

static constexpr uint8_t LF = 0x0a;
static constexpr uint8_t ESC = 0x1b;
static constexpr uint8_t GS = 0x1d;
static constexpr uint8_t FS = 0x1c;
static constexpr uint8_t DLE = 0x10;
static constexpr uint8_t EOT = 0x04;

enum Align : uint8_t
{
  ALIGN_LEFT = 0,
  ALIGN_CENTER = 1,
  ALIGN_RIGHT = 2,
};

// ESC t n - single-byte character code table. Numbering is model-specific beyond
// the first few; these three are the ones that hold across Epson-compatible
// firmware.
enum CodeTable : uint8_t
{
  CODE_TABLE_PC437 = 0,     // US / European
  CODE_TABLE_KATAKANA = 1,  // half-width katakana in 0xa1..0xdf
  CODE_TABLE_PC850 = 2,     // multilingual
};

// FS C n - which multi-byte encoding kanji text is sent in. Japanese models
// power up in one of these; setting it explicitly is what makes a sketch
// portable between them.
enum KanjiCode : uint8_t
{
  KANJI_CODE_JIS = 0,
  KANJI_CODE_SHIFT_JIS = 1,
};

// GS V m - cut mode. 66 feeds by n dots first, which is what a receipt wants:
// without the feed the last lines are still inside the mechanism, above the
// blade, and get cut through.
enum CutMode : uint8_t
{
  CUT_FULL = 0,
  CUT_PARTIAL = 1,
  CUT_FEED_AND_FULL = 65,
  CUT_FEED_AND_PARTIAL = 66,
};

// DLE EOT n - real-time status. Answered on bulk IN even when the printer is busy
// or offline, because it is handled ahead of the print buffer.
enum RealtimeStatus : uint8_t
{
  STATUS_PRINTER = 1,
  STATUS_OFFLINE = 2,
  STATUS_ERROR = 3,
  STATUS_PAPER_ROLL = 4,
};

// Bits of the DLE EOT 4 (paper roll sensor) reply. Bits 0 and 1 are fixed at 0
// and 1 in every reply, which is how a status byte is told apart from print data
// echoed back by a confused device.
static constexpr uint8_t PAPER_NEAR_END = 0x0c; // bits 2,3 both set
static constexpr uint8_t PAPER_END = 0x60;      // bits 5,6 both set

struct PaperStatus
{
  uint8_t raw = 0;
  bool nearEnd = false;
  bool out = false;
};

inline PaperStatus decodePaperStatus(uint8_t raw)
{
  PaperStatus status;
  status.raw = raw;
  // Either bit of each pair is enough: the pairs are duplicated for reliability
  // on a serial line, and a printer that sets only one still means it.
  status.nearEnd = (raw & PAPER_NEAR_END) != 0;
  status.out = (raw & PAPER_END) != 0;
  return status;
}

// Appends ESC/POS into a caller-owned buffer. Every call is bounds-checked and
// sets an overflow flag instead of truncating silently, so a receipt that does not
// fit is a detectable error rather than a half-printed slip.
class Builder
{
public:
  Builder(uint8_t *buffer, size_t capacity) : buffer_(buffer), capacity_(capacity) {}

  void reset()
  {
    length_ = 0;
    overflow_ = false;
  }

  const uint8_t *data() const { return buffer_; }
  size_t length() const { return length_; }
  bool overflow() const { return overflow_; }
  bool ok() const { return !overflow_; }
  size_t remaining() const { return capacity_ - length_; }

  // ESC @ - initialise. Clears the print buffer and returns every setting below
  // to its power-on value, so a receipt always starts from a known state whatever
  // the previous sketch left behind.
  Builder &init() { return command(ESC, '@'); }

  Builder &codeTable(uint8_t table) { return command(ESC, 't', table); }
  Builder &kanjiCode(uint8_t code) { return command(FS, 'C', code); }

  // FS & / FS . - kanji mode on and off. Only while it is on are 0x81.. bytes
  // read as the first half of a two-byte character; with it off the same bytes
  // print as whatever the single-byte code table has there.
  Builder &kanjiOn() { return command(FS, '&'); }
  Builder &kanjiOff() { return command(FS, '.'); }

  Builder &align(uint8_t alignment) { return command(ESC, 'a', alignment); }
  Builder &bold(bool on) { return command(ESC, 'E', on ? 1 : 0); }
  Builder &underline(uint8_t dots) { return command(ESC, '-', dots); } // 0,1,2
  Builder &inverse(bool on) { return command(GS, 'B', on ? 1 : 0); }   // white on black
  Builder &upsideDown(bool on) { return command(ESC, '{', on ? 1 : 0); }

  // GS ! n - character size, 1..8 in each direction, packed as
  // (width-1) << 4 | (height-1).
  Builder &size(uint8_t width, uint8_t height)
  {
    const uint8_t w = clampScale(width);
    const uint8_t h = clampScale(height);
    return command(GS, '!', static_cast<uint8_t>(((w - 1) << 4) | (h - 1)));
  }

  // ESC 3 n - line spacing in dots. ESC 2 restores the default.
  Builder &lineSpacing(uint8_t dots) { return command(ESC, '3', dots); }
  Builder &defaultLineSpacing() { return command(ESC, '2'); }

  Builder &text(const char *string)
  {
    return string ? bytes(reinterpret_cast<const uint8_t *>(string), strlen(string)) : *this;
  }

  Builder &line(const char *string = nullptr)
  {
    text(string);
    return feed();
  }

  Builder &feed(uint8_t lines = 1)
  {
    for (uint8_t i = 0; i < lines; i++)
    {
      put(LF);
    }
    return *this;
  }

  // ESC d n - feed n lines in one command. Cheaper than n LFs and, unlike them,
  // does not print a partially filled line first.
  Builder &feedLines(uint8_t lines) { return command(ESC, 'd', lines); }

  // GS V m [n] - cut. The feeding variants take the feed amount in dots.
  Builder &cut(uint8_t mode = CUT_FEED_AND_PARTIAL, uint8_t feedDots = 0x50)
  {
    if (mode == CUT_FEED_AND_FULL || mode == CUT_FEED_AND_PARTIAL)
    {
      return command(GS, 'V', mode, feedDots);
    }
    return command(GS, 'V', mode);
  }

  // GS k m n d1..dn - barcode, using the length-prefixed form (m >= 65) so the
  // data may contain any byte. m 73 is CODE128, where the data itself starts with
  // a code-set selector such as "{B".
  Builder &barcode(uint8_t type, const uint8_t *payload, size_t payloadLength)
  {
    if (!payload || payloadLength == 0 || payloadLength > 255)
    {
      overflow_ = true;
      return *this;
    }
    command(GS, 'k', type, static_cast<uint8_t>(payloadLength));
    return bytes(payload, payloadLength);
  }

  Builder &barcodeHeight(uint8_t dots) { return command(GS, 'h', dots); }
  Builder &barcodeWidth(uint8_t module) { return command(GS, 'w', module); }        // 2..6
  Builder &barcodeTextPosition(uint8_t position) { return command(GS, 'H', position); } // 0..3

  // GS ( k - QR code, as the four-function sequence every implementation needs:
  // model, module size, error correction, store data, then print. The length
  // field covers the data plus the two function bytes and is little endian.
  Builder &qr(const char *payload, uint8_t moduleSize = 4, uint8_t errorCorrection = 49)
  {
    if (!payload)
    {
      overflow_ = true;
      return *this;
    }
    const size_t payloadLength = strlen(payload);
    if (payloadLength == 0 || payloadLength > 0xffff - 3)
    {
      overflow_ = true;
      return *this;
    }
    // cn=49 fn=65: model 2
    qrHeader(4);
    put(49);
    put(65);
    put(50);
    put(0);
    // cn=49 fn=67: module size
    qrHeader(3);
    put(49);
    put(67);
    put(moduleSize);
    // cn=49 fn=69: error correction level, 48..51 = L,M,Q,H
    qrHeader(3);
    put(49);
    put(69);
    put(errorCorrection);
    // cn=49 fn=80: store the data
    qrHeader(payloadLength + 3);
    put(49);
    put(80);
    put(48);
    bytes(reinterpret_cast<const uint8_t *>(payload), payloadLength);
    // cn=49 fn=81: print what is stored
    qrHeader(3);
    put(49);
    put(81);
    put(48);
    return *this;
  }

  // GS v 0 m xL xH yL yH d1..dk - raster bitmap, one bit per pixel, MSB first,
  // rows padded to whole bytes. This is how arbitrary text gets printed on a
  // printer whose internal fonts do not cover it: render to a bitmap on the ESP32
  // and send that.
  Builder &raster(const uint8_t *bitmap, size_t widthPixels, size_t heightPixels, uint8_t mode = 0)
  {
    const size_t widthBytes = (widthPixels + 7) / 8;
    if (!bitmap || widthBytes == 0 || heightPixels == 0 || widthBytes > 0xffff ||
        heightPixels > 0xffff)
    {
      overflow_ = true;
      return *this;
    }
    command(GS, 'v', '0', mode);
    put(static_cast<uint8_t>(widthBytes & 0xff));
    put(static_cast<uint8_t>(widthBytes >> 8));
    put(static_cast<uint8_t>(heightPixels & 0xff));
    put(static_cast<uint8_t>(heightPixels >> 8));
    return bytes(bitmap, widthBytes * heightPixels);
  }

  // DLE EOT n - real-time status request. Sent in its own transfer, not appended
  // to a receipt: the point of it is that it is answered immediately, and a
  // printer that is mid-receipt would otherwise process it in order.
  Builder &realtimeStatus(uint8_t which) { return command(DLE, EOT, which); }

  // ESC = n - select which peripheral receives data. 1 = printer, 0 = nothing;
  // some models power up deselected after a paper-out error.
  Builder &selectPrinter(bool on) { return command(ESC, '=', on ? 1 : 0); }

  Builder &bytes(const uint8_t *data, size_t length)
  {
    if (!data)
    {
      return *this;
    }
    if (length > remaining())
    {
      overflow_ = true;
      return *this;
    }
    memcpy(buffer_ + length_, data, length);
    length_ += length;
    return *this;
  }

private:
  static uint8_t clampScale(uint8_t scale)
  {
    if (scale < 1)
    {
      return 1;
    }
    return scale > 8 ? 8 : scale;
  }

  void put(uint8_t value)
  {
    if (remaining() == 0)
    {
      overflow_ = true;
      return;
    }
    buffer_[length_++] = value;
  }

  void qrHeader(size_t length)
  {
    put(GS);
    put('(');
    put('k');
    put(static_cast<uint8_t>(length & 0xff));
    put(static_cast<uint8_t>(length >> 8));
  }

  Builder &command(uint8_t a, uint8_t b)
  {
    put(a);
    put(b);
    return *this;
  }

  Builder &command(uint8_t a, uint8_t b, uint8_t c)
  {
    command(a, b);
    put(c);
    return *this;
  }

  Builder &command(uint8_t a, uint8_t b, uint8_t c, uint8_t d)
  {
    command(a, b, c);
    put(d);
    return *this;
  }

  uint8_t *buffer_ = nullptr;
  size_t capacity_ = 0;
  size_t length_ = 0;
  bool overflow_ = false;
};

} // namespace escpos
