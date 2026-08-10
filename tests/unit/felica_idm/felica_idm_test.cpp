// Host tests for the FeliCa and RC-S300 protocol layers used by
// examples/Ccid/EspUsbHostCcidFelicaIdm. Both headers are pure byte formatting
// with no Arduino / USB dependencies, so they are included directly and the
// example's own code is exercised.
//
// The anchor of the RC-S300 half is tests/probe/rcs300_felica: every pseudo APDU
// and every response below is a byte sequence that was sent to, or came back
// from, a real reader.

#include "FelicaProtocol.hpp"
#include "Rcs300Protocol.hpp"

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

void checkBytes(const uint8_t *actual,
                size_t actualLength,
                const std::vector<uint8_t> &expected,
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

// ---------------------------------------------------------------- FeliCa frames

void testPollingFrame()
{
  uint8_t frame[16] = {};

  // The frame this example exists to send: Polling for the transit system, asking
  // the target to report the system code it answers with.
  size_t length = felica::pollingFrame(frame, sizeof(frame), felica::SYSTEM_CODE_TRANSIT);
  checkBytes(frame, length, {0x06, 0x00, 0x00, 0x03, 0x01, 0x00}, "polling frame for 0x0003");

  // The wildcard, which is what a reader polls with on its own.
  length = felica::pollingFrame(frame, sizeof(frame), felica::SYSTEM_CODE_ANY);
  checkBytes(frame, length, {0x06, 0x00, 0xff, 0xff, 0x01, 0x00}, "polling frame for 0xffff");

  // Request Code and Time Slot are the caller's.
  length = felica::pollingFrame(frame, sizeof(frame), 0x12fc, felica::REQUEST_NONE, 0x03);
  checkBytes(frame, length, {0x06, 0x00, 0x12, 0xfc, 0x00, 0x03}, "polling frame with no request");

  // The length byte counts itself, so it is the frame size and not the payload.
  checkEqual(frame[0], felica::POLLING_FRAME_BYTES, "length byte counts itself");

  checkEqual(felica::pollingFrame(frame, 5, felica::SYSTEM_CODE_ANY), 0, "buffer one byte short");
  checkEqual(felica::pollingFrame(nullptr, sizeof(frame), felica::SYSTEM_CODE_ANY), 0, "null buffer");
}

void testPollingResponse()
{
  // LEN 01 IDm PMm RD, the shape a target answers a Polling with when request
  // data was asked for.
  const std::vector<uint8_t> withRequestData = {0x14, 0x01, 0x01, 0x02, 0x03, 0x04, 0x05,
                                                0x06, 0x07, 0x08, 0x10, 0x11, 0x12, 0x13,
                                                0x14, 0x15, 0x16, 0x17, 0x00, 0x03};
  felica::Target target;
  check(felica::parsePollingResponse(withRequestData.data(), withRequestData.size(), target),
        "polling response with request data parses");
  checkBytes(target.idm, sizeof(target.idm), {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08},
             "IDm");
  checkBytes(target.pmm, sizeof(target.pmm), {0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17},
             "PMm");
  check(target.hasRequestData, "request data reported as present");
  checkEqual(target.requestData, 0x0003, "answering system code");

  // Without request data the frame is two bytes shorter and the field must not be
  // reported as present.
  std::vector<uint8_t> withoutRequestData(withRequestData.begin(), withRequestData.end() - 2);
  withoutRequestData[0] = 0x12;
  check(felica::parsePollingResponse(withoutRequestData.data(), withoutRequestData.size(), target),
        "polling response without request data parses");
  check(!target.hasRequestData, "no request data reported");
  checkEqual(target.requestData, 0, "request data left zero");

  // A reader may hand over a buffer longer than the frame; the declared length is
  // what decides where the frame ends.
  std::vector<uint8_t> trailing = withoutRequestData;
  trailing.push_back(0xaa);
  trailing.push_back(0xbb);
  check(felica::parsePollingResponse(trailing.data(), trailing.size(), target),
        "trailing bytes are ignored");
  check(!target.hasRequestData, "trailing bytes are not read as request data");

  // Rejected inputs.
  check(!felica::parsePollingResponse(nullptr, 20, target), "null response");
  check(!felica::parsePollingResponse(withRequestData.data(), 17, target), "too short to hold IDm");

  std::vector<uint8_t> wrongCode = withRequestData;
  wrongCode[1] = 0x07; // an error response, not a Polling answer
  check(!felica::parsePollingResponse(wrongCode.data(), wrongCode.size(), target),
        "wrong response code");

  std::vector<uint8_t> lengthTooBig = withRequestData;
  lengthTooBig[0] = 0x20;
  check(!felica::parsePollingResponse(lengthTooBig.data(), lengthTooBig.size(), target),
        "declared length beyond a Polling response");

  std::vector<uint8_t> lengthTooSmall = withRequestData;
  lengthTooSmall[0] = 0x10;
  check(!felica::parsePollingResponse(lengthTooSmall.data(), lengthTooSmall.size(), target),
        "declared length too small for IDm and PMm");

  std::vector<uint8_t> declaredPastBuffer = withRequestData;
  check(!felica::parsePollingResponse(declaredPastBuffer.data(), 19, target),
        "declared length past the buffer");
}

// ------------------------------------------------------------ RC-S300 requests

void testPseudoApdus()
{
  uint8_t message[128] = {};

  // The four manage session commands, byte for byte as the probe sent them.
  size_t length = rcs300::manageSession(message, sizeof(message), rcs300::TAG_START_SESSION);
  checkBytes(message, length, {0xff, 0x50, 0x00, 0x00, 0x02, 0x81, 0x00, 0x00}, "start session");
  length = rcs300::manageSession(message, sizeof(message), rcs300::TAG_END_SESSION);
  checkBytes(message, length, {0xff, 0x50, 0x00, 0x00, 0x02, 0x82, 0x00, 0x00}, "end session");
  length = rcs300::manageSession(message, sizeof(message), rcs300::TAG_RF_OFF);
  checkBytes(message, length, {0xff, 0x50, 0x00, 0x00, 0x02, 0x83, 0x00, 0x00}, "RF off");
  length = rcs300::manageSession(message, sizeof(message), rcs300::TAG_RF_ON);
  checkBytes(message, length, {0xff, 0x50, 0x00, 0x00, 0x02, 0x84, 0x00, 0x00}, "RF on");

  // Switch protocol. The two byte value is what the reader accepts (the one byte
  // form was refused with 6700), and 03 00 is the value a Polling is answered
  // under -- the reversed 00 03 is accepted but leaves every Polling unanswered.
  length = rcs300::switchToFelica(message, sizeof(message));
  checkBytes(message, length, {0xff, 0x50, 0x00, 0x02, 0x04, 0x8f, 0x02, 0x03, 0x00, 0x00},
             "switch to FeliCa");

  // A transparent exchange carrying the Polling for the transit system: timeout
  // object, transmit object, frame. 100 ms is 0x000186a0 microseconds, little
  // endian.
  uint8_t frame[felica::POLLING_FRAME_BYTES] = {};
  const size_t frameLength =
      felica::pollingFrame(frame, sizeof(frame), felica::SYSTEM_CODE_TRANSIT);
  length = rcs300::transparentExchange(message, sizeof(message), frame, frameLength, 100000);
  checkBytes(message, length,
             {0xff, 0x50, 0x00, 0x01, 0x0f, 0x5f, 0x46, 0x04, 0xa0, 0x86, 0x01, 0x00, 0x95, 0x06,
              0x06, 0x00, 0x00, 0x03, 0x01, 0x00, 0x00},
             "transparent exchange with the 0x0003 polling");

  // Rejected inputs.
  checkEqual(rcs300::transparentExchange(message, sizeof(message), nullptr, 6, 100000), 0,
             "null frame");
  checkEqual(rcs300::transparentExchange(message, sizeof(message), frame, 0, 100000), 0,
             "empty frame");
  checkEqual(rcs300::command(message, 7, rcs300::P2_MANAGE_SESSION, frame, frameLength), 0,
             "buffer too small for the objects");
}

// ----------------------------------------------------------- RC-S300 responses

void testResponses()
{
  rcs300::Response response;

  // What every accepted command answered with: status object, then SW.
  const std::vector<uint8_t> accepted = {0xc0, 0x03, 0x00, 0x90, 0x00, 0x90, 0x00};
  check(rcs300::parseResponse(accepted.data(), accepted.size(), response), "accepted parses");
  check(response.accepted, "accepted reported as accepted");
  checkEqual(response.result, rcs300::RESULT_OK, "accepted result");
  checkEqual(response.statusWord, 0x9000, "accepted status word");
  check(response.received == nullptr, "accepted carries no received object");

  // Switch protocol to FeliCa: the reader adds a protocol object naming what it
  // selected. 0x08 is the PC/SC protocol bit for FeliCa.
  const std::vector<uint8_t> switched = {0xc0, 0x03, 0x00, 0x90, 0x00,
                                         0x8f, 0x01, 0x08, 0x90, 0x00};
  check(rcs300::parseResponse(switched.data(), switched.size(), response), "switch parses");
  check(response.accepted, "switch accepted");
  checkEqual(response.protocolLength, 1, "protocol object length");
  checkEqual(response.protocol[0], 0x08, "protocol object names FeliCa");

  // An exchange the reader ran with nothing in the field.
  const std::vector<uint8_t> noAnswer = {0xc0, 0x03, 0x02, 0x64, 0x01, 0x90, 0x00};
  check(rcs300::parseResponse(noAnswer.data(), noAnswer.size(), response), "no answer parses");
  check(!response.accepted, "no answer is not accepted");
  checkEqual(response.result, rcs300::RESULT_NO_ANSWER, "no answer result");
  checkEqual(response.statusWord, rcs300::SW_NO_ANSWER, "no answer status word");

  // The refusals the probe collected: RF on outside a session, a malformed
  // exchange, a one byte switch protocol object, an unsupported protocol value.
  const std::vector<std::pair<std::vector<uint8_t>, uint16_t>> refusals = {
      {{0xc0, 0x03, 0x01, 0x63, 0x01, 0x90, 0x00}, 0x6301},
      {{0xc0, 0x03, 0x01, 0x64, 0x01, 0x90, 0x00}, 0x6401},
      {{0xc0, 0x03, 0x01, 0x67, 0x00, 0x90, 0x00}, 0x6700},
      {{0xc0, 0x03, 0x01, 0x6a, 0x81, 0x90, 0x00}, 0x6a81},
  };
  for (const auto &refusal : refusals)
  {
    check(rcs300::parseResponse(refusal.first.data(), refusal.first.size(), response),
          "refusal parses");
    check(!response.accepted, "refusal is not accepted");
    checkEqual(response.result, rcs300::RESULT_ERROR, "refusal result");
    checkEqual(response.statusWord, refusal.second, "refusal status word");
  }

  // A successful exchange, in the shape a real reader answered one with: the
  // status object, then 92 01 00 and 96 02 00 00, then the target's frame in a
  // received object. The frame here is a Polling answer, so this is the whole path
  // from response bytes to an IDm. The IDm and PMm are made up; only the framing
  // is from the measurement.
  const std::vector<uint8_t> exchanged = {0xc0, 0x03, 0x00, 0x90, 0x00, 0x92, 0x01, 0x00, 0x96,
                                          0x02, 0x00, 0x00, 0x97, 0x14, 0x14, 0x01, 0x01, 0x02,
                                          0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x10, 0x11, 0x12,
                                          0x13, 0x14, 0x15, 0x16, 0x17, 0x00, 0x03, 0x90, 0x00};
  check(rcs300::parseResponse(exchanged.data(), exchanged.size(), response), "exchange parses");
  check(response.accepted, "exchange accepted");
  check(response.received != nullptr, "exchange carries a received object");
  checkEqual(response.receivedLength, 0x14, "received object length");
  felica::Target target;
  check(felica::parsePollingResponse(response.received, response.receivedLength, target),
        "received object holds a Polling answer");
  checkBytes(target.idm, sizeof(target.idm), {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08},
             "IDm out of the received object");
  checkEqual(target.requestData, 0x0003, "answering system code out of the received object");

  // A two byte tag in a response must not be read as a one byte one: 5F46 is the
  // only such tag in this dialect.
  const std::vector<uint8_t> twoByteTag = {0x5f, 0x46, 0x04, 0xa0, 0x86, 0x01, 0x00,
                                           0xc0, 0x03, 0x00, 0x90, 0x00, 0x90, 0x00};
  check(rcs300::parseResponse(twoByteTag.data(), twoByteTag.size(), response),
        "response with a two byte tag parses");
  check(response.accepted, "response with a two byte tag accepted");

  // Rejected inputs.
  check(!rcs300::parseResponse(nullptr, 7, response), "null response");
  check(!rcs300::parseResponse(accepted.data(), 1, response), "response shorter than a status word");

  // No status object at all: not the shape this dialect answers in.
  const std::vector<uint8_t> noStatus = {0x8f, 0x01, 0x08, 0x90, 0x00};
  check(!rcs300::parseResponse(noStatus.data(), noStatus.size(), response), "no status object");

  // An object whose length runs past the end of the response.
  const std::vector<uint8_t> overrun = {0xc0, 0x03, 0x00, 0x90, 0x00, 0x97, 0x20, 0x01, 0x90, 0x00};
  check(!rcs300::parseResponse(overrun.data(), overrun.size(), response), "object length overruns");

  // A status object too short to hold a result and a status word.
  const std::vector<uint8_t> shortStatus = {0xc0, 0x02, 0x00, 0x90, 0x90, 0x00};
  check(!rcs300::parseResponse(shortStatus.data(), shortStatus.size(), response),
        "status object too short");
}

} // namespace

int main()
{
  testPollingFrame();
  testPollingResponse();
  testPseudoApdus();
  testResponses();

  if (failures != 0)
  {
    printf("%d check(s) failed\n", failures);
    return 1;
  }
  printf("all FeliCa IDm checks passed\n");
  return 0;
}
