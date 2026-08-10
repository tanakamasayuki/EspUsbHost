// Device layer for the 3.5-inch USB smart screen: everything that needs the USB
// host.
//
// TuringProtocol.hpp only formats bytes. This header connects it to
// EspUsbHost: it finds the panel among the enumerated CDC devices, sets the line
// coding, starts the asynchronous CDC OUT queue and streams commands and pixels
// through it.
//
// The protocol is a plain byte stream, so everything below is a writer over that
// stream. Bytes accumulate in a buffer borrowed from the USB queue and are
// submitted when the buffer fills or when push() is called, which turns a screen
// update into a few large transfers rather than one transfer per row.
//
// See README.md for the protocol references and the trademark notice.

#pragma once

#include "EspUsbHost.h"
#include "TuringProtocol.hpp"

#include <string.h>

namespace turing
{

// CDC OUT queue shape. The panel, not the bus, sets the pace: it NAKs until it
// has consumed what it was sent, which measures out at roughly 0.15-0.27 MB/s
// against a full-speed ceiling of about 1 MB/s. Raising the depth or the slot
// size does not move that number (measured: 3x4KB and 4x8KB give the same rate),
// so this is the smallest pool that still keeps one transfer on the wire while
// the next is being filled. 4 KB per slot holds six 320-pixel rows.
static constexpr size_t QUEUE_DEPTH = 3;
static constexpr size_t QUEUE_BUFFER_BYTES = 4096;

// How long to wait for a free queue slot before giving up on a write.
static constexpr uint32_t ACQUIRE_TIMEOUT_MS = 1000;
static constexpr uint32_t FLUSH_TIMEOUT_MS = 5000;

// One 3.5-inch USB smart screen.
class TuringDevice
{
public:
  explicit TuringDevice(EspUsbHost &host) : host_(host) {}

  // Finds the panel: VID/PID match on a device whose CDC OUT endpoint is ready.
  uint8_t findDisplay(uint8_t address = ESP_USB_HOST_ANY_ADDRESS) const
  {
    EspUsbHostDeviceInfo devices[ESP_USB_HOST_MAX_DEVICES];
    const size_t count = host_.getDevices(devices, ESP_USB_HOST_MAX_DEVICES);
    for (size_t i = 0; i < count; i++)
    {
      if (devices[i].vid != VID || devices[i].pid != PID)
      {
        continue;
      }
      if (address != ESP_USB_HOST_ANY_ADDRESS && devices[i].address != address)
      {
        continue;
      }
      if (!host_.serialReady(devices[i].address))
      {
        continue;
      }
      return devices[i].address;
    }
    return 0;
  }

  // Finds the panel, sets the line coding and starts the CDC OUT queue. Leaves
  // the panel in its current orientation; call setOrientation() to fix it.
  bool begin(uint8_t address = ESP_USB_HOST_ANY_ADDRESS)
  {
    const uint8_t found = findDisplay(address);
    if (found == 0)
    {
      return false;
    }
    if (!host_.setSerialBaudRate(BAUD, found))
    {
      return false;
    }
    // Without the queue every write would allocate its own transfer and none of
    // them would ever push back, so a full frame would outrun the bus and
    // exhaust DMA memory. The queue is what paces this example.
    if (!host_.serialWriteQueueBegin(QUEUE_DEPTH, QUEUE_BUFFER_BYTES, found))
    {
      return false;
    }

    address_ = found;
    pending_ = 0;
    orientation_ = ORIENTATION_PORTRAIT;
    width_ = NATIVE_WIDTH;
    height_ = NATIVE_HEIGHT;
    generation_++;
    return true;
  }

  void end()
  {
    if (address_ == 0)
    {
      return;
    }
    releaseBuffer();
    host_.serialWriteQueueEnd(address_);
    address_ = 0;
    generation_++;
  }

  bool ready() const { return address_ != 0; }
  uint8_t address() const { return address_; }
  uint16_t width() const { return width_; }
  uint16_t height() const { return height_; }
  Orientation orientation() const { return orientation_; }

  // Bumped whenever the device is reopened or its orientation changes. A
  // consumer that caches what is on screen (a diff-transfer canvas, say) must
  // discard that cache when this changes.
  uint32_t generation() const { return generation_; }

