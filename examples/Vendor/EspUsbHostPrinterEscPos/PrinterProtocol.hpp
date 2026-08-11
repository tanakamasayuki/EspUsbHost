// USB Printer Class (interface class 0x07) wire details.
//
// The class itself is thin: two bulk endpoints that carry a print data stream the
// class says nothing about, plus three class requests on EP0. Everything here is
// pure byte formatting and parsing with no Arduino or USB dependencies, so
// tests/unit/escpos compiles this header directly with g++.
//
// The print data language is a separate concern - see EscPos.hpp.
//
// Reference: USB-IF, Universal Serial Bus Device Class Definition for Printing
// Devices, version 1.1, sections 4.2.1 (subclass/protocol codes) and 4.2.2
// (class-specific requests).

#pragma once

#include <stddef.h>
#include <stdint.h>

namespace printer
{

// A receipt is built in one buffer and sent in one bulk transfer, which matters:
// a printer starts printing as soon as it has a full line, so a receipt split
// across transfers the host might delay comes out stuttering, and on some models
// as a partial line before a timeout. 2 KB holds a long slip with a QR code; a
// raster image of a whole 58 mm page needs more and should be sent in bands.
static constexpr size_t PRINT_BUFFER_SIZE = 2048;

// Interface class / subclass. The protocol code says how the data stream is
// carried, and is the one field worth checking before opening a device: a
// unidirectional interface has no bulk IN, so no status can ever be read back.
static constexpr uint8_t INTERFACE_CLASS = 0x07;
static constexpr uint8_t INTERFACE_SUBCLASS_PRINTER = 0x01;
static constexpr uint8_t PROTOCOL_UNIDIRECTIONAL = 0x01;
static constexpr uint8_t PROTOCOL_BIDIRECTIONAL = 0x02;
static constexpr uint8_t PROTOCOL_1284_4 = 0x03;

// Class-specific requests (Printing Devices 1.1, table 4-2).
static constexpr uint8_t REQ_GET_DEVICE_ID = 0x00;
static constexpr uint8_t REQ_GET_PORT_STATUS = 0x01;
static constexpr uint8_t REQ_SOFT_RESET = 0x02;

// bmRequestType for those requests. GET_* are class IN requests to an interface;
// SOFT_RESET is a class OUT request to an interface with no data stage.
static constexpr uint8_t CLASS_REQUEST_IN = 0xa1;  // 1 01 00001
static constexpr uint8_t CLASS_REQUEST_OUT = 0x21; // 0 01 00001

// GET_DEVICE_ID takes the configuration index in wValue and
// (interface << 8) | alternate setting in wIndex. Both are byte-swapped relative
// to the other class requests, which take the plain interface number in wIndex -
// a detail that is easy to get wrong, so it is built here rather than at the call
// site.
inline uint16_t deviceIdIndex(uint8_t interfaceNumber, uint8_t alternateSetting = 0)
{
  return static_cast<uint16_t>((static_cast<uint16_t>(interfaceNumber) << 8) | alternateSetting);
}

// GET_PORT_STATUS returns one byte. The bits are the Centronics/1284 status
// lines, and the sense of two of them is inverted: NotError and Select are 1 when
// things are *good*, PaperEmpty is 1 when the paper is *gone*.
static constexpr uint8_t PORT_STATUS_PAPER_EMPTY = 0x20; // bit 5
static constexpr uint8_t PORT_STATUS_SELECT = 0x10;      // bit 4
static constexpr uint8_t PORT_STATUS_NOT_ERROR = 0x08;   // bit 3

struct PortStatus
{
  uint8_t raw = 0;
  bool paperEmpty = false;
  bool selected = false;
  bool error = false;
  // A printer that does not implement the request answers 0x00 rather than
  // stalling. See below - when this is set, none of the fields above mean anything.
  bool unknown = false;
};

// A raw 0x00 is treated as "no information", not as what it literally decodes to.
//
// Taken at face value 0x00 says deselected, error, paper present, which is a state
// a printer that is answering EP0 and printing happily is not in. Measured on an
// Xprinter XP-C58K: 0x00 every time, before and after other exchanges and after
// SOFT_RESET, while the ESC/POS real-time status reported the printer ready
// throughout. Reading it literally would have a print loop refuse to print on a
// perfectly healthy printer, which is why the flag exists rather than a comment
// somewhere telling callers to be careful.
//
// The cost of the choice is that a genuine deselected-with-error state that reports
// exactly 0x00 reads as unknown. That is the safer way round: the caller falls back
// to the real-time status, which is a real answer, instead of acting on a byte that
// several models never fill in.
inline PortStatus decodePortStatus(uint8_t raw)
{
  PortStatus status;
  status.raw = raw;
  status.unknown = raw == 0x00;
  if (status.unknown)
  {
    return status;
  }
  status.paperEmpty = (raw & PORT_STATUS_PAPER_EMPTY) != 0;
  status.selected = (raw & PORT_STATUS_SELECT) != 0;
  status.error = (raw & PORT_STATUS_NOT_ERROR) == 0;
  return status;
}

// GET_DEVICE_ID returns an IEEE 1284 device ID string: a two-byte big-endian
// length that counts itself, followed by semicolon-separated KEY:value pairs.
//
//   00 3c 4d 46 47 3a ...   -> length 0x003c, then "MFG:..."
//
// Returns true when the response is well formed, with the characters copied into
// text (always NUL-terminated) and their count in textLength. False means the
// response is too short or its length field disagrees with what arrived, so the
// string cannot be trusted - it could be cut off mid-field.
//
// An empty ID is well formed and returns true with textLength 0. That is not a
// theoretical case: an Xprinter XP-C58K answers `00 02`, a length field and nothing
// else, so treating empty as failure would report a working request as broken.
inline bool decodeDeviceId(const uint8_t *data,
                           size_t length,
                           char *text,
                           size_t textSize,
                           size_t *textLength = nullptr)
{
  if (textLength)
  {
    *textLength = 0;
  }
  if (!data || !text || textSize == 0)
  {
    return false;
  }
  text[0] = '\0';
  if (length < 2)
  {
    return false;
  }
  const size_t declared = (static_cast<size_t>(data[0]) << 8) | data[1];
  if (declared < 2 || declared > length)
  {
    return false;
  }
  size_t payload = declared - 2;
  if (payload > textSize - 1)
  {
    payload = textSize - 1;
  }
  for (size_t i = 0; i < payload; i++)
  {
    text[i] = static_cast<char>(data[2 + i]);
  }
  text[payload] = '\0';
  if (textLength)
  {
    *textLength = payload;
  }
  return true;
}

// Pulls one field out of a device ID string. Keys are matched case-sensitively
// and only at the start of a field, so MDL does not match CMDL. Both the long and
// short spellings of the standard keys are worth trying: printers use MFG or
// MANUFACTURER, MDL or MODEL, CMD or COMMAND SET.
//
// Returns the value length, or 0 when the key is absent.
inline size_t deviceIdField(const char *deviceId, const char *key, char *value, size_t valueSize)
{
  if (!deviceId || !key || !value || valueSize == 0)
  {
    return 0;
  }
  value[0] = '\0';

  size_t keyLength = 0;
  while (key[keyLength] != '\0')
  {
    keyLength++;
  }
  if (keyLength == 0)
  {
    return 0;
  }

  size_t i = 0;
  while (deviceId[i] != '\0')
  {
    // Start of a field: either the string start or just past a ';'.
    size_t k = 0;
    while (k < keyLength && deviceId[i + k] == key[k])
    {
      k++;
    }
    if (k == keyLength && deviceId[i + k] == ':')
    {
      size_t from = i + k + 1;
      size_t out = 0;
      while (deviceId[from] != '\0' && deviceId[from] != ';' && out < valueSize - 1)
      {
        value[out++] = deviceId[from++];
      }
      value[out] = '\0';
      return out;
    }
    // Skip to the character after the next ';'.
    while (deviceId[i] != '\0' && deviceId[i] != ';')
    {
      i++;
    }
    if (deviceId[i] == ';')
    {
      i++;
    }
  }
  return 0;
}

} // namespace printer
