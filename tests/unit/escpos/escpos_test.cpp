// Host tests for the USB Printer Class request layer and the ESC/POS builder used
// by examples/Vendor/EspUsbHostPrinterEscPos. All three headers under test are
// pure byte formatting with no Arduino / USB dependencies, so they are included
// directly and the production code itself is exercised.

#include "EscPos.hpp"
#include "PrinterProtocol.hpp"
#include "ReceiptJa.hpp"

#include <cstdio>
#include <cstring>
#include <string>
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

void checkText(const char *actual, const char *expected, const char *what)
{
  if (strcmp(actual, expected) != 0)
  {
    printf("FAIL: %s (actual=\"%s\", expected=\"%s\")\n", what, actual, expected);
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
    for (size_t i = 0; i < expected.size(); i++)
    {
      printf(" %02x", expected[i]);
    }
    printf("\n");
    failures++;
  }
}

// Finds a byte sequence inside a built stream. Used where a test cares that a
// command is present and correct without pinning everything around it.
bool contains(const uint8_t *data, size_t length, const std::vector<uint8_t> &needle)
{
  if (needle.empty() || needle.size() > length)
  {
    return false;
  }
  for (size_t i = 0; i + needle.size() <= length; i++)
  {
    if (memcmp(data + i, needle.data(), needle.size()) == 0)
    {
      return true;
    }
  }
  return false;
}

size_t countSequence(const uint8_t *data, size_t length, const std::vector<uint8_t> &needle)
{
  size_t found = 0;
  for (size_t i = 0; i + needle.size() <= length; i++)
  {
    if (memcmp(data + i, needle.data(), needle.size()) == 0)
    {
      found++;
    }
  }
  return found;
}

// -- Printer class requests ------------------------------------------------

void testRequestConstants()
{
  // Class IN / OUT to an interface. Getting the recipient wrong is the classic
  // way for GET_DEVICE_ID to return nothing on a device that supports it.
  checkEqual(printer::CLASS_REQUEST_IN, 0xa1, "GET_* is a class IN request to an interface");
  checkEqual(printer::CLASS_REQUEST_OUT, 0x21, "SOFT_RESET is a class OUT request to an interface");
  checkEqual(printer::REQ_GET_DEVICE_ID, 0x00, "GET_DEVICE_ID code");
  checkEqual(printer::REQ_GET_PORT_STATUS, 0x01, "GET_PORT_STATUS code");
  checkEqual(printer::REQ_SOFT_RESET, 0x02, "SOFT_RESET code");
  checkEqual(printer::INTERFACE_CLASS, 0x07, "printer interface class");

  // GET_DEVICE_ID puts the interface in the high byte and the alternate setting
  // in the low byte, unlike every other class request here.
  checkEqual(printer::deviceIdIndex(0), 0x0000, "wIndex for interface 0 alt 0");
  checkEqual(printer::deviceIdIndex(1), 0x0100, "wIndex for interface 1 alt 0");
  checkEqual(printer::deviceIdIndex(2, 3), 0x0203, "wIndex packs interface and alternate setting");
}

void testPortStatus()
{
  // A printer that reports properly: paper present, selected, no error.
  const printer::PortStatus idle = printer::decodePortStatus(0x18);
  check(!idle.unknown, "a non-zero byte carries information");
  check(!idle.paperEmpty, "0x18 has paper");
  check(idle.selected, "0x18 is selected");
  check(!idle.error, "0x18 has no error - NotError is 1 when things are good");

  const printer::PortStatus empty = printer::decodePortStatus(0x38);
  check(empty.paperEmpty, "bit 5 set means paper empty");
  check(!empty.error, "paper empty on its own is not the error bit");

  const printer::PortStatus faulted = printer::decodePortStatus(0x10);
  check(faulted.error, "NotError clear means error");
  check(faulted.selected, "select is independent of the error bit");

  const printer::PortStatus offline = printer::decodePortStatus(0x08);
  check(!offline.selected, "select clear means deselected");
  check(!offline.error, "deselected is not by itself an error");

  // The measured value of an XP-C58K, which does not implement the request: 0x00
  // every time, while its real-time status reported the printer ready. Read
  // literally it says deselected-with-error, so it is reported as no information
  // instead - otherwise a print loop refuses to print on a healthy printer.
  const printer::PortStatus unimplemented = printer::decodePortStatus(0x00);
  check(unimplemented.unknown, "0x00 carries no information");
  check(!unimplemented.error, "0x00 is not reported as an error");
  check(!unimplemented.paperEmpty, "0x00 is not reported as paper empty");
  checkEqual(unimplemented.raw, 0x00, "the raw byte is still available");
}