  // Rotates the panel and adopts the resulting logical size. The frame buffer
  // contents are undefined afterwards, hence the generation bump.
  bool setOrientation(Orientation orientation)
  {
    if (address_ == 0)
    {
      return false;
    }
    const bool landscape =
        orientation == ORIENTATION_LANDSCAPE || orientation == ORIENTATION_REVERSE_LANDSCAPE;
    const uint16_t width = landscape ? NATIVE_HEIGHT : NATIVE_WIDTH;
    const uint16_t height = landscape ? NATIVE_WIDTH : NATIVE_HEIGHT;

    uint8_t packet[ORIENTATION_COMMAND_BYTES];
    const size_t length = encodeOrientation(packet, orientation, width, height);
    // An orientation change has to be the only thing in flight, so finish any
    // open rectangle, drain, and wait for it to land.
    if (!finishBitmap() || !writeBytes(packet, length) || !flush())
    {
      return false;
    }

    orientation_ = orientation;
    width_ = width;
    height_ = height;
    generation_++;
    return true;
  }

  // 0 = off, 100 = full.
  bool setBrightness(uint8_t percent) { return sendCommandPacket(COMMAND_SET_BRIGHTNESS, encodeBrightnessPercent(percent)); }
  bool screenOn() { return sendCommandPacket(COMMAND_SCREEN_ON); }
  bool screenOff() { return sendCommandPacket(COMMAND_SCREEN_OFF); }
  bool clearWhite() { return sendCommandPacket(COMMAND_CLEAR); }
  bool clearBlack() { return sendCommandPacket(COMMAND_TO_BLACK); }

  // Starts a pixel rectangle. Exactly w * h pixels must follow, row by row,
  // before any other command.
  //
  // The panel will not accept a command that arrives while it is still taking
  // the previous rectangle's pixels: it keeps consuming the bytes, but the
  // command and everything after it are discarded. So a bitmap is the unit of
  // synchronisation here -- the previous one is completed and drained before
  // this header goes out. See README.md for how this shows up if it is skipped.
  bool beginBitmap(uint16_t x, uint16_t y, uint16_t w, uint16_t h)
  {
    if (address_ == 0 || !rectFits(x, y, w, h, width_, height_))
    {
      return false;
    }
    if (!finishBitmap())
    {
      return false;
    }
    uint8_t header[COMMAND_BYTES];
    encodeBitmapHeader(header, x, y, w, h);
    // The header travels as a transfer of its own: the panel takes a command
    // from the start of a transfer and ignores the rest of it, so payload bytes
    // packed in behind the header are lost and the image slides sideways.
    if (!writeBytes(header, sizeof(header)) || !push())
    {
      return false;
    }
    pending_ = static_cast<size_t>(w) * h;
    return true;
  }

  // Pixels of the rectangle opened by beginBitmap(), already stored as RGB565
  // little-endian byte pairs -- LovyanGFX's rgb565_nonswapped layout, so no
  // per-pixel conversion. The rectangle is drained once its last pixel is in.
  bool writePixelBytes(const uint8_t *pixels, size_t count)
  {
    if (!pixels)
    {
      return false;
    }
    return writeBytes(pixels, count * 2) && consumePending(count);
  }

  // Repeats one color for `count` pixels, written straight into the USB buffer.
  bool writeFill(uint16_t color, size_t count)
  {
    if (address_ == 0)
    {
      return false;
    }
    size_t written = 0;
    while (count != 0)
    {
      if (!ensureBuffer(2))
      {
        dropped_++;
        return false;
      }
      const size_t room = (capacity_ - used_) / 2;
      const size_t take = count < room ? count : room;
      uint8_t *out = buffer_ + used_;
      for (size_t i = 0; i < take; i++)
      {
        encodePixel(out, color);
        out += 2;
      }
      used_ += take * 2;
      count -= take;
      written += take;
    }
    return consumePending(written);
  }

  bool fillRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color)
  {
    return beginBitmap(x, y, w, h) && writeFill(color, static_cast<size_t>(w) * h);
  }

  bool fillScreen(uint16_t color) { return fillRect(0, 0, width_, height_, color); }

  // Submits whatever is buffered without waiting for completion. Keeps pixels
  // moving without stalling the caller, so this is what a panel calls when it
  // finishes a drawing operation.
  bool push()
  {
    if (address_ == 0)
    {
      return false;
    }
    if (!buffer_ || used_ == 0)
    {
      releaseBuffer();
      return true;
    }
    return submitBuffer();
  }

  // push() plus a wait for the queue to drain. Call this only where the caller
  // genuinely needs the bytes to have landed.
  bool flush()
  {
    if (!push())
    {
      return false;
    }
    return host_.serialWriteFlush(FLUSH_TIMEOUT_MS, address_);
  }

  EspUsbHostSerialWriteStats stats() const { return host_.serialWriteStats(address_); }
  void resetStats()
  {
    host_.serialWriteStatsReset(address_);
    dropped_ = 0;
  }

