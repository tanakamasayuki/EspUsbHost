// DL-1xx bulk command stream: register writes and RLE pixel writes.
//
// This header is deliberately free of Arduino, LovyanGFX and USB dependencies so
// it can be compiled and tested on the host (see tests/unit/dl1xx). It only
// formats bytes into a caller-supplied buffer; who owns that buffer and how it
// reaches the device is decided by Dl1xxDevice.hpp.
//
// The wire format comes from published reverse-engineering work on the
// DisplayLink DL-1xx chips (Florian Echtler's protocol notes), the ISC-licensed
// OpenBSD `udl` driver, and the MIT-licensed Pico_USB_Disp protocol notes. No
// code from the GPL-2.0 `udlfb`, from LGPL-2.1 `libdlo`, or from any Synaptics
// SDK was used.
//
// DisplayLink is a trademark of Synaptics Incorporated. This project is not
// affiliated with, endorsed by, or certified by Synaptics.

#pragma once

#include <stddef.h>
#include <stdint.h>

namespace dl1xx
{

// Every command on the bulk OUT endpoint starts with this byte. On its own it is
// a no-op, which makes it usable as padding.
static constexpr uint8_t CMD_PREFIX = 0xaf;

static constexpr uint8_t CMD_REGISTER_WRITE = 0x20;
static constexpr uint8_t CMD_PIXELS_RLE16 = 0x6b;
static constexpr uint8_t CMD_FLUSH = 0xa0;

// Register writes take effect as a group: write LOCK, set the registers, then
// write UNLOCK.
static constexpr uint8_t REG_LOCK = 0xff;
static constexpr uint8_t REG_LOCK_VALUE = 0x00;
static constexpr uint8_t REG_UNLOCK_VALUE = 0xff;

// One pixel-write command covers at most this many pixels, and a count field
// encodes 256 as 0.
static constexpr size_t MAX_PIXELS_PER_COMMAND = 256;

// AF 6B + 3 address bytes + 1 count byte.
static constexpr size_t RLE_HEADER_BYTES = 6;

// Worst case for one command: every pixel differs, so a single raw run of
// `pixels` literals plus its one count byte. For 256 pixels that is 519 bytes,
// a slight expansion over the 512 raw bytes.
static constexpr size_t maxRleCommandBytes(size_t pixels)
{
  return RLE_HEADER_BYTES + 1 + pixels * 2;
}

// Timing registers do not hold plain numbers. A value of N is encoded as the
// state of a 16-bit LFSR (taps 15, 4, 2, 1) after stepping N times from 0xFFFF.
// The tap set is primitive: the sequence has full period 65535 and never
// reaches 0.
static inline uint16_t lfsr16(uint16_t steps)
{
  uint16_t state = 0xffff;
  for (uint16_t i = 0; i < steps; i++)
  {
    const uint16_t feedback =
        ((state >> 15) ^ (state >> 4) ^ (state >> 2) ^ (state >> 1)) & 1u;
    state = static_cast<uint16_t>((state << 1) | feedback);
  }
  return state;
}

// Formats commands into a fixed buffer. Every writer returns false and latches
// overflow() when the buffer is too small; a failed writer appends nothing, so a
// truncated command is never sent.
class CommandBuffer
{
public:
  CommandBuffer(uint8_t *data, size_t capacity) : data_(data), capacity_(data ? capacity : 0) {}

  void reset()
  {
    length_ = 0;
    overflow_ = false;
  }

  size_t length() const { return length_; }
  size_t capacity() const { return capacity_; }
  size_t remaining() const { return capacity_ - length_; }
  bool overflow() const { return overflow_; }
  const uint8_t *data() const { return data_; }

  bool registerWrite(uint8_t reg, uint8_t value)
  {
    if (!reserve(4))
    {
      return false;
    }
    put(CMD_PREFIX);
    put(CMD_REGISTER_WRITE);
    put(reg);
    put(value);
    return true;
  }

  // A 16-bit register value occupies two consecutive registers, high byte first.
  bool registerWrite16(uint8_t reg, uint16_t value)
  {
    if (remaining() < 8)
    {
      overflow_ = true;
      return false;
    }
    return registerWrite(reg, static_cast<uint8_t>(value >> 8)) &&
           registerWrite(static_cast<uint8_t>(reg + 1), static_cast<uint8_t>(value & 0xff));
  }

  // The pixel clock register is the exception: low byte first.
  bool registerWrite16LowFirst(uint8_t reg, uint16_t value)
  {
    if (remaining() < 8)
    {
      overflow_ = true;
      return false;
    }
    return registerWrite(reg, static_cast<uint8_t>(value & 0xff)) &&
           registerWrite(static_cast<uint8_t>(reg + 1), static_cast<uint8_t>(value >> 8));
  }

  // Plane base addresses are 24-bit, most significant byte first.
  bool registerWrite24(uint8_t reg, uint32_t value)
  {
    if (remaining() < 12)
    {
      overflow_ = true;
      return false;
    }
    return registerWrite(reg, static_cast<uint8_t>((value >> 16) & 0xff)) &&
           registerWrite(static_cast<uint8_t>(reg + 1), static_cast<uint8_t>((value >> 8) & 0xff)) &&
           registerWrite(static_cast<uint8_t>(reg + 2), static_cast<uint8_t>(value & 0xff));
  }

  bool lockRegisters() { return registerWrite(REG_LOCK, REG_LOCK_VALUE); }
  bool unlockRegisters() { return registerWrite(REG_LOCK, REG_UNLOCK_VALUE); }

  // Forces the device to execute whatever it has buffered.
  bool flush()
  {
    if (!reserve(2))
    {
      return false;
    }
    put(CMD_PREFIX);
    put(CMD_FLUSH);
    return true;
  }