void testDeviceId()
{
  char text[128] = {};
  size_t textLength = 0;

  // Length counts itself: a 20-character ID declares 22.
  const std::string body = "MFG:Xprinter;MDL:XP;";
  std::vector<uint8_t> response = {0x00, static_cast<uint8_t>(body.size() + 2)};
  response.insert(response.end(), body.begin(), body.end());
  check(printer::decodeDeviceId(response.data(), response.size(), text, sizeof(text), &textLength),
        "a well-formed device ID is accepted");
  checkEqual(textLength, body.size(), "device ID length excludes the two length bytes");
  checkText(text, body.c_str(), "device ID text");

  // A response that arrived with padding past the declared length is trimmed to
  // what the device said, not to what turned up.
  response.push_back('X');
  response.push_back('X');
  check(printer::decodeDeviceId(response.data(), response.size(), text, sizeof(text), &textLength),
        "padding does not make the response malformed");
  checkEqual(textLength, body.size(), "trailing bytes past the declared length are ignored");

  // A declared length longer than the transfer means a short read: the string is
  // incomplete and could be cut mid-field, so it is rejected rather than trusted.
  const uint8_t truncated[] = {0x00, 0x40, 'M', 'F', 'G', ':', 'X', ';'};
  check(!printer::decodeDeviceId(truncated, sizeof(truncated), text, sizeof(text), &textLength),
        "a declared length past the response is rejected");
  checkText(text, "", "a rejected device ID leaves an empty string");
  checkEqual(textLength, 0, "a rejected device ID reports no characters");

  const uint8_t tooShort[] = {0x00};
  check(!printer::decodeDeviceId(tooShort, sizeof(tooShort), text, sizeof(text), &textLength),
        "a response shorter than the length field is rejected");

  // An empty ID is well formed, and is what a real printer answers: an Xprinter
  // XP-C58K returns exactly these two bytes. Reporting it as a failure would mark a
  // working request as broken, so the return value and the character count say
  // different things here on purpose.
  const uint8_t empty[] = {0x00, 0x02};
  check(printer::decodeDeviceId(empty, sizeof(empty), text, sizeof(text), &textLength),
        "an empty device ID is well formed");
  checkEqual(textLength, 0, "an empty device ID yields no characters");
  checkText(text, "", "an empty device ID is an empty string");

  // A length of 0 or 1 cannot count itself, so it is malformed rather than empty.
  const uint8_t zeroLength[] = {0x00, 0x00, 'X'};
  check(!printer::decodeDeviceId(zeroLength, sizeof(zeroLength), text, sizeof(text), &textLength),
        "a length field below 2 is rejected");

  // A caller's small buffer truncates rather than overflows.
  char small[8] = {};
  check(printer::decodeDeviceId(response.data(), response.size(), small, sizeof(small), &textLength),
        "a truncating copy still succeeds");
  checkEqual(textLength, 7, "the copy is capped by the caller's buffer");
  checkText(small, "MFG:Xpr", "truncated copy is still NUL-terminated");
}

