// Host tests for the USBTMC message layer used by
// examples/Vendor/EspUsbHostUsbtmcScpi. The header under test is pure byte
// formatting with no Arduino / USB dependencies, so it is included directly and
// the production code itself is exercised.

#include "UsbtmcProtocol.hpp"

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

// USBTMC 1.0 table 3: every message ends on a 4-byte boundary.
void testPadding()
{
  checkEqual(usbtmc::paddedLength(0), 0, "padded(0)");
  checkEqual(usbtmc::paddedLength(1), 4, "padded(1)");
  checkEqual(usbtmc::paddedLength(4), 4, "padded(4)");
  checkEqual(usbtmc::paddedLength(5), 8, "padded(5)");
  // The header alone is already aligned, so a payload's padding is independent
  // of it.
  checkEqual(usbtmc::paddedLength(usbtmc::HEADER_SIZE), usbtmc::HEADER_SIZE, "padded(header)");
  checkEqual(usbtmc::paddedLength(usbtmc::HEADER_SIZE + 5), usbtmc::HEADER_SIZE + 8,
             "padded(header + 5)");
}

// bTag must never be 0 and must differ from its predecessor, so the sequence
// wraps 255 -> 1 rather than 255 -> 0.
void testTagSequence()
{
  checkEqual(usbtmc::nextTag(0), 1, "nextTag(0)");
  checkEqual(usbtmc::nextTag(1), 2, "nextTag(1)");
  checkEqual(usbtmc::nextTag(254), 255, "nextTag(254)");
  checkEqual(usbtmc::nextTag(255), 1, "nextTag(255)");

  uint8_t tag = 0;
  for (int i = 0; i < 1000; i++)
  {
    const uint8_t previous = tag;
    tag = usbtmc::nextTag(tag);
    check(tag != 0, "tag never 0");
    check(tag != previous, "tag differs from the previous one");
  }
}

// "*IDN?" is 5 bytes, so the message is 12 + 5 padded to 20.
void testDevDepMsgOut()
{
  uint8_t buffer[32];
  memset(buffer, 0xaa, sizeof(buffer));
  const char *command = "*IDN?";
  const size_t length =
      usbtmc::encodeDevDepMsgOut(buffer, sizeof(buffer), 0x01,
                                 reinterpret_cast<const uint8_t *>(command), strlen(command));
  checkBytes(buffer, length,
             {
                 0x01,                   // MsgID DEV_DEP_MSG_OUT
                 0x01,                   // bTag
                 0xfe,                   // bTagInverse
                 0x00,                   // reserved
                 0x05, 0x00, 0x00, 0x00, // TransferSize
                 0x01,                   // bmTransferAttributes: EOM
                 0x00, 0x00, 0x00,       // reserved
                 '*', 'I', 'D', 'N', '?',
                 0x00, 0x00, 0x00 // alignment padding
             },
             "DEV_DEP_MSG_OUT(*IDN?)");

  // EOM clear, for a command split over several messages.
  const size_t partial =
      usbtmc::encodeDevDepMsgOut(buffer, sizeof(buffer), 0x7f,
                                 reinterpret_cast<const uint8_t *>("AB"), 2, false);
  checkEqual(partial, 16, "partial message length");
  checkEqual(buffer[1], 0x7f, "bTag");
  checkEqual(buffer[2], 0x80, "bTagInverse");
  checkEqual(buffer[8], 0x00, "EOM clear");

  // A 4-byte payload needs no padding: 12 + 4 is already aligned.
  checkEqual(usbtmc::encodeDevDepMsgOut(buffer, sizeof(buffer), 0x02,
                                        reinterpret_cast<const uint8_t *>("*CLS"), 4),
             16, "aligned payload is not padded");

  // Rejections: bTag 0 is reserved, and the buffer must hold the padded length.
  checkEqual(usbtmc::encodeDevDepMsgOut(buffer, sizeof(buffer), 0x00,
                                        reinterpret_cast<const uint8_t *>("A"), 1),
             0, "bTag 0 refused");
  checkEqual(usbtmc::encodeDevDepMsgOut(buffer, 16, 0x01,
                                        reinterpret_cast<const uint8_t *>("*IDN?"), 5),
             0, "capacity below the padded length refused");
  checkEqual(usbtmc::encodeDevDepMsgOut(buffer, sizeof(buffer), 0x01, nullptr, 4), 0,
             "null payload with a length refused");
}

void testRequestDevDepMsgIn()
{
  uint8_t buffer[32];
  memset(buffer, 0xaa, sizeof(buffer));
  const size_t length = usbtmc::encodeRequestDevDepMsgIn(buffer, sizeof(buffer), 0x02, 496);
  checkBytes(buffer, length,
             {
                 0x02,                   // MsgID REQUEST_DEV_DEP_MSG_IN
                 0x02,                   // bTag
                 0xfd,                   // bTagInverse
                 0x00,                   // reserved
                 0xf0, 0x01, 0x00, 0x00, // TransferSize 496, little endian
                 0x00,                   // bmTransferAttributes: TermChar off
                 0x00,                   // TermChar
                 0x00, 0x00              // reserved
             },
             "REQUEST_DEV_DEP_MSG_IN(496)");

  usbtmc::encodeRequestDevDepMsgIn(buffer, sizeof(buffer), 0x03, 64, true, '\n');
  checkEqual(buffer[8], 0x02, "TermCharEnabled");
  checkEqual(buffer[9], '\n', "TermChar");

  checkEqual(usbtmc::encodeRequestDevDepMsgIn(buffer, sizeof(buffer), 0, 64), 0, "bTag 0 refused");
  checkEqual(usbtmc::encodeRequestDevDepMsgIn(buffer, 11, 1, 64), 0, "short buffer refused");
}

