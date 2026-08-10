// AX206 device layer: everything that needs the USB host.
//
// Ax206Protocol.hpp only formats bytes. This header connects it to EspUsbHost:
// it finds the display, claims the interface by number, and runs each command as
// a Bulk-Only Transport transaction -- CBW out, data phase, CSW in.
//
// The unit of drawing is a whole frame, because that is the only blit the device
// accepts. A frame is one transaction: the command declares all 307,200 bytes up
// front, and the device answers only once it has every one of them. Pixels are
// therefore streamed rather than buffered -- they accumulate in a buffer borrowed
// from the USB write queue and are submitted as it fills, so a frame costs a few
// dozen large transfers and no frame buffer on the host.
//
// Streaming forwards is the whole contract: the data phase is a single run from
// the top-left pixel to the bottom-right, so a caller may skip ahead (the gap is
// padded) but can never go back. seekTo() is what enforces that. Callers that
// need arbitrary drawing order have to keep their own frame buffer.
//
// See README.md for the protocol references and the trademark notice.

#pragma once

#include "Ax206Protocol.hpp"
#include "EspUsbHost.h"

#include <string.h>

namespace ax206
{

// Bulk OUT queue shape, the same reasoning as the DL-1xx example: the measured
// full-speed ceiling is reached from a depth of 2 at any transfer size, so the
// memory is better spent on a buffer large enough to hold many rows.
static constexpr size_t QUEUE_DEPTH = 2;
static constexpr size_t QUEUE_BUFFER_BYTES = 8192;

static constexpr uint32_t ACQUIRE_TIMEOUT_MS = 1000;
static constexpr uint32_t FLUSH_TIMEOUT_MS = 5000;
// How long to wait for the CSW that ends a transaction. A full-screen blit is
// 307,200 bytes, so the device can be busy for a while before it answers.
static constexpr uint32_t CSW_TIMEOUT_MS = 5000;

// One AX206 display.
class Ax206Device
{
public:
  explicit Ax206Device(EspUsbHost &host) : host_(host) {}