void testDeviceIdField()
{
  const char *id = "MFG:Xprinter;CMD:ESC/POS,ESC/P;MDL:XP-C58K;CLS:PRINTER;";
  char value[64] = {};

  checkEqual(printer::deviceIdField(id, "MFG", value, sizeof(value)), 8, "MFG length");
  checkText(value, "Xprinter", "MFG value");
  checkEqual(printer::deviceIdField(id, "MDL", value, sizeof(value)), 7, "MDL length");
  checkText(value, "XP-C58K", "MDL value");
  checkEqual(printer::deviceIdField(id, "CMD", value, sizeof(value)), 13, "CMD length");
  checkText(value, "ESC/POS,ESC/P", "a value may contain commas and slashes");

  // A key is only matched at the start of a field, so the CMD field does not
  // answer a search for MD, and CMDL would not answer one for CMD.
  checkEqual(printer::deviceIdField(id, "MD", value, sizeof(value)), 0,
             "a key is matched in full, not as a prefix of the field name");
  checkEqual(printer::deviceIdField("CMDL:x;MDL:y;", "MDL", value, sizeof(value)), 1,
             "a longer key at the front does not shadow the real field");
  checkText(value, "y", "the real field is found after the decoy");

  checkEqual(printer::deviceIdField(id, "SERN", value, sizeof(value)), 0, "an absent key yields 0");
  checkText(value, "", "an absent key leaves an empty value");

  // The last field of a device ID string is often not terminated with ';'.
  checkEqual(printer::deviceIdField("MFG:A;MDL:B", "MDL", value, sizeof(value)), 1,
             "an unterminated final field is still read");
  checkText(value, "B", "unterminated final value");

  char small[4] = {};
  checkEqual(printer::deviceIdField(id, "MFG", small, sizeof(small)), 3,
             "a value is truncated to the caller's buffer");
  checkText(small, "Xpr", "truncated value is NUL-terminated");
}

// -- ESC/POS builder -------------------------------------------------------

void testBasicCommands()
{
  uint8_t buffer[64] = {};
  escpos::Builder out(buffer, sizeof(buffer));

  out.init();
  checkBytes(out.data(), out.length(), {0x1b, '@'}, "ESC @");

  out.reset();
  out.codeTable(escpos::CODE_TABLE_KATAKANA).kanjiCode(escpos::KANJI_CODE_SHIFT_JIS);
  checkBytes(out.data(), out.length(), {0x1b, 't', 0x01, 0x1c, 'C', 0x01},
             "ESC t and FS C carry the table numbers");

  out.reset();
  out.kanjiOn().kanjiOff();
  checkBytes(out.data(), out.length(), {0x1c, '&', 0x1c, '.'}, "FS & / FS .");

  out.reset();
  out.align(escpos::ALIGN_CENTER).bold(true).underline(2).inverse(true).upsideDown(false);
  checkBytes(out.data(), out.length(),
             {0x1b, 'a', 0x01, 0x1b, 'E', 0x01, 0x1b, '-', 0x02, 0x1d, 'B', 0x01, 0x1b, '{', 0x00},
             "text attributes");

  out.reset();
  out.lineSpacing(30).defaultLineSpacing();
  checkBytes(out.data(), out.length(), {0x1b, '3', 0x1e, 0x1b, '2'}, "line spacing");

  out.reset();
  out.selectPrinter(true);
  checkBytes(out.data(), out.length(), {0x1b, '=', 0x01}, "ESC =");
}