  // Writes that could not be handed to USB. Any non-zero value means the byte
  // stream lost bytes, and since the panel counts the pixels of a DISPLAY_BITMAP
  // itself, a loss desynchronises everything that follows. Callers that see this
  // move should resynchronise with resync().
  uint32_t dropped() const { return dropped_; }

  // Rectangles that were opened and then left short, and had to be padded. A
  // non-zero value means a drawing path is not sending what it promised.
  uint32_t underfilled() const { return underfilled_; }

  // Recovers from a lost write. Sending as many pixels as the largest possible
  // outstanding payload would need is not possible, so instead the panel is
  // given a full-screen bitmap: whatever count it is still waiting for is
  // satisfied by the first part of it, and the rest repaints the screen.
  bool resync()
  {
    if (address_ == 0)
    {
      return false;
    }
    releaseBuffer();
    dropped_ = 0;
    pending_ = 0;
    generation_++;
    return fillScreen(0) && flush();
  }

private:
  // Counts down the pixels still owed to the open rectangle and drains the link
  // once it is complete, which is what lets the next command through.
  bool consumePending(size_t pixels)
  {
    if (pending_ == 0)
    {
      return true;
    }
    if (pixels < pending_)
    {
      pending_ -= pixels;
      return true;
    }
    pending_ = 0;
    return flush();
  }

  // Satisfies a rectangle the caller opened but did not fill. The panel counts
  // the pixels itself, so leaving it short would make it read the next command
  // as pixel data; padding costs a few bytes and keeps the stream in step.
  bool finishBitmap()
  {
    if (pending_ == 0)
    {
      return true;
    }
    underfilled_++;
    const size_t missing = pending_;
    pending_ = 0;
    return writeFill(0, missing) && flush();
  }

  // A command packet must not be split across a pixel payload, so it is written
  // as one unit and then pushed.
  bool sendCommandPacket(uint8_t command, uint16_t x = 0)
  {
    if (address_ == 0)
    {
      return false;
    }
    if (!finishBitmap())
    {
      return false;
    }
    uint8_t packet[COMMAND_BYTES];
    encodeCommand(packet, command, x);
    return writeBytes(packet, sizeof(packet)) && flush();
  }

  // Borrows a queue slot if there is no current buffer, or submits and takes a
  // new one when the current buffer cannot hold `needed` more bytes.
  bool ensureBuffer(size_t needed)
  {
    if (buffer_ && capacity_ - used_ < needed)
    {
      if (!submitBuffer())
      {
        return false;
      }
    }
    if (buffer_)
    {
      return true;
    }
    buffer_ = host_.serialWriteAcquire(&capacity_, ACQUIRE_TIMEOUT_MS, address_);
    if (!buffer_)
    {
      capacity_ = 0;
      return false;
    }
    used_ = 0;
    return capacity_ >= needed;
  }

  bool writeBytes(const uint8_t *data, size_t length)
  {
    if (address_ == 0 || (length != 0 && !data))
    {
      return false;
    }
    while (length != 0)
    {
      if (!ensureBuffer(1))
      {
        dropped_++;
        return false;
      }
      const size_t room = capacity_ - used_;
      const size_t take = length < room ? length : room;
      memcpy(buffer_ + used_, data, take);
      used_ += take;
      data += take;
      length -= take;
    }
    return true;
  }

  bool submitBuffer()
  {
    if (!buffer_)
    {
      return true;
    }
    uint8_t *buffer = buffer_;
    const size_t length = used_;
    buffer_ = nullptr;
    used_ = 0;
    capacity_ = 0;
    if (length == 0)
    {
      host_.serialWriteRelease(buffer, address_);
      return true;
    }
    if (!host_.serialWriteSubmit(buffer, length, address_))
    {
      // The slot was neither sent nor given back by submit, so return it here;
      // leaking it would shrink the pool until every acquire times out.
      host_.serialWriteRelease(buffer, address_);
      dropped_++;
      return false;
    }
    return true;
  }

  void releaseBuffer()
  {
    if (buffer_)
    {
      host_.serialWriteRelease(buffer_, address_);
    }
    buffer_ = nullptr;
    used_ = 0;
    capacity_ = 0;
  }

  EspUsbHost &host_;
  uint8_t address_ = 0;
  Orientation orientation_ = ORIENTATION_PORTRAIT;
  uint16_t width_ = NATIVE_WIDTH;
  uint16_t height_ = NATIVE_HEIGHT;
  uint32_t generation_ = 0;
  uint32_t dropped_ = 0;
  uint32_t underfilled_ = 0;
  size_t pending_ = 0;
  uint8_t *buffer_ = nullptr;
  size_t capacity_ = 0;
  size_t used_ = 0;
};

} // namespace turing