  // Finds the display by VID/PID. The interface class is not a useful filter
  // here: 0xdc / 0xa0 / 0xb0 means nothing in particular, so the identity of the
  // device is what selects it.
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
      return devices[i].address;
    }
    return 0;
  }

  // Claims the interface, starts the write queue and wakes the panel.
  bool begin(uint8_t address = ESP_USB_HOST_ANY_ADDRESS)
  {
    const uint8_t found = findDisplay(address);
    if (found == 0)
    {
      return false;
    }
    // Named explicitly, because the interface is not vendor-class and nothing
    // would select it automatically. On-demand reads, because this is a
    // transactional protocol: the device answers only inside a transaction, and
    // a continuously outstanding IN transfer is a transfer error the rest of the
    // time -- on our hub it took the hub driver down with it.
    if (!host_.vendorOpen(found, INTERFACE_NUMBER, ESP_USB_HOST_VENDOR_READ_ON_DEMAND))
    {
      return false;
    }
    if (host_.vendorOutEndpoint(found) == 0 || host_.vendorInEndpoint(found) == 0)
    {
      // Both directions are required: the CSW that ends every transaction comes
      // back on the bulk IN endpoint.
      return false;
    }
    if (!host_.vendorWriteQueueBegin(QUEUE_DEPTH, QUEUE_BUFFER_BYTES, found))
    {
      return false;
    }

    address_ = found;
    written_ = 0;
    frameOpen_ = false;
    generation_++;
    if (!sendInit())
    {
      end();
      return false;
    }
    // The capture sets the backlight once the panel is awake. Without it a panel
    // that came up dark stays dark however much is drawn on it.
    setBrightness(BRIGHTNESS_MAX);
    return true;
  }

  void end()
  {
    if (address_ == 0)
    {
      return;
    }
    releaseBuffer();
    host_.vendorWriteQueueEnd(address_);
    address_ = 0;
    written_ = 0;
    frameOpen_ = false;
    generation_++;
  }

  bool ready() const { return address_ != 0; }
  uint8_t address() const { return address_; }
  uint16_t width() const { return WIDTH; }
  uint16_t height() const { return HEIGHT; }

  // Bumped whenever the device is reopened. A consumer that caches what is on
  // screen (a diff-transfer canvas, say) must discard that cache when it moves.
  uint32_t generation() const { return generation_; }

  // Transactions that ended with a non-zero CSW status or no CSW at all. Either
  // means the screen no longer holds what the caller believes.
  uint32_t failures() const { return failures_; }

  // Wakes the panel and reads back the geometry it reports. Also the recovery
  // path if it stops answering.
  bool sendInit()
  {
    uint8_t cdb[CDB_BYTES];
    encodeInitCdb(cdb);
    if (!sendCbw(INIT_TRANSFER_LENGTH, cdb))
    {
      return false;
    }
    // The data phase belongs to the device even though the wrapper declares the
    // other direction. Reading it is what keeps the endpoint in step.
    uint8_t reply[64];
    size_t length = 0;
    if (!readIn(reply, sizeof(reply), &length))
    {
      return false;
    }
    parseInitReply(reply, length, &reportedWidth_, &reportedHeight_);
    return awaitCsw();
  }

  // Backlight, 0 to BRIGHTNESS_MAX. A command with no data phase.
  bool setBrightness(uint8_t level)
  {
    if (address_ == 0)
    {
      return false;
    }
    uint8_t cdb[CDB_BYTES];
    encodeBrightnessCdb(cdb, level);
    if (!sendCbw(0, cdb))
    {
      return false;
    }
    return awaitCsw();
  }

  // Geometry the device reported from INIT. Should match WIDTH and HEIGHT.
  uint16_t reportedWidth() const { return reportedWidth_; }
  uint16_t reportedHeight() const { return reportedHeight_; }

  // Opens a frame. Every one of WIDTH * HEIGHT pixels is owed to the device from
  // here on, in reading order; endFrame() pads whatever the caller did not fill.
  bool beginFrame()
  {
    if (address_ == 0)
    {
      return false;
    }
    if (frameOpen_ && !endFrame())
    {
      return false;
    }
    uint8_t cdb[CDB_BYTES];
    encodeBlitCdb(cdb, 0, 0, WIDTH, HEIGHT);
    if (!sendCbw(blitDataLength(WIDTH, HEIGHT), cdb))
    {
      return false;
    }
    written_ = 0;
    frameOpen_ = true;
    return true;
  }

  bool frameOpen() const { return frameOpen_; }

  // Pixels already written in this frame, counted from the top-left.
  size_t written() const { return written_; }

  // Moves the write position to `index`, padding the gap. Moving backwards is
  // impossible in a stream, so it fails instead -- see backward().
  bool seekTo(size_t index)
  {
    if (!frameOpen_)
    {
      return false;
    }
    if (index == written_)
    {
      return true;
    }
    if (index < written_)
    {
      backward_++;
      return false;
    }
    if (index > FRAME_PIXELS)
    {
      return false;
    }
    return writeFill(padColor_, index - written_);
  }

  bool seekTo(uint16_t x, uint16_t y) { return seekTo(static_cast<size_t>(y) * WIDTH + x); }

  // Pixels at the current position, already stored as RGB565 big-endian byte
  // pairs -- LovyanGFX's rgb565_2Byte layout, so no per-pixel conversion.
  bool writePixelBytes(const uint8_t *pixels, size_t count)
  {
    if (!frameOpen_ || !pixels)
    {
      return false;
    }
    count = clampToFrame(count);
    if (count == 0)
    {
      return true;
    }
    if (!writeBytes(pixels, count * 2))
    {
      return false;
    }
    written_ += count;
    return true;
  }

  // Repeats one color for `count` pixels, written straight into the USB buffer.
  bool writeFill(uint16_t color, size_t count)
  {
    if (!frameOpen_)
    {
      return false;
    }
    count = clampToFrame(count);
    while (count != 0)
    {
      if (!ensureBuffer(2))
      {
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
      written_ += take;
    }
    return true;
  }

  // Pads the frame out to the full screen and completes the transaction. The
  // device is waiting for exactly the declared number of bytes and will not
  // answer until it has them, so the padding is what keeps the link from wedging.
  bool endFrame()
  {
    if (!frameOpen_)
    {
      return true;
    }
    if (written_ < FRAME_PIXELS)
    {
      underfilled_++;
      if (!writeFill(padColor_, FRAME_PIXELS - written_))
      {
        frameOpen_ = false;
        return false;
      }
    }
    frameOpen_ = false;
    written_ = 0;
    return awaitCsw();
  }

  // Color used for pixels the caller never wrote: the gap a seek jumps over and
  // the tail endFrame() pads. Black by default.
  void setPadColor(uint16_t color) { padColor_ = color; }

  bool fillScreen(uint16_t color)
  {
    return beginFrame() && writeFill(color, FRAME_PIXELS) && endFrame();
  }

  // Completes the open frame, if any.
  bool flush() { return endFrame(); }

  // Frames where the caller left pixels unwritten, and writes that asked to go
  // back to a pixel already sent. Both mean the screen is not what the caller
  // believes.
  uint32_t underfilled() const { return underfilled_; }
  uint32_t backward() const { return backward_; }

  EspUsbHostVendorWriteStats stats() const { return host_.vendorWriteStats(address_); }
  void resetStats() { host_.vendorWriteStatsReset(address_); }

private:
  // Sends a CBW as a transfer of its own, so the command never shares a transfer
  // with the data phase that follows it.
  bool sendCbw(uint32_t transferLength, const uint8_t *cdb)
  {
    uint8_t cbw[CBW_BYTES];
    encodeCbw(cbw, tag_, transferLength, cdb);
    if (!writeBytes(cbw, sizeof(cbw)))
    {
      return false;
    }
    return submitBuffer();
  }

  // One bulk IN read, with everything still queued for output pushed out first:
  // the device answers only once it has what the command declared.
  bool readIn(uint8_t *buffer, size_t capacity, size_t *length)
  {
    if (!drainOutput())
    {
      failures_++;
      return false;
    }
    if (!host_.vendorReadSync(buffer, capacity, length, CSW_TIMEOUT_MS, address_))
    {
      failures_++;
      return false;
    }
    return true;
  }

  // Waits for the transaction's status wrapper.
  bool awaitCsw()
  {
    // One packet is enough: the CSW is 13 bytes and arrives on its own.
    uint8_t packet[64];
    size_t length = 0;
    if (!readIn(packet, sizeof(packet), &length))
    {
      return false;
    }
    uint8_t status = 0;
    if (!parseCsw(packet, length, tag_, &status))
    {
      failures_++;
      return false;
    }
    if (status != CSW_PASSED)
    {
      failures_++;
      return false;
    }
    return true;
  }

  bool drainOutput()
  {
    return submitBuffer() && host_.vendorWriteFlush(FLUSH_TIMEOUT_MS, address_);
  }

  // Never write past the end of the frame: the device counts the bytes it was
  // promised and anything beyond them would land in the next transaction.
  size_t clampToFrame(size_t count) const
  {
    const size_t room = FRAME_PIXELS - written_;
    return count < room ? count : room;
  }

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
    buffer_ = host_.vendorWriteAcquire(&capacity_, ACQUIRE_TIMEOUT_MS, address_);
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
      host_.vendorWriteRelease(buffer, address_);
      return true;
    }
    if (!host_.vendorWriteSubmit(buffer, length, address_))
    {
      host_.vendorWriteRelease(buffer, address_);
      return false;
    }
    return true;
  }

  void releaseBuffer()
  {
    if (buffer_)
    {
      host_.vendorWriteRelease(buffer_, address_);
    }
    buffer_ = nullptr;
    used_ = 0;
    capacity_ = 0;
  }

  EspUsbHost &host_;
  uint8_t address_ = 0;
  uint32_t generation_ = 0;
  uint32_t tag_ = DEFAULT_TAG;
  uint32_t failures_ = 0;
  uint32_t underfilled_ = 0;
  uint32_t backward_ = 0;
  uint16_t reportedWidth_ = 0;
  uint16_t reportedHeight_ = 0;
  uint16_t padColor_ = 0;
  bool frameOpen_ = false;
  size_t written_ = 0;
  uint8_t *buffer_ = nullptr;
  size_t capacity_ = 0;
  size_t used_ = 0;
};

} // namespace ax206