void testSize()
{
  uint8_t buffer[16] = {};
  escpos::Builder out(buffer, sizeof(buffer));

  // GS ! packs width-1 in the high nibble and height-1 in the low one.
  out.size(1, 1);
  checkBytes(out.data(), out.length(), {0x1d, '!', 0x00}, "1x1 is 0x00");
  out.reset();
  out.size(2, 2);
  checkBytes(out.data(), out.length(), {0x1d, '!', 0x11}, "2x2 is 0x11");
  out.reset();
  out.size(1, 2);
  checkBytes(out.data(), out.length(), {0x1d, '!', 0x01}, "double height only");
  out.reset();
  out.size(8, 8);
  checkBytes(out.data(), out.length(), {0x1d, '!', 0x77}, "8x8 is the maximum, 0x77");

  // Out-of-range scales are clamped into the legal 1..8, because the encoding has
  // nowhere to put a 9 and would wrap into the neighbouring nibble.
  out.reset();
  out.size(0, 0);
  checkBytes(out.data(), out.length(), {0x1d, '!', 0x00}, "0 clamps up to 1");
  out.reset();
  out.size(9, 200);
  checkBytes(out.data(), out.length(), {0x1d, '!', 0x77}, "oversized scales clamp down to 8");
}

void testTextAndFeed()
{
  uint8_t buffer[64] = {};
  escpos::Builder out(buffer, sizeof(buffer));

  out.line("hi");
  checkBytes(out.data(), out.length(), {'h', 'i', 0x0a}, "line appends LF");

  out.reset();
  out.feed(3);
  checkBytes(out.data(), out.length(), {0x0a, 0x0a, 0x0a}, "feed(n) is n LFs");

  out.reset();
  out.feedLines(4);
  checkBytes(out.data(), out.length(), {0x1b, 'd', 0x04}, "ESC d is one command");

  out.reset();
  out.text(nullptr).line();
  checkBytes(out.data(), out.length(), {0x0a}, "a null string contributes nothing");
}

void testCut()
{
  uint8_t buffer[16] = {};
  escpos::Builder out(buffer, sizeof(buffer));

  // The feeding modes take the feed distance; the plain ones must not, or the
  // printer reads the next byte of the receipt as an argument.
  out.cut(escpos::CUT_FEED_AND_PARTIAL, 0x50);
  checkBytes(out.data(), out.length(), {0x1d, 'V', 66, 0x50}, "GS V 66 takes a feed amount");
  out.reset();
  out.cut(escpos::CUT_FEED_AND_FULL, 0x20);
  checkBytes(out.data(), out.length(), {0x1d, 'V', 65, 0x20}, "GS V 65 takes a feed amount");
  out.reset();
  out.cut(escpos::CUT_PARTIAL);
  checkBytes(out.data(), out.length(), {0x1d, 'V', 0x01}, "GS V 1 takes no argument");
  out.reset();
  out.cut(escpos::CUT_FULL);
  checkBytes(out.data(), out.length(), {0x1d, 'V', 0x00}, "GS V 0 takes no argument");
}

void testBarcode()
{
  uint8_t buffer[64] = {};
  escpos::Builder out(buffer, sizeof(buffer));

  const char *payload = "{B1234";
  out.barcodeHeight(60).barcodeWidth(2).barcodeTextPosition(2);
  out.barcode(73, reinterpret_cast<const uint8_t *>(payload), strlen(payload));
  checkBytes(out.data(), out.length(),
             {0x1d, 'h', 60, 0x1d, 'w', 0x02, 0x1d, 'H', 0x02, 0x1d, 'k', 73, 6, '{', 'B', '1', '2',
              '3', '4'},
             "GS k m n d1..dn with an explicit length");

  // An empty or oversized payload has no valid encoding, and a silently dropped
  // barcode would leave the receipt looking finished but wrong.
  out.reset();
  out.barcode(73, reinterpret_cast<const uint8_t *>(payload), 0);
  check(out.overflow(), "an empty barcode payload is an error");
  out.reset();
  std::vector<uint8_t> huge(256, 'x');
  out.barcode(73, huge.data(), huge.size());
  check(out.overflow(), "a payload past 255 bytes is an error");
}