// A response header built by hand from the field layout, so a shared bug in the
// encoder cannot hide the decoder's.
std::vector<uint8_t> makeResponse(uint8_t bTag, uint8_t bTagInverse, uint32_t transferSize,
                                  uint8_t attributes, size_t payloadBytes)
{
  std::vector<uint8_t> data = {usbtmc::MSG_DEV_DEP_MSG_IN,
                               bTag,
                               bTagInverse,
                               0x00,
                               static_cast<uint8_t>(transferSize),
                               static_cast<uint8_t>(transferSize >> 8),
                               static_cast<uint8_t>(transferSize >> 16),
                               static_cast<uint8_t>(transferSize >> 24),
                               attributes,
                               0x00,
                               0x00,
                               0x00};
  data.resize(usbtmc::HEADER_SIZE + payloadBytes, 'x');
  return data;
}

void testDecodeMsgInHeader()
{
  usbtmc::MsgInHeader header;

  auto good = makeResponse(0x05, 0xfa, 7, usbtmc::ATTR_END_OF_MESSAGE, 8);
  check(usbtmc::decodeMsgInHeader(good.data(), good.size(), header), "decode accepts a good header");
  checkEqual(header.bTag, 0x05, "decoded bTag");
  checkEqual(header.transferSize, 7, "decoded TransferSize");
  check(header.endOfMessage, "decoded EOM");
  check(!header.termCharDetected, "TermChar not detected");

  auto termChar = makeResponse(0x05, 0xfa, 4, usbtmc::ATTR_END_OF_MESSAGE | usbtmc::ATTR_TERM_CHAR_DETECTED, 4);
  check(usbtmc::decodeMsgInHeader(termChar.data(), termChar.size(), header) && header.termCharDetected,
        "TermChar detected bit");

  auto notLast = makeResponse(0x06, 0xf9, 4, 0, 4);
  check(usbtmc::decodeMsgInHeader(notLast.data(), notLast.size(), header) && !header.endOfMessage,
        "EOM clear means the response continues");

  // Each of these means the bulk IN stream is out of sync, and the caller has to
  // abort rather than use the payload.
  check(!usbtmc::decodeMsgInHeader(good.data(), usbtmc::HEADER_SIZE - 1, header),
        "short read refused");
  check(!usbtmc::decodeMsgInHeader(nullptr, 64, header), "null refused");

  auto wrongMsgId = good;
  wrongMsgId[0] = usbtmc::MSG_DEV_DEP_MSG_OUT;
  check(!usbtmc::decodeMsgInHeader(wrongMsgId.data(), wrongMsgId.size(), header),
        "wrong MsgID refused");

  auto badInverse = makeResponse(0x05, 0xfb, 7, usbtmc::ATTR_END_OF_MESSAGE, 8);
  check(!usbtmc::decodeMsgInHeader(badInverse.data(), badInverse.size(), header),
        "bTagInverse mismatch refused");

  auto zeroTag = makeResponse(0x00, 0xff, 7, usbtmc::ATTR_END_OF_MESSAGE, 8);
  check(!usbtmc::decodeMsgInHeader(zeroTag.data(), zeroTag.size(), header), "bTag 0 refused");

  // TransferSize larger than what arrived: trusting it would read past the
  // buffer.
  auto overrun = makeResponse(0x05, 0xfa, 64, usbtmc::ATTR_END_OF_MESSAGE, 8);
  check(!usbtmc::decodeMsgInHeader(overrun.data(), overrun.size(), header),
        "TransferSize past the end of the read refused");

  // Exactly filling the read is fine.
  auto exact = makeResponse(0x05, 0xfa, 8, usbtmc::ATTR_END_OF_MESSAGE, 8);
  check(usbtmc::decodeMsgInHeader(exact.data(), exact.size(), header),
        "TransferSize equal to the payload accepted");
}

