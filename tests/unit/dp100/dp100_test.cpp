// Host tests for the ALIENTEK DP100 frame layer used by
// examples/HID/EspUsbHostDp100Power. The header under test is pure byte
// formatting with no Arduino / USB dependencies, so it is included directly and
// the production code itself is exercised.
//
// The response vectors are reports captured from a real DP100 by
// tests/probe/dp100, so the field offsets are pinned to hardware rather than to
// a reading of someone's protocol notes.

#include "Dp100Protocol.hpp"

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

void checkEqual(long actual, long expected, const char *what)
{
  if (actual != expected)
  {
    printf("FAIL: %s (actual=%ld, expected=%ld)\n", what, actual, expected);
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

std::vector<uint8_t> fromHex(const char *text)
{
  std::vector<uint8_t> out;
  int high = -1;
  for (const char *p = text; *p; p++)
  {
    int digit;
    if (*p >= '0' && *p <= '9')
    {
      digit = *p - '0';
    }
    else if (*p >= 'a' && *p <= 'f')
    {
      digit = *p - 'a' + 10;
    }
    else
    {
      continue;
    }
    if (high < 0)
    {
      high = digit;
      continue;
    }
    out.push_back(static_cast<uint8_t>((high << 4) | digit));
    high = -1;
  }
  return out;
}

// A second CRC-16/MODBUS, written from the table-driven form instead of bit by
// bit, so a shared mistake in the production implementation cannot hide.
uint16_t referenceCrc(const uint8_t *data, size_t length)
{
  static uint16_t table[256];
  static bool built = false;
  if (!built)
  {
    for (int i = 0; i < 256; i++)
    {
      uint16_t value = static_cast<uint16_t>(i);
      for (int bit = 0; bit < 8; bit++)
      {
        value = (value & 1) ? static_cast<uint16_t>((value >> 1) ^ 0xa001) : static_cast<uint16_t>(value >> 1);
      }
      table[i] = value;
    }
    built = true;
  }
  uint16_t crc = 0xffff;
  for (size_t i = 0; i < length; i++)
  {
    crc = static_cast<uint16_t>((crc >> 8) ^ table[(crc ^ data[i]) & 0xff]);
  }
  return crc;
}

void testCrc()
{
  // The canonical MODBUS check value: "123456789" -> 0x4b37.
  const uint8_t check123456789[] = "123456789";
  checkEqual(dp100::crc16(check123456789, 9), 0x4b37, "CRC-16/MODBUS check value");
  checkEqual(dp100::crc16(nullptr, 0), 0xffff, "CRC of nothing is the init value");

  // Against the independent implementation, over every length up to a full frame
  // and with a pattern that exercises all byte values.
  uint8_t data[dp100::REPORT_SIZE];
  for (size_t i = 0; i < sizeof(data); i++)
  {
    data[i] = static_cast<uint8_t>(i * 7 + 3);
  }
  for (size_t length = 0; length <= sizeof(data); length++)
  {
    if (dp100::crc16(data, length) != referenceCrc(data, length))
    {
      printf("FAIL: CRC differs from the reference at length %zu\n", length);
      failures++;
      return;
    }
  }
}

void testEncodeRequest()
{
  uint8_t report[dp100::REPORT_SIZE];
  memset(report, 0xaa, sizeof(report));
  checkEqual(static_cast<long>(dp100::encodeRequest(report, sizeof(report), dp100::OP_DEVICE_INFO)),
             dp100::REPORT_SIZE, "a request is a whole report");

  // The bytes a DEVICE_INFO request goes out as. The CRC covers the header only,
  // there being no data.
  std::vector<uint8_t> expected(dp100::REPORT_SIZE, 0x00);
  expected[0] = 0xfb;
  expected[1] = 0x10;
  expected[2] = 0x00;
  expected[3] = 0x00;
  const uint16_t crc = referenceCrc(expected.data(), 4);
  expected[4] = static_cast<uint8_t>(crc & 0xff);
  expected[5] = static_cast<uint8_t>(crc >> 8);
  checkBytes(report, dp100::REPORT_SIZE, expected, "DEVICE_INFO request");

  // With data, the CRC moves to sit right after it and the rest stays zero.
  const uint8_t data[] = {0x01, 0x02, 0x03};
  dp100::encodeRequest(report, sizeof(report), dp100::OP_BASIC_INFO, data, sizeof(data));
  checkEqual(report[3], 3, "length field");
  checkEqual(report[4], 0x01, "first data byte");
  checkEqual(report[6], 0x03, "last data byte");
  const uint16_t dataCrc = referenceCrc(report, 7);
  checkEqual(report[7], dataCrc & 0xff, "CRC low after the data");
  checkEqual(report[8], dataCrc >> 8, "CRC high after the data");
  checkEqual(report[9], 0x00, "padding after the CRC");

  // Rejections.
  checkEqual(static_cast<long>(dp100::encodeRequest(report, dp100::REPORT_SIZE - 1, dp100::OP_DEVICE_INFO)),
             0, "a buffer smaller than a report is refused");
  checkEqual(static_cast<long>(dp100::encodeRequest(report, sizeof(report), dp100::OP_BASIC_INFO,
                                                    nullptr, 4)),
             0, "null data with a length is refused");
  uint8_t oversized[dp100::REPORT_SIZE] = {};
  checkEqual(static_cast<long>(dp100::encodeRequest(report, sizeof(report), dp100::OP_BASIC_INFO,
                                                    oversized, dp100::MAX_DATA + 1)),
             0, "data that leaves no room for the CRC is refused");
}

// Captured from a real DP100 (tests/probe/dp100). 40-byte DEVICE_INFO body.
const char *REAL_DEVICE_INFO =
    "fa10002841544b2d445031303000ffffffffffff0e000e000b00aa00"
    "c7819d000040041622a75005e8070c022c52"
    "000000000000000000000000000000000000000000000000000000";

// Captured from a real DP100, output off, unloaded. 16-byte BASIC_INFO body.
const char *REAL_BASIC_INFO =
    "fa300010802f00000000182e2a012401cb13020096f4"
    "0e000b00aa00c7819d000040041622a75005e8070c022c52000000000000000000000000000000000000";

// Captured from a real DP100: the answer to a frame with a wrong CRC. A one-byte
// body of 0x00 is the failure status.
const char *REAL_FAILURE = "fa10000100f944";
// Captured from a real DP100: the answer to an accepted BASIC_SET. Same shape, but
// the status byte is 0x01.
const char *REAL_SUCCESS = "fa350001013388";
// Captured from a real DP100: BASIC_SET read of index 0 (`35 80`), the setpoint the
// supply was found running from.
const char *REAL_BASIC_SET = "fa35000a0001a00fb80b2477ba13f3c0";

void testDecodeResponse()
{
  const std::vector<uint8_t> report = fromHex(REAL_DEVICE_INFO);
  check(report.size() >= dp100::REPORT_SIZE, "captured report is a full report");

  dp100::Response response;
  check(dp100::decodeResponse(report.data(), report.size(), response),
        "a captured DEVICE_INFO report decodes");
  checkEqual(response.opcode, dp100::OP_DEVICE_INFO, "opcode");
  checkEqual(static_cast<long>(response.length), 40, "body length");
  check(!dp100::isStatus(response), "a payload is not a status");

  // Each of these means the report cannot be trusted.
  check(!dp100::decodeResponse(report.data(), 5, response), "a short read is refused");
  check(!dp100::decodeResponse(nullptr, 64, response), "null is refused");

  std::vector<uint8_t> wrongDirection = report;
  wrongDirection[0] = dp100::DIRECTION_HOST_TO_DEVICE;
  check(!dp100::decodeResponse(wrongDirection.data(), wrongDirection.size(), response),
        "the host->device direction byte is refused on a response");

  std::vector<uint8_t> badCrc = report;
  badCrc[44] ^= 0x01;
  check(!dp100::decodeResponse(badCrc.data(), badCrc.size(), response), "a bad CRC is refused");

  std::vector<uint8_t> corruptBody = report;
  corruptBody[10] ^= 0xff;
  check(!dp100::decodeResponse(corruptBody.data(), corruptBody.size(), response),
        "a corrupted body fails the CRC");

  // A length field that runs past what arrived, rejected on the length rather
  // than on the checksum: only 40 of the 64 bytes are offered to the decoder.
  std::vector<uint8_t> overrun(report.begin(), report.begin() + dp100::REPORT_SIZE);
  overrun[3] = 63;
  check(!dp100::decodeResponse(overrun.data(), 40, response),
        "a length past the end of the read is refused");

  // The status the device answers a frame it did not accept with. Both statuses are
  // valid frames; what differs is the one byte inside.
  const std::vector<uint8_t> failure = fromHex(REAL_FAILURE);
  check(dp100::decodeResponse(failure.data(), failure.size(), response),
        "the failure report itself is a valid frame");
  checkEqual(static_cast<long>(response.length), 1, "status body is one byte");
  check(dp100::isStatus(response), "a one-byte body is a status");
  check(dp100::isFailure(response), "0x00 is failure");
  check(!dp100::isSuccess(response), "0x00 is not success");

  dp100::DeviceInfo info;
  check(!dp100::decodeDeviceInfo(response, info), "a status is not a DEVICE_INFO payload");

  const std::vector<uint8_t> success = fromHex(REAL_SUCCESS);
  check(dp100::decodeResponse(success.data(), success.size(), response), "success decodes");
  checkEqual(response.opcode, dp100::OP_BASIC_SET, "success came from BASIC_SET");
  check(dp100::isSuccess(response), "0x01 is success");
  check(!dp100::isFailure(response), "0x01 is not failure");

  // A payload is neither.
  const std::vector<uint8_t> payload = fromHex(REAL_BASIC_INFO);
  check(dp100::decodeResponse(payload.data(), payload.size(), response), "payload decodes");
  check(!dp100::isStatus(response), "a 16-byte body is not a status");
}

void testDeviceInfo()
{
  const std::vector<uint8_t> report = fromHex(REAL_DEVICE_INFO);
  dp100::Response response;
  check(dp100::decodeResponse(report.data(), report.size(), response), "decode");

  dp100::DeviceInfo info;
  check(dp100::decodeDeviceInfo(response, info), "DEVICE_INFO decodes");
  check(strcmp(info.type, "ATK-DP100") == 0, "device type, 0xff padding stripped");
  checkEqual(info.hardwareVersion, 14, "hardware version");
  checkEqual(info.applicationVersion, 14, "application version");
  checkEqual(info.bootVersion, 11, "boot version");
  checkEqual(info.runArea, 0x00aa, "run area");
  checkEqual(info.year, 2024, "year");
  checkEqual(info.month, 12, "month");
  checkEqual(info.day, 2, "day");
  const std::vector<uint8_t> serial = fromHex("c7819d000040041622a75005");
  checkBytes(info.serial, sizeof(info.serial), serial, "serial bytes");

  // A body one byte short of the layout is refused rather than read partially.
  dp100::Response truncated = response;
  truncated.length = dp100::DEVICE_INFO_SIZE - 1;
  check(!dp100::decodeDeviceInfo(truncated, info), "a truncated DEVICE_INFO is refused");

  dp100::Response wrongOpcode = response;
  wrongOpcode.opcode = dp100::OP_BASIC_INFO;
  check(!dp100::decodeDeviceInfo(wrongOpcode, info), "a different opcode is refused");
}

void testBasicInfo()
{
  const std::vector<uint8_t> report = fromHex(REAL_BASIC_INFO);
  dp100::Response response;
  check(dp100::decodeResponse(report.data(), report.size(), response), "decode");
  checkEqual(response.opcode, dp100::OP_BASIC_INFO, "opcode");

  dp100::BasicInfo info;
  check(dp100::decodeBasicInfo(response, info), "BASIC_INFO decodes");
  // The values this capture was taken with: ~12.16 V in, output off, ~29 degC.
  checkEqual(info.inputMillivolts, 12160, "input mV");
  checkEqual(info.outputMillivolts, 0, "output mV, output off");
  checkEqual(info.outputMilliamps, 0, "output mA, output off");
  checkEqual(info.maxOutputMillivolts, 11800, "max output mV for this input");
  checkEqual(info.temperature1Deci, 298, "temperature 1, 0.1 degC");
  checkEqual(info.temperature2Deci, 292, "temperature 2, 0.1 degC");
  checkEqual(info.rail5vMillivolts, 5067, "internal 5 V rail, mV");
  checkEqual(info.outputMode, 2, "output mode");
  checkEqual(info.workStatus, 0, "work status");

  dp100::Response truncated = response;
  truncated.length = dp100::BASIC_INFO_SIZE - 1;
  check(!dp100::decodeBasicInfo(truncated, info), "a truncated BASIC_INFO is refused");
}

// The setpoint frame, against the report a real DP100 answered a read with, and
// against the flags a write needs. The index flags matter: a write to a bare index
// is answered with success and then silently ignored, which is exactly the kind of
// mistake a test can catch and a bench cannot.
void testBasicSet()
{
  const std::vector<uint8_t> report = fromHex(REAL_BASIC_SET);
  dp100::Response response;
  check(dp100::decodeResponse(report.data(), report.size(), response), "decode");
  checkEqual(response.opcode, dp100::OP_BASIC_SET, "opcode");

  dp100::BasicSet set;
  check(dp100::decodeBasicSet(response, set), "BASIC_SET decodes");
  checkEqual(set.index, 0, "index is echoed without the request's flag");
  checkEqual(set.state, dp100::STATE_OUTPUT_ON, "state, the output enable");
  checkEqual(set.voltageMillivolts, 4000, "4.000 V setpoint");
  checkEqual(set.currentMilliamps, 3000, "3.000 A setpoint");
  checkEqual(set.overVoltageMillivolts, 30500, "OVP 30.5 V, the model's ceiling");
  checkEqual(set.overCurrentMilliamps, 5050, "OCP 5.05 A");

  // A read request is one byte: the index with the read flag.
  uint8_t request[dp100::REPORT_SIZE];
  checkEqual(static_cast<long>(dp100::encodeBasicSetRead(request, sizeof(request), 0)),
             dp100::REPORT_SIZE, "a read request is a whole report");
  checkEqual(request[1], dp100::OP_BASIC_SET, "opcode");
  checkEqual(request[3], 1, "one byte of data");
  checkEqual(request[4], 0x80, "index 0 carries the read flag");
  dp100::encodeBasicSetRead(request, sizeof(request), 2);
  checkEqual(request[4], 0x82, "index 2 carries the read flag");

  // A write request is ten bytes, and the index carries the write flag instead.
  dp100::BasicSet write;
  write.index = 0;
  write.state = dp100::STATE_OUTPUT_ON;
  write.voltageMillivolts = 5000;
  write.currentMilliamps = 500;
  write.overVoltageMillivolts = 30500;
  write.overCurrentMilliamps = 5050;
  checkEqual(static_cast<long>(dp100::encodeBasicSet(request, sizeof(request), write)),
             dp100::REPORT_SIZE, "a write is a whole report");
  checkEqual(request[3], dp100::BASIC_SET_SIZE, "ten bytes of data");
  checkEqual(request[4], 0x20, "index 0 carries the write flag, not the read flag");
  checkEqual(request[5], dp100::STATE_OUTPUT_ON, "state");
  checkEqual(request[6], 5000 & 0xff, "voltage low byte");
  checkEqual(request[7], 5000 >> 8, "voltage high byte");
  checkEqual(request[8], 500 & 0xff, "current low byte");
  checkEqual(dp100::readUint16Le(request + 10), 30500, "OVP carried through");
  checkEqual(dp100::readUint16Le(request + 12), 5050, "OCP carried through");
  write.index = 3;
  dp100::encodeBasicSet(request, sizeof(request), write);
  checkEqual(request[4], 0x23, "index 3 with the write flag");

  // Round trip: turn the request into a response frame and read it back.
  uint8_t asResponse[dp100::REPORT_SIZE];
  memcpy(asResponse, request, sizeof(asResponse));
  asResponse[0] = dp100::DIRECTION_DEVICE_TO_HOST;
  const uint16_t crc = referenceCrc(asResponse, 4 + dp100::BASIC_SET_SIZE);
  asResponse[4 + dp100::BASIC_SET_SIZE] = static_cast<uint8_t>(crc & 0xff);
  asResponse[5 + dp100::BASIC_SET_SIZE] = static_cast<uint8_t>(crc >> 8);
  check(dp100::decodeResponse(asResponse, sizeof(asResponse), response), "decode");
  dp100::BasicSet readBack;
  check(dp100::decodeBasicSet(response, readBack), "BASIC_SET decodes");
  checkEqual(readBack.voltageMillivolts, 5000, "voltage round trip");
  checkEqual(readBack.currentMilliamps, 500, "current round trip");
  checkEqual(readBack.state, dp100::STATE_OUTPUT_ON, "state round trip");

  dp100::Response truncated = response;
  truncated.length = dp100::BASIC_SET_SIZE - 1;
  check(!dp100::decodeBasicSet(truncated, readBack), "a truncated BASIC_SET is refused");
}

} // namespace

int main()
{
  testCrc();
  testEncodeRequest();
  testDecodeResponse();
  testDeviceInfo();
  testBasicInfo();
  testBasicSet();

  if (failures != 0)
  {
    printf("%d check(s) failed\n", failures);
    return 1;
  }
  printf("all DP100 protocol checks passed\n");
  return 0;
}