void testQr()
{
  uint8_t buffer[64] = {};
  escpos::Builder out(buffer, sizeof(buffer));

  out.qr("AB", 4, 49);
  checkBytes(out.data(), out.length(),
             {// model 2
              0x1d, '(', 'k', 0x04, 0x00, 49, 65, 50, 0,
              // module size 4
              0x1d, '(', 'k', 0x03, 0x00, 49, 67, 4,
              // error correction level M
              0x1d, '(', 'k', 0x03, 0x00, 49, 69, 49,
              // store "AB": the length covers cn, fn, m and the data
              0x1d, '(', 'k', 0x05, 0x00, 49, 80, 48, 'A', 'B',
              // print
              0x1d, '(', 'k', 0x03, 0x00, 49, 81, 48},
             "the five-command QR sequence");

  // The store length is little endian and counts three bytes of overhead, which is
  // the field most implementations get wrong.
  uint8_t big[1024] = {};
  escpos::Builder wide(big, sizeof(big));
  std::string payload(300, 'x');
  wide.qr(payload.c_str());
  const std::vector<uint8_t> storeHeader = {0x1d, '(', 'k', 0x2f, 0x01, 49, 80, 48};
  check(contains(wide.data(), wide.length(), storeHeader),
       "a 300-byte payload declares 303 as 2f 01");

  out.reset();
  out.qr("");
  check(out.overflow(), "an empty QR payload is an error");
}

void testRaster()
{
  uint8_t buffer[64] = {};
  escpos::Builder out(buffer, sizeof(buffer));

  // 12 pixels wide rounds up to 2 bytes per row; 3 rows means 6 data bytes.
  const uint8_t bitmap[6] = {0xff, 0xf0, 0x80, 0x10, 0xff, 0xf0};
  out.raster(bitmap, 12, 3);
  checkBytes(out.data(), out.length(),
             {0x1d, 'v', '0', 0x00, 0x02, 0x00, 0x03, 0x00, 0xff, 0xf0, 0x80, 0x10, 0xff, 0xf0},
             "GS v 0 with the width in bytes and the height in rows");

  out.reset();
  out.raster(bitmap, 0, 3);
  check(out.overflow(), "a zero-width raster is an error");
  out.reset();
  out.raster(bitmap, 12, 0);
  check(out.overflow(), "a zero-height raster is an error");
}

void testRealtimeStatus()
{
  uint8_t buffer[8] = {};
  escpos::Builder out(buffer, sizeof(buffer));

  out.realtimeStatus(escpos::STATUS_PAPER_ROLL);
  checkBytes(out.data(), out.length(), {0x10, 0x04, 0x04}, "DLE EOT 4");

  // Paper-out is reported in two duplicated bit pairs. Either bit of a pair means
  // the condition, so a printer that sets only one is still believed.
  const escpos::PaperStatus present = escpos::decodePaperStatus(0x12);
  check(!present.nearEnd, "0x12 is not near the end");
  check(!present.out, "0x12 has paper");

  const escpos::PaperStatus low = escpos::decodePaperStatus(0x1e);
  check(low.nearEnd, "bits 2,3 mean the roll is nearly out");
  check(!low.out, "nearly out is not out");

  const escpos::PaperStatus gone = escpos::decodePaperStatus(0x72);
  check(gone.out, "bits 5,6 mean the paper is out");

  const escpos::PaperStatus half = escpos::decodePaperStatus(0x22);
  check(half.out, "one bit of the pair is enough");
}