void testCapabilities()
{
  uint8_t data[usbtmc::CAPABILITIES_SIZE] = {};
  data[0] = usbtmc::STATUS_SUCCESS;
  data[2] = 0x00; // bcdUSBTMC 1.00, little endian
  data[3] = 0x01;
  data[4] = 0x04;  // INDICATOR_PULSE supported, not listen/talk-only
  data[5] = 0x01;  // TermChar supported
  data[12] = 0x00; // bcdUSB488 1.00, little endian
  data[13] = 0x01;
  data[14] = 0x04; // USB488.2, no TRIGGER, no REN_CONTROL
  data[15] = 0x0c; // SCPI + SR1

  usbtmc::Capabilities capabilities;
  check(usbtmc::decodeCapabilities(data, sizeof(data), capabilities), "capabilities decoded");
  checkEqual(capabilities.bcdUsbtmc, 0x0100, "bcdUSBTMC");
  checkEqual(capabilities.bcdUsb488, 0x0100, "bcdUSB488");
  check(capabilities.indicatorPulse, "INDICATOR_PULSE");
  check(!capabilities.listenOnly && !capabilities.talkOnly, "bidirectional");
  check(capabilities.termCharSupported, "TermChar");
  check(capabilities.usb488_2, "USB488.2");
  check(!capabilities.trigger && !capabilities.remoteLocalControl, "no TRIGGER / REN_CONTROL");
  check(capabilities.scpi, "SCPI bit");
  check(capabilities.sr1, "SR1");
  check(!capabilities.rl1 && !capabilities.dt1, "no RL1 / DT1");

  // A FAILED status is reported as a decode failure, but the fields are still
  // filled so a caller can log them.
  data[0] = usbtmc::STATUS_FAILED;
  check(!usbtmc::decodeCapabilities(data, sizeof(data), capabilities), "FAILED status refused");
  checkEqual(capabilities.status, usbtmc::STATUS_FAILED, "status kept");

  check(!usbtmc::decodeCapabilities(data, usbtmc::CAPABILITIES_SIZE - 1, capabilities),
        "short capabilities refused");
}

// The bytes a KIKUSUI PMX18-5A actually returns, captured by
// tests/manual/usbtmc_scpi. This pins the USB488 field offsets: they sit at 14
// and 15 because bcdUSB488 occupies 12..13, and reading them two bytes early
// makes an instrument that supports everything look like one that supports
// nothing.
void testCapabilitiesFromRealDevice()
{
  const uint8_t data[usbtmc::CAPABILITIES_SIZE] = {0x01, 0x00, 0x00, 0x01, 0x00, 0x01, 0x00, 0x00,
                                                   0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x07, 0x0f,
                                                   0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
  usbtmc::Capabilities capabilities;
  check(usbtmc::decodeCapabilities(data, sizeof(data), capabilities), "PMX18-5A capabilities decoded");
  checkEqual(capabilities.bcdUsbtmc, 0x0100, "PMX bcdUSBTMC 1.00");
  checkEqual(capabilities.bcdUsb488, 0x0100, "PMX bcdUSB488 1.00");
  check(!capabilities.listenOnly && !capabilities.talkOnly, "PMX is bidirectional");
  check(!capabilities.indicatorPulse, "PMX has no INDICATOR_PULSE");
  check(capabilities.termCharSupported, "PMX supports TermChar");
  check(capabilities.trigger && capabilities.remoteLocalControl && capabilities.usb488_2,
        "PMX interface capabilities 0x07");
  check(capabilities.scpi && capabilities.sr1 && capabilities.rl1 && capabilities.dt1,
        "PMX device capabilities 0x0f: SCPI, SR1, RL1, DT1");
}

void testStatusNames()
{
  check(strcmp(usbtmc::statusName(usbtmc::STATUS_SUCCESS), "SUCCESS") == 0, "SUCCESS name");
  check(strcmp(usbtmc::statusName(usbtmc::STATUS_PENDING), "PENDING") == 0, "PENDING name");
  check(strcmp(usbtmc::statusName(0x55), "unknown") == 0, "unknown status name");
}

// The class request constants are what the control transfers are addressed with;
// a wrong value here fails on the wire in a way that is hard to read back.
void testRequestConstants()
{
  checkEqual(usbtmc::CLASS_REQUEST_IN, 0xa1, "class request IN: class type, interface, device->host");
  checkEqual(usbtmc::CLASS_REQUEST_OUT, 0x21, "class request OUT");
  checkEqual(usbtmc::STANDARD_ENDPOINT_OUT, 0x02, "standard request to an endpoint");
  checkEqual(usbtmc::STANDARD_CLEAR_FEATURE, 0x01, "CLEAR_FEATURE");
  checkEqual(usbtmc::FEATURE_ENDPOINT_HALT, 0x0000, "ENDPOINT_HALT");
  checkEqual(usbtmc::REQ_INITIATE_CLEAR, 5, "INITIATE_CLEAR");
  checkEqual(usbtmc::REQ_GET_CAPABILITIES, 7, "GET_CAPABILITIES");
  checkEqual(usbtmc::REQ_READ_STATUS_BYTE, 128, "READ_STATUS_BYTE");
}

} // namespace

int main()
{
  testPadding();
  testTagSequence();
  testDevDepMsgOut();
  testRequestDevDepMsgIn();
  testDecodeMsgInHeader();
  testCapabilities();
  testCapabilitiesFromRealDevice();
  testStatusNames();
  testRequestConstants();

  if (failures != 0)
  {
    printf("%d check(s) failed\n", failures);
    return 1;
  }
  printf("all USBTMC protocol checks passed\n");
  return 0;
}
