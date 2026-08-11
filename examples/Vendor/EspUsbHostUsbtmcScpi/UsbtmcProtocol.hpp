// USBTMC wire format: the 12-byte bulk message header, the 4-byte alignment
// rule, the class request codes and the GET_CAPABILITIES payload.
//
// Byte formatting only. No Arduino, no USB, no SCPI - which is what lets
// tests/unit/usbtmc compile this header with g++ and test the production code
// rather than a copy of it.
//
// See README.md for the specification references.

#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace usbtmc
{

// Every bulk message, in either direction, starts with this header.
static constexpr size_t HEADER_SIZE = 12;

// bMsgID, USBTMC 1.0 table 1 plus the USB488 addition.
enum MsgId : uint8_t
{
  MSG_DEV_DEP_MSG_OUT = 1,
  MSG_REQUEST_DEV_DEP_MSG_IN = 2,
  MSG_DEV_DEP_MSG_IN = 2, // same value, device -> host direction
  MSG_VENDOR_SPECIFIC_OUT = 126,
  MSG_REQUEST_VENDOR_SPECIFIC_IN = 127,
  MSG_TRIGGER = 128, // USB488
};

// bRequest for the class requests on EP0. 1..64 are USBTMC, 128 and above are
// the USB488 subclass.
enum Request : uint8_t
{
  REQ_INITIATE_ABORT_BULK_OUT = 1,
  REQ_CHECK_ABORT_BULK_OUT_STATUS = 2,
  REQ_INITIATE_ABORT_BULK_IN = 3,
  REQ_CHECK_ABORT_BULK_IN_STATUS = 4,
  REQ_INITIATE_CLEAR = 5,
  REQ_CHECK_CLEAR_STATUS = 6,
  REQ_GET_CAPABILITIES = 7,
  REQ_INDICATOR_PULSE = 64,
  REQ_READ_STATUS_BYTE = 128,  // USB488
  REQ_REN_CONTROL = 160,       // USB488
  REQ_GO_TO_LOCAL = 161,       // USB488
  REQ_LOCAL_LOCKOUT = 162,     // USB488
};

// First byte of every class request response.
enum Status : uint8_t
{
  STATUS_SUCCESS = 0x01,
  STATUS_PENDING = 0x02,
  STATUS_FAILED = 0x80,
  STATUS_TRANSFER_NOT_IN_PROGRESS = 0x81,
  STATUS_SPLIT_NOT_IN_PROGRESS = 0x82,
  STATUS_SPLIT_IN_PROGRESS = 0x83,
};

// bmRequestType for the class requests: class type, interface recipient. The
// library's typed vendorControlIn/Out() send 0xc0 / 0x40 instead, so these go
// through vendorControlTransfer().
static constexpr uint8_t CLASS_REQUEST_IN = 0xa1;
static constexpr uint8_t CLASS_REQUEST_OUT = 0x21;

// Standard CLEAR_FEATURE(ENDPOINT_HALT), the last step of the CLEAR sequence.
static constexpr uint8_t STANDARD_ENDPOINT_OUT = 0x02;
static constexpr uint8_t STANDARD_CLEAR_FEATURE = 0x01;
static constexpr uint16_t FEATURE_ENDPOINT_HALT = 0x0000;

// bmTransferAttributes bits.
static constexpr uint8_t ATTR_END_OF_MESSAGE = 0x01;    // DEV_DEP_MSG_OUT / _IN
static constexpr uint8_t ATTR_TERM_CHAR_ENABLED = 0x02; // REQUEST_DEV_DEP_MSG_IN
static constexpr uint8_t ATTR_TERM_CHAR_DETECTED = 0x02; // DEV_DEP_MSG_IN

// Total bytes a message occupies on the wire: the device expects each message to
// end on a 4-byte boundary, so the payload is followed by up to three zeros.
inline size_t paddedLength(size_t length)
{
  return (length + 3u) & ~static_cast<size_t>(3u);
}

// bTag runs 1..255; zero is reserved and a message must not reuse the tag of the
// one before it.
inline uint8_t nextTag(uint8_t tag)
{
  return (tag >= 255 || tag == 0) ? 1 : static_cast<uint8_t>(tag + 1);
}

inline void writeUint32Le(uint8_t *out, uint32_t value)
{
  out[0] = static_cast<uint8_t>(value);
  out[1] = static_cast<uint8_t>(value >> 8);
  out[2] = static_cast<uint8_t>(value >> 16);
  out[3] = static_cast<uint8_t>(value >> 24);
}

inline uint32_t readUint32Le(const uint8_t *data)
{
  return static_cast<uint32_t>(data[0]) | (static_cast<uint32_t>(data[1]) << 8) |
         (static_cast<uint32_t>(data[2]) << 16) | (static_cast<uint32_t>(data[3]) << 24);
}

inline uint16_t readUint16Le(const uint8_t *data)
{
  return static_cast<uint16_t>(static_cast<uint16_t>(data[0]) | (static_cast<uint16_t>(data[1]) << 8));
}

// DEV_DEP_MSG_OUT: carries a command string to the device. Returns the bytes
// written including padding, or 0 if the buffer is too small or bTag is invalid.
inline size_t encodeDevDepMsgOut(uint8_t *out,
                                 size_t capacity,
                                 uint8_t bTag,
                                 const uint8_t *payload,
                                 size_t payloadLength,
                                 bool endOfMessage = true)
{
  const size_t total = paddedLength(HEADER_SIZE + payloadLength);
  if (!out || bTag == 0 || total > capacity || (payloadLength > 0 && !payload))
  {
    return 0;
  }
  memset(out, 0, total);
  out[0] = MSG_DEV_DEP_MSG_OUT;
  out[1] = bTag;
  out[2] = static_cast<uint8_t>(~bTag);
  writeUint32Le(out + 4, static_cast<uint32_t>(payloadLength));
  out[8] = endOfMessage ? ATTR_END_OF_MESSAGE : 0;
  if (payloadLength > 0)
  {
    memcpy(out + HEADER_SIZE, payload, payloadLength);
  }
  return total;
}

// REQUEST_DEV_DEP_MSG_IN: asks the device to send up to maxTransferSize bytes of
// response on the bulk IN endpoint. Header only, so always 12 bytes.
inline size_t encodeRequestDevDepMsgIn(uint8_t *out,
                                       size_t capacity,
                                       uint8_t bTag,
                                       uint32_t maxTransferSize,
                                       bool termCharEnabled = false,
                                       uint8_t termChar = 0)
{
  if (!out || bTag == 0 || capacity < HEADER_SIZE)
  {
    return 0;
  }
  memset(out, 0, HEADER_SIZE);
  out[0] = MSG_REQUEST_DEV_DEP_MSG_IN;
  out[1] = bTag;
  out[2] = static_cast<uint8_t>(~bTag);
  writeUint32Le(out + 4, maxTransferSize);
  out[8] = termCharEnabled ? ATTR_TERM_CHAR_ENABLED : 0;
  out[9] = termCharEnabled ? termChar : 0;
  return HEADER_SIZE;
}

// Header of a DEV_DEP_MSG_IN response.
struct MsgInHeader
{
  uint8_t msgId = 0;
  uint8_t bTag = 0;
  uint32_t transferSize = 0;
  bool endOfMessage = false;
  bool termCharDetected = false;
};

// Parses the 12-byte response header. Rejects a short read, a wrong bMsgID, a
// bTagInverse that does not complement bTag, and a payload longer than what
// arrived - each of those means the stream is out of sync and the caller has to
// abort rather than trust the bytes.
inline bool decodeMsgInHeader(const uint8_t *data, size_t length, MsgInHeader &header)
{
  if (!data || length < HEADER_SIZE)
  {
    return false;
  }
  if (data[0] != MSG_DEV_DEP_MSG_IN)
  {
    return false;
  }
  if (data[1] == 0 || data[2] != static_cast<uint8_t>(~data[1]))
  {
    return false;
  }
  const uint32_t transferSize = readUint32Le(data + 4);
  if (transferSize > length - HEADER_SIZE)
  {
    return false;
  }
  header.msgId = data[0];
  header.bTag = data[1];
  header.transferSize = transferSize;
  header.endOfMessage = (data[8] & ATTR_END_OF_MESSAGE) != 0;
  header.termCharDetected = (data[8] & ATTR_TERM_CHAR_DETECTED) != 0;
  return true;
}

// GET_CAPABILITIES response, 24 bytes.
struct Capabilities
{
  uint8_t status = 0;
  uint16_t bcdUsbtmc = 0;
  bool listenOnly = false;
  bool talkOnly = false;
  bool indicatorPulse = false;
  bool termCharSupported = false;
  // USB488 fields, meaningful when the interface protocol is 0x01.
  uint16_t bcdUsb488 = 0;
  bool usb488_2 = false;
  bool remoteLocalControl = false;
  bool trigger = false;
  bool scpi = false;
  bool sr1 = false;
  bool rl1 = false;
  bool dt1 = false;
};

static constexpr size_t CAPABILITIES_SIZE = 24;

inline bool decodeCapabilities(const uint8_t *data, size_t length, Capabilities &capabilities)
{
  if (!data || length < CAPABILITIES_SIZE)
  {
    return false;
  }
  capabilities.status = data[0];
  capabilities.bcdUsbtmc = readUint16Le(data + 2);
  capabilities.listenOnly = (data[4] & 0x01) != 0;
  capabilities.talkOnly = (data[4] & 0x02) != 0;
  capabilities.indicatorPulse = (data[4] & 0x04) != 0;
  capabilities.termCharSupported = (data[5] & 0x01) != 0;
  // Bytes 6..11 are reserved. The USB488 subclass then places its own version
  // field before the capability bytes, so those are 14 and 15, not 12 and 13 -
  // a PMX18-5A reads 00 01 07 0f across 12..15, which is bcdUSB488 1.00 followed
  // by the two capability bytes.
  capabilities.bcdUsb488 = readUint16Le(data + 12);
  capabilities.trigger = (data[14] & 0x01) != 0;
  capabilities.remoteLocalControl = (data[14] & 0x02) != 0;
  capabilities.usb488_2 = (data[14] & 0x04) != 0;
  capabilities.dt1 = (data[15] & 0x01) != 0;
  capabilities.rl1 = (data[15] & 0x02) != 0;
  capabilities.sr1 = (data[15] & 0x04) != 0;
  capabilities.scpi = (data[15] & 0x08) != 0;
  return capabilities.status == STATUS_SUCCESS;
}

inline const char *statusName(uint8_t status)
{
  switch (status)
  {
  case STATUS_SUCCESS:
    return "SUCCESS";
  case STATUS_PENDING:
    return "PENDING";
  case STATUS_FAILED:
    return "FAILED";
  case STATUS_TRANSFER_NOT_IN_PROGRESS:
    return "TRANSFER_NOT_IN_PROGRESS";
  case STATUS_SPLIT_NOT_IN_PROGRESS:
    return "SPLIT_NOT_IN_PROGRESS";
  case STATUS_SPLIT_IN_PROGRESS:
    return "SPLIT_IN_PROGRESS";
  default:
    return "unknown";
  }
}

} // namespace usbtmc