void testOverflow()
{
  // A receipt that does not fit must be detectable: the builder stops writing and
  // latches the flag, so a caller cannot send a slip whose tail - very possibly
  // the arguments of a command, or the cut - was dropped.
  uint8_t buffer[6] = {};
  escpos::Builder out(buffer, sizeof(buffer));
  out.init();
  checkEqual(out.length(), 2, "two bytes used");
  checkEqual(out.remaining(), 4, "four bytes left");
  check(out.ok(), "not overflowed yet");

  out.text("abcd");
  checkEqual(out.length(), 6, "buffer exactly full");
  check(out.ok(), "filling the buffer exactly is not an overflow");

  out.text("e");
  check(out.overflow(), "one byte past the end overflows");
  checkEqual(out.length(), 6, "the overflowing write is not partially applied");

  // A partial command is worse than none, so a command that does not fit whole is
  // still an overflow and the flag survives until reset().
  out.reset();
  check(out.ok(), "reset clears the flag");
  out.text("abcde");
  out.cut();
  check(out.overflow(), "a command that does not fit overflows");

  // A command whose first bytes fit and whose last do not leaves those first bytes
  // in the buffer. The flag is the only thing standing between that and a printer
  // reading the start of a command it will never see the end of.
  out.reset();
  out.text("abcde");
  out.init();
  check(out.overflow(), "a command cut off after its first byte overflows");
  checkEqual(out.length(), 6, "the bytes that fitted are still there, which is why ok() is checked");
}

// -- The receipt content ---------------------------------------------------

void testReceipt()
{
  uint8_t buffer[printer::PRINT_BUFFER_SIZE] = {};
  escpos::Builder out(buffer, sizeof(buffer));

  receipt::buildReceipt(out, true, 12345678);
  check(out.ok(), "the Japanese receipt fits in the shipped print buffer");
  check(out.length() > 200, "the receipt is not empty");

  // Kanji mode has to be left off at the end, and every FS & needs its FS .:
  // with kanji mode still on, the ASCII of the *next* receipt is misread.
  const size_t on = countSequence(out.data(), out.length(), {0x1c, '&'});
  const size_t off = countSequence(out.data(), out.length(), {0x1c, '.'});
  checkEqual(on, off, "kanji mode is switched off as many times as it is switched on");
  check(on > 0, "the Japanese receipt does switch kanji mode on");

  // Shift-JIS, not JIS: the byte arrays in ReceiptJa.hpp are Shift-JIS.
  check(contains(out.data(), out.length(), {0x1c, 'C', 0x01}),
       "the receipt selects the Shift-JIS kanji code system");
  checkEqual(out.data()[0], 0x1b, "a receipt starts with ESC @");
  checkEqual(out.data()[1], '@', "a receipt starts with ESC @");

  // With cut requested the slip ends with the cut, and it is the feeding variant.
  const size_t length = out.length();
  checkBytes(out.data() + length - 4, 4, {0x1d, 'V', 66, 0x50}, "a cut receipt ends with GS V 66");

  // Without it, the slip ends with a feed instead so the text clears the head.
  out.reset();
  receipt::buildReceipt(out, false, 1);
  check(out.ok(), "the uncut receipt fits too");
  check(!contains(out.data(), out.length(), {0x1d, 'V'}),
       "no cut command is emitted when cutting is off");
  checkBytes(out.data() + out.length() - 3, 3, {0x1b, 'd', 0x04},
             "an uncut receipt ends with a feed");

  // The ASCII slip must not touch kanji mode at all: it is the fallback for a
  // printer that has no two-byte font.
  out.reset();
  receipt::buildAsciiTest(out, false);
  check(out.ok(), "the ASCII slip fits");
  checkEqual(countSequence(out.data(), out.length(), {0x1c, '&'}), 0,
             "the ASCII slip never enables kanji mode");
  for (size_t i = 0; i < out.length(); i++)
  {
    if (out.data()[i] >= 0x80)
    {
      check(false, "the ASCII slip contains only single-byte characters");
      break;
    }
  }
}

} // namespace

int main()
{
  testRequestConstants();
  testPortStatus();
  testDeviceId();
  testDeviceIdField();
  testBasicCommands();
  testSize();
  testTextAndFeed();
  testCut();
  testBarcode();
  testQr();
  testRaster();
  testRealtimeStatus();
  testOverflow();
  testReceipt();

  if (failures == 0)
  {
    printf("all escpos tests passed\n");
    return 0;
  }
  printf("%d escpos test(s) failed\n", failures);
  return 1;
}