  // Padding: the prefix byte alone does nothing.
  bool pad(size_t bytes)
  {
    if (!reserve(bytes))
    {
      return false;
    }
    for (size_t i = 0; i < bytes; i++)
    {
      put(CMD_PREFIX);
    }
    return true;
  }

  // RLE pixel write to the base16 plane.
  //
  // `byteAddress` is a byte offset into the device frame buffer, normally
  // (y * width + x) * 2. `pixels` are RGB565 values in host order; they go out
  // big-endian. Returns how many pixels this command consumed, which can be less
  // than `count` when the 256-pixel limit or the buffer is reached, and 0 when
  // nothing fit. Call it in a loop, advancing the address by 2 bytes per pixel.
  //
  // The encoding alternates a raw run (a literal count followed by that many
  // pixels) with a repeat run (one byte holding how many extra copies of the
  // previous pixel follow), starting with a raw run. A raw run normally extends
  // up to and including the first pixel of a repeat, so the repeat count that
  // follows is always at least 1; a count of 0 would be ambiguous with 256. When
  // a repeat count of 0 would be required the command simply ends instead.
  size_t writePixelsRle(uint32_t byteAddress, const uint16_t *pixels, size_t count)
  {
    if (!pixels)
    {
      return 0;
    }
    return encodeRle(byteAddress, HostPixels{pixels}, count);
  }

  // Same, for pixels that are already stored as big-endian RGB565 byte pairs --
  // which is exactly LovyanGFX's rgb565_2Byte memory layout, so a panel can hand
  // its converted line buffer over without any per-pixel byte swapping.
  size_t writePixelsRleBigEndian(uint32_t byteAddress, const uint8_t *pixels, size_t count)
  {
    if (!pixels)
    {
      return 0;
    }
    return encodeRle(byteAddress, BigEndianPixels{pixels}, count);
  }

private:
  // Pixel accessors. Both return the value in wire order: the high byte is the
  // first byte sent, so emitting high-then-low reproduces big-endian input
  // byte for byte.
  struct HostPixels
  {
    const uint16_t *pixels;
    uint16_t operator[](size_t i) const { return pixels[i]; }
  };

  struct BigEndianPixels
  {
    const uint8_t *pixels;
    uint16_t operator[](size_t i) const
    {
      return static_cast<uint16_t>((pixels[i * 2] << 8) | pixels[i * 2 + 1]);
    }
  };

  template <typename Source>
  size_t encodeRle(uint32_t byteAddress, Source pixels, size_t count)
  {
    if (count == 0)
    {
      return 0;
    }
    // The smallest useful command is one literal pixel.
    if (remaining() < RLE_HEADER_BYTES + 1 + 2)
    {
      overflow_ = true;
      return 0;
    }

    const size_t budget = count < MAX_PIXELS_PER_COMMAND ? count : MAX_PIXELS_PER_COMMAND;
    const size_t headerAt = length_;
    put(CMD_PREFIX);
    put(CMD_PIXELS_RLE16);
    put(static_cast<uint8_t>((byteAddress >> 16) & 0xff));
    put(static_cast<uint8_t>((byteAddress >> 8) & 0xff));
    put(static_cast<uint8_t>(byteAddress & 0xff));
    const size_t countAt = length_;
    put(0); // patched below once the pixel total is known

    size_t consumed = 0;
    while (consumed < budget)
    {
      // Raw run: literals up to the start of the next repeated pair.
      size_t rawLength = 1;
      while (consumed + rawLength < budget &&
             pixels[consumed + rawLength - 1] != pixels[consumed + rawLength])
      {
        rawLength++;
      }
      // Trim to what the buffer can still hold, keeping at least one pixel.
      const size_t available = remaining();
      if (available < 1 + 2)
      {
        break;
      }
      const size_t fits = (available - 1) / 2;
      if (rawLength > fits)
      {
        rawLength = fits;
      }
      put(static_cast<uint8_t>(rawLength == MAX_PIXELS_PER_COMMAND ? 0 : rawLength));
      for (size_t i = 0; i < rawLength; i++)
      {
        const uint16_t pixel = pixels[consumed + i];
        put(static_cast<uint8_t>(pixel >> 8));
        put(static_cast<uint8_t>(pixel & 0xff));
      }
      consumed += rawLength;
      if (consumed >= budget)
      {
        break;
      }

      // Repeat run: extra copies of the pixel the raw run ended on.
      size_t extras = 0;
      while (consumed + extras < budget && extras < 255 &&
             pixels[consumed + extras] == pixels[consumed - 1])
      {
        extras++;
      }
      if (extras == 0 || remaining() < 1)
      {
        // Alternation would require a repeat element here, and 0 cannot be
        // expressed. End the command; the caller continues at the next address.
        break;
      }
      put(static_cast<uint8_t>(extras));
      consumed += extras;
    }

    if (consumed == 0)
    {
      // Nothing was encoded; drop the header so no partial command is emitted.
      length_ = headerAt;
      overflow_ = true;
      return 0;
    }
    data_[countAt] = static_cast<uint8_t>(consumed == MAX_PIXELS_PER_COMMAND ? 0 : consumed);
    return consumed;
  }

  bool reserve(size_t bytes)
  {
    if (remaining() < bytes)
    {
      overflow_ = true;
      return false;
    }
    return true;
  }

  void put(uint8_t value) { data_[length_++] = value; }

  uint8_t *data_ = nullptr;
  size_t capacity_ = 0;
  size_t length_ = 0;
  bool overflow_ = false;
};

} // namespace dl1xx
