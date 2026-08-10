// Wire format of an AX206 USB photo-frame display.
//
// The device speaks USB mass-storage Bulk-Only Transport, but with vendor
// commands in the 16-byte CDB instead of SCSI ones: a 31-byte CBW carrying the
// command and the length of what follows, then the data, then a 13-byte CSW.
//
// Pure byte formatting: no USB, no LovyanGFX, no Arduino. Everything here is a
// free function over caller-owned buffers, so it can be unit tested on a host
// compiler.
//
// See README.md for the protocol references and the trademark notice.

#pragma once

#include <stddef.h>
#include <stdint.h>

namespace ax206
{

static constexpr uint16_t VID = 0x1908;
static constexpr uint16_t PID = 0x0102;

// The interface is declared as class 0xdc / subclass 0xa0 / protocol 0xb0, which
// is not a class this or any other host driver handles, so it has to be claimed
// by number.
static constexpr uint8_t INTERFACE_NUMBER = 0;
static constexpr uint8_t INTERFACE_CLASS = 0xdc;

// Native panel geometry, landscape.
static constexpr uint16_t WIDTH = 480;
static constexpr uint16_t HEIGHT = 320;

// One frame is the only blit the device accepts, so this is the size of every
// data phase that carries pixels: 307,200 bytes at two bytes each.
static constexpr size_t FRAME_PIXELS = static_cast<size_t>(WIDTH) * HEIGHT;

// Bulk-Only Transport framing.
static constexpr size_t CBW_BYTES = 31;
static constexpr size_t CSW_BYTES = 13;
static constexpr size_t CDB_BYTES = 16;
static constexpr uint32_t CBW_SIGNATURE = 0x43425355; // "USBC"
static constexpr uint32_t CSW_SIGNATURE = 0x53425355; // "USBS"

// Every vendor CDB starts with this byte, with the command itself at index 6.
static constexpr uint8_t CDB_PREFIX = 0xcd;
static constexpr uint8_t CDB_MARKER_INDEX = 5;
static constexpr uint8_t COMMAND_INDEX = 6;

// The three commands a bus capture of a working host shows, and nothing else.
// The value at index 5 goes with the command and differs between them, so it is
// carried per command rather than being a constant of the frame.
static constexpr uint8_t COMMAND_INIT = 0x00;
static constexpr uint8_t COMMAND_INIT_MARKER = 0x02;
static constexpr uint8_t COMMAND_BLIT = 0x12;
static constexpr uint8_t COMMAND_BLIT_MARKER = 0x06;
static constexpr uint8_t COMMAND_SET_PROPERTY = 0x01;
static constexpr uint8_t COMMAND_SET_PROPERTY_MARKER = 0x06;

// Property index and range for the backlight.
static constexpr uint8_t PROPERTY_BRIGHTNESS = 0x01;
static constexpr uint8_t BRIGHTNESS_MAX = 7;

// INIT declares a 5-byte data phase with the wrapper's flags set to host to
// device -- and then the device sends those five bytes instead. Reading them is
// mandatory: leave them on the device and every later read is made against a
// device that is one data phase out of step.
static constexpr uint32_t INIT_TRANSFER_LENGTH = 5;

// The capture uses one constant tag for every command.
static constexpr uint32_t DEFAULT_TAG = 0xefbeadde;

// CSW status byte.
static constexpr uint8_t CSW_PASSED = 0x00;

inline void putLe16(uint8_t *out, uint16_t value)
{
  out[0] = static_cast<uint8_t>(value & 0xff);
  out[1] = static_cast<uint8_t>(value >> 8);
}

inline void putLe32(uint8_t *out, uint32_t value)
{
  out[0] = static_cast<uint8_t>(value & 0xff);
  out[1] = static_cast<uint8_t>((value >> 8) & 0xff);
  out[2] = static_cast<uint8_t>((value >> 16) & 0xff);
  out[3] = static_cast<uint8_t>((value >> 24) & 0xff);
}

inline uint32_t getLe32(const uint8_t *in)
{
  return static_cast<uint32_t>(in[0]) | (static_cast<uint32_t>(in[1]) << 8) |
         (static_cast<uint32_t>(in[2]) << 16) | (static_cast<uint32_t>(in[3]) << 24);
}

// Command Block Wrapper. dCBWDataTransferLength is the length of the data phase
// that follows; bmCBWFlags is 0 (host to device) for every command here, and the
// CDB is always the full 16 bytes. Writes CBW_BYTES bytes.
inline void encodeCbw(uint8_t *out, uint32_t tag, uint32_t transferLength, const uint8_t *cdb)
{
  out[0] = 'U';
  out[1] = 'S';
  out[2] = 'B';
  out[3] = 'C';
  putLe32(out + 4, tag);
  putLe32(out + 8, transferLength);
  out[12] = 0x00; // bmCBWFlags: data out
  out[13] = 0x00; // bCBWLUN
  out[14] = static_cast<uint8_t>(CDB_BYTES);
  for (size_t i = 0; i < CDB_BYTES; i++)
  {
    out[15 + i] = cdb[i];
  }
}

inline void clearCdb(uint8_t *cdb)
{
  for (size_t i = 0; i < CDB_BYTES; i++)
  {
    cdb[i] = 0;
  }
  cdb[0] = CDB_PREFIX;
}

// Wakes the panel up. Sent once after the interface is claimed. The five bytes
// of its data phase come back from the device, not the other way round.
inline void encodeInitCdb(uint8_t *cdb)
{
  clearCdb(cdb);
  cdb[CDB_MARKER_INDEX] = COMMAND_INIT_MARKER;
  cdb[COMMAND_INDEX] = COMMAND_INIT;
}

// The INIT reply is the panel geometry: width and height as little-endian 16-bit
// values, then one more byte whose meaning is not known. Returns false when the
// reply is too short to hold it.
inline bool parseInitReply(const uint8_t *data, size_t length, uint16_t *width, uint16_t *height)
{
  if (!data || length < 4)
  {
    return false;
  }
  if (width)
  {
    *width = static_cast<uint16_t>(data[0] | (data[1] << 8));
  }
  if (height)
  {
    *height = static_cast<uint16_t>(data[2] | (data[3] << 8));
  }
  return true;
}

// Backlight level, 0 to BRIGHTNESS_MAX. No data phase: the wrapper declares zero
// bytes and the status wrapper follows immediately. The panel extent in the tail
// is what the capture carries; the device appears not to mind it being there.
inline void encodeBrightnessCdb(uint8_t *cdb, uint8_t level)
{
  clearCdb(cdb);
  cdb[CDB_MARKER_INDEX] = COMMAND_SET_PROPERTY_MARKER;
  cdb[COMMAND_INDEX] = COMMAND_SET_PROPERTY;
  cdb[7] = PROPERTY_BRIGHTNESS;
  cdb[8] = 0;
  cdb[9] = level > BRIGHTNESS_MAX ? BRIGHTNESS_MAX : level;
  cdb[10] = 0;
  putLe16(cdb + 11, static_cast<uint16_t>(WIDTH - 1));
  putLe16(cdb + 13, static_cast<uint16_t>(HEIGHT - 1));
  cdb[15] = 0;
}

// Opens a pixel rectangle. The corners are inclusive, so a full screen is
// (0, 0)-(479, 319), and exactly (x1-x0+1) * (y1-y0+1) pixels must follow as the
// data phase.
//
// In practice only the full screen works: every blit in a capture of a working
// host covers the whole panel, and SimHub needs its "Disable partial draws"
// option for these displays. The rectangle stays general because the command
// itself is, but Ax206Device only ever opens the full screen.
inline void encodeBlitCdb(uint8_t *cdb, uint16_t x, uint16_t y, uint16_t w, uint16_t h)
{
  clearCdb(cdb);
  cdb[CDB_MARKER_INDEX] = COMMAND_BLIT_MARKER;
  cdb[COMMAND_INDEX] = COMMAND_BLIT;
  putLe16(cdb + 7, x);
  putLe16(cdb + 9, y);
  putLe16(cdb + 11, static_cast<uint16_t>(x + w - 1));
  putLe16(cdb + 13, static_cast<uint16_t>(y + h - 1));
  cdb[15] = 0;
}

inline uint32_t blitDataLength(uint16_t w, uint16_t h)
{
  return static_cast<uint32_t>(w) * h * 2u;
}

// True when the rectangle is non-empty and inside the panel.
inline bool rectFits(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t width, uint16_t height)
{
  if (w == 0 || h == 0)
  {
    return false;
  }
  const uint32_t x1 = static_cast<uint32_t>(x) + w - 1;
  const uint32_t y1 = static_cast<uint32_t>(y) + h - 1;
  return x1 < width && y1 < height;
}

// Command Status Wrapper, located by its signature rather than by offset: the
// reference driver scans the packet for it, and a device that is out of step can
// prepend bytes. Returns false when no CSW for `tag` is present.
inline bool parseCsw(const uint8_t *data, size_t length, uint32_t tag, uint8_t *status)
{
  if (!data || length < CSW_BYTES)
  {
    return false;
  }
  for (size_t i = 0; i + CSW_BYTES <= length; i++)
  {
    if (data[i] != 'U' || data[i + 1] != 'S' || data[i + 2] != 'B' || data[i + 3] != 'S')
    {
      continue;
    }
    if (getLe32(data + i + 4) != tag)
    {
      continue;
    }
    if (status)
    {
      *status = data[i + 12];
    }
    return true;
  }
  return false;
}

// Pixels go out as RGB565 big-endian, which is LovyanGFX's rgb565_2Byte memory
// layout. Converted pixels therefore need no byte swapping; these helpers exist
// for code that builds colors by hand.
inline uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b)
{
  return static_cast<uint16_t>(((r & 0xf8) << 8) | ((g & 0xfc) << 3) | (b >> 3));
}

inline void encodePixel(uint8_t *out, uint16_t color)
{
  out[0] = static_cast<uint8_t>(color >> 8);
  out[1] = static_cast<uint8_t>(color & 0xff);
}

} // namespace ax206
