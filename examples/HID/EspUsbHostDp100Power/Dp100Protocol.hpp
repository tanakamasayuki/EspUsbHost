// ALIENTEK DP100 wire format: the 64-byte HID report frame, its CRC-16/MODBUS,
// and the payloads of the read opcodes.
//
// Byte formatting only. No Arduino, no USB - which is what lets
// tests/unit/dp100 compile this header with g++ and test the production code
// rather than a copy of it.
//
// Every field below was confirmed against real hardware by tests/probe/dp100;
// see README.md for the capture and for the specification references.

#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace dp100
{

// The HID endpoints are 64 bytes in both directions and the device expects a full
// report, so a frame is always padded out to this.
static constexpr size_t REPORT_SIZE = 64;
// [direction][opcode][reserved][len] ... [crc lo][crc hi]
static constexpr size_t HEADER_SIZE = 4;
static constexpr size_t CRC_SIZE = 2;
static constexpr size_t MAX_DATA = REPORT_SIZE - HEADER_SIZE - CRC_SIZE;

static constexpr uint8_t DIRECTION_HOST_TO_DEVICE = 0xfb;
static constexpr uint8_t DIRECTION_DEVICE_TO_HOST = 0xfa;

enum OpCode : uint8_t
{
  OP_DEVICE_INFO = 0x10,
  // 0x12..0x15 are the firmware update opcodes. Deliberately not defined: a
  // mistake there leaves the instrument unable to boot.
  OP_BASIC_INFO = 0x30,
  OP_BASIC_SET = 0x35,
  OP_SYSTEM_INFO = 0x40,
  OP_SYSTEM_SET = 0x45,
  OP_SCAN_OUT = 0x50,
  OP_SERIAL_OUT = 0x55,
  OP_DISCONNECT = 0x80,
};

// CRC-16/MODBUS: reflected polynomial 0xa001, init 0xffff, no final xor. Computed
// bit by bit rather than from a table, so there is no lookup data to be wrong
// about on a device where a bad CRC just gets the frame rejected.
inline uint16_t crc16(const uint8_t *data, size_t length)
{
  uint16_t crc = 0xffff;
  for (size_t i = 0; i < length; i++)
  {
    crc ^= data[i];
    for (int bit = 0; bit < 8; bit++)
    {
      crc = (crc & 1) ? static_cast<uint16_t>((crc >> 1) ^ 0xa001) : static_cast<uint16_t>(crc >> 1);
    }
  }
  return crc;
}

inline uint16_t readUint16Le(const uint8_t *data)
{
  return static_cast<uint16_t>(static_cast<uint16_t>(data[0]) | (static_cast<uint16_t>(data[1]) << 8));
}

inline int16_t readInt16Le(const uint8_t *data)
{
  return static_cast<int16_t>(readUint16Le(data));
}

// Builds one request into a full 64-byte report. Returns REPORT_SIZE, or 0 when
// the data does not fit. The whole report is always sent: the device is fed a
// fixed-size HID report and reads the length from the header, so the padding is
// what the endpoint needs rather than something the protocol cares about.
inline size_t encodeRequest(uint8_t *out,
                            size_t capacity,
                            uint8_t opcode,
                            const uint8_t *data = nullptr,
                            size_t dataLength = 0)
{
  if (!out || capacity < REPORT_SIZE || dataLength > MAX_DATA || (dataLength > 0 && !data))
  {
    return 0;
  }
  memset(out, 0, REPORT_SIZE);
  out[0] = DIRECTION_HOST_TO_DEVICE;
  out[1] = opcode;
  out[2] = 0x00;
  out[3] = static_cast<uint8_t>(dataLength);
  if (dataLength > 0)
  {
    memcpy(out + HEADER_SIZE, data, dataLength);
  }
  const uint16_t crc = crc16(out, HEADER_SIZE + dataLength);
  out[HEADER_SIZE + dataLength] = static_cast<uint8_t>(crc & 0xff);
  out[HEADER_SIZE + dataLength + 1] = static_cast<uint8_t>(crc >> 8);
  return REPORT_SIZE;
}

struct Response
{
  uint8_t opcode = 0;
  const uint8_t *data = nullptr;
  size_t length = 0;
};

// Parses a report received on the interrupt IN endpoint. Rejects a short read, a
// wrong direction byte, a length that runs past what arrived, and a CRC mismatch;
// each of those means the bytes cannot be trusted. `data` points into `report`.
inline bool decodeResponse(const uint8_t *report, size_t reportLength, Response &response)
{
  if (!report || reportLength < HEADER_SIZE + CRC_SIZE)
  {
    return false;
  }
  if (report[0] != DIRECTION_DEVICE_TO_HOST)
  {
    return false;
  }
  const size_t length = report[3];
  if (HEADER_SIZE + length + CRC_SIZE > reportLength)
  {
    return false;
  }
  const uint16_t expected = crc16(report, HEADER_SIZE + length);
  const uint16_t actual = readUint16Le(report + HEADER_SIZE + length);
  if (expected != actual)
  {
    return false;
  }
  response.opcode = report[1];
  response.data = report + HEADER_SIZE;
  response.length = length;
  return true;
}

// A one-byte body is a status code rather than a payload: 0x01 is success (what an
// accepted BASIC_SET answers with) and 0x00 is failure (what every wrong CRC
// variant answered with). Anything else is unknown and treated as failure.
static constexpr uint8_t STATUS_OK = 0x01;
static constexpr uint8_t STATUS_FAIL = 0x00;

inline bool isStatus(const Response &response)
{
  return response.length == 1 && response.data != nullptr;
}

// True when the response is a status body reporting success.
inline bool isSuccess(const Response &response)
{
  return isStatus(response) && response.data[0] == STATUS_OK;
}

// True when the device answered but said no. A malformed frame - a wrong CRC, for
// instance - comes back this way rather than as silence, so it is worth telling
// apart from a timeout.
inline bool isFailure(const Response &response)
{
  return isStatus(response) && response.data[0] != STATUS_OK;
}

// DEVICE_INFO (0x10), 40 bytes.
static constexpr size_t DEVICE_INFO_SIZE = 40;
static constexpr size_t DEVICE_TYPE_SIZE = 16;
static constexpr size_t DEVICE_SERIAL_SIZE = 12;

struct DeviceInfo
{
  // Padded with 0xff on the wire; kept here as a NUL-terminated string. Note this
  // is the device's own name ("ATK-DP100"), not the USB product string
  // ("ATK-MDP100").
  char type[DEVICE_TYPE_SIZE + 1] = {};
  uint16_t hardwareVersion = 0;
  uint16_t applicationVersion = 0;
  uint16_t bootVersion = 0;
  uint16_t runArea = 0;
  // Raw bytes, not text, and unrelated to the USB serial string.
  uint8_t serial[DEVICE_SERIAL_SIZE] = {};
  uint16_t year = 0;
  uint8_t month = 0;
  uint8_t day = 0;
};

inline bool decodeDeviceInfo(const Response &response, DeviceInfo &info)
{
  if (response.opcode != OP_DEVICE_INFO || response.length < DEVICE_INFO_SIZE || !response.data)
  {
    return false;
  }
  const uint8_t *data = response.data;
  size_t typeLength = 0;
  while (typeLength < DEVICE_TYPE_SIZE && data[typeLength] != 0x00 && data[typeLength] != 0xff)
  {
    info.type[typeLength] = static_cast<char>(data[typeLength]);
    typeLength++;
  }
  info.type[typeLength] = '\0';
  info.hardwareVersion = readUint16Le(data + 16);
  info.applicationVersion = readUint16Le(data + 18);
  info.bootVersion = readUint16Le(data + 20);
  info.runArea = readUint16Le(data + 22);
  memcpy(info.serial, data + 24, DEVICE_SERIAL_SIZE);
  info.year = readUint16Le(data + 36);
  info.month = data[38];
  info.day = data[39];
  return true;
}

// BASIC_INFO (0x30), 16 bytes. Units were established by watching which values
// move and against a known input voltage; see tests/probe/dp100.
static constexpr size_t BASIC_INFO_SIZE = 16;

struct BasicInfo
{
  int16_t inputMillivolts = 0;    // vin
  int16_t outputMillivolts = 0;   // vout
  int16_t outputMilliamps = 0;    // iout
  int16_t maxOutputMillivolts = 0; // vo_max, the ceiling the present input allows
  int16_t temperature1Deci = 0;   // 0.1 degC
  int16_t temperature2Deci = 0;   // 0.1 degC
  uint16_t rail5vMillivolts = 0;  // dc_5v, an internal rail
  uint8_t outputMode = 0;
  uint8_t workStatus = 0;
};

inline bool decodeBasicInfo(const Response &response, BasicInfo &info)
{
  if (response.opcode != OP_BASIC_INFO || response.length < BASIC_INFO_SIZE || !response.data)
  {
    return false;
  }
  const uint8_t *data = response.data;
  info.inputMillivolts = readInt16Le(data + 0);
  info.outputMillivolts = readInt16Le(data + 2);
  info.outputMilliamps = readInt16Le(data + 4);
  info.maxOutputMillivolts = readInt16Le(data + 6);
  info.temperature1Deci = readInt16Le(data + 8);
  info.temperature2Deci = readInt16Le(data + 10);
  info.rail5vMillivolts = readUint16Le(data + 12);
  info.outputMode = data[14];
  info.workStatus = data[15];
  return true;
}

// BASIC_SET (0x35), 10 bytes: the setpoints and the output enable.
//
// The index carries a flag that says what the frame is for, and getting it wrong
// is silent: a write to a bare index is answered with STATUS_OK and then ignored.
// Both flags were established by measurement (tests/probe/dp100).
static constexpr size_t BASIC_SET_SIZE = 10;
static constexpr uint8_t INDEX_WRITE_FLAG = 0x20; // set a value
static constexpr uint8_t INDEX_READ_FLAG = 0x80;  // read the live value back

// The state byte is the output enable, measured: 1 put 5.000 V on the terminals,
// 0 took it back to 0.
static constexpr uint8_t STATE_OUTPUT_OFF = 0x00;
static constexpr uint8_t STATE_OUTPUT_ON = 0x01;

struct BasicSet
{
  // The bare group index, without a flag: encodeBasicSet() adds the write flag and
  // encodeBasicSetRead() the read flag.
  uint8_t index = 0;
  // Output enable on a write; on a read, what the supply is set to do.
  uint8_t state = 0;
  uint16_t voltageMillivolts = 0;
  uint16_t currentMilliamps = 0;
  uint16_t overVoltageMillivolts = 0;
  uint16_t overCurrentMilliamps = 0;
};

// Reads one setpoint. The bare index answers too, but it reports the stored preset
// rather than the live value, so the read flag is what a caller wants.
inline size_t encodeBasicSetRead(uint8_t *out, size_t capacity, uint8_t index)
{
  const uint8_t data[1] = {static_cast<uint8_t>(index | INDEX_READ_FLAG)};
  return encodeRequest(out, capacity, OP_BASIC_SET, data, sizeof(data));
}

inline size_t encodeBasicSet(uint8_t *out, size_t capacity, const BasicSet &set)
{
  uint8_t data[BASIC_SET_SIZE] = {};
  data[0] = static_cast<uint8_t>(set.index | INDEX_WRITE_FLAG);
  data[1] = set.state;
  data[2] = static_cast<uint8_t>(set.voltageMillivolts & 0xff);
  data[3] = static_cast<uint8_t>(set.voltageMillivolts >> 8);
  data[4] = static_cast<uint8_t>(set.currentMilliamps & 0xff);
  data[5] = static_cast<uint8_t>(set.currentMilliamps >> 8);
  data[6] = static_cast<uint8_t>(set.overVoltageMillivolts & 0xff);
  data[7] = static_cast<uint8_t>(set.overVoltageMillivolts >> 8);
  data[8] = static_cast<uint8_t>(set.overCurrentMilliamps & 0xff);
  data[9] = static_cast<uint8_t>(set.overCurrentMilliamps >> 8);
  return encodeRequest(out, capacity, OP_BASIC_SET, data, sizeof(data));
}

inline bool decodeBasicSet(const Response &response, BasicSet &set)
{
  if (response.opcode != OP_BASIC_SET || response.length < BASIC_SET_SIZE || !response.data)
  {
    return false;
  }
  const uint8_t *data = response.data;
  // The device echoes the bare index, without the flag the request carried.
  set.index = data[0];
  set.state = data[1];
  set.voltageMillivolts = readUint16Le(data + 2);
  set.currentMilliamps = readUint16Le(data + 4);
  set.overVoltageMillivolts = readUint16Le(data + 6);
  set.overCurrentMilliamps = readUint16Le(data + 8);
  return true;
}

} // namespace dp100
