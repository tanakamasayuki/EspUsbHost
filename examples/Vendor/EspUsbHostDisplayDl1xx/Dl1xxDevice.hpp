// DL-1xx device layer: everything that needs the USB host.
//
// Dl1xxProtocol.hpp / Dl1xxModes.hpp only format bytes. This header connects
// them to EspUsbHost: it claims the vendor interface, sends the channel key,
// reads EDID, programs a mode, and streams pixels through the asynchronous bulk
// OUT queue.
//
// See README.md for the protocol references and the trademark notice.

#pragma once

#include "Dl1xxModes.hpp"
#include "Dl1xxProtocol.hpp"
#include "EspUsbHost.h"

#include <string.h>

namespace dl1xx
{

static constexpr uint16_t VID = 0x17e9;

// Vendor control requests.
static constexpr uint8_t REQUEST_CHANNEL_KEY = 0x12;
static constexpr uint8_t REQUEST_EDID_BYTE = 0x02;
static constexpr uint16_t EDID_INDEX = 0x00a1;

// Selects the standard channel, which leaves the pixel stream unencrypted.
static constexpr uint8_t CHANNEL_KEY[16] = {0x57, 0xcd, 0xdc, 0xa7, 0x1c, 0x88, 0x5e, 0x15,
                                            0x60, 0xfe, 0xc6, 0x97, 0x16, 0x3d, 0x47, 0xf2};

static constexpr size_t EDID_BLOCK_SIZE = 128;

// Bulk OUT queue shape. The measured full-speed ceiling is reached from a depth
// of 2 at any transfer size, so depth buys nothing beyond 2 and the memory is
// better spent on a buffer large enough to pack many commands per transfer.
static constexpr size_t QUEUE_DEPTH = 2;
static constexpr size_t QUEUE_BUFFER_BYTES = 8192;

// How long to wait for a free queue slot before giving up on a command.
static constexpr uint32_t ACQUIRE_TIMEOUT_MS = 1000;
static constexpr uint32_t FLUSH_TIMEOUT_MS = 5000;

// Pixels per solid-fill command. One command covers at most 256 pixels.
static constexpr size_t SOLID_CHUNK_PIXELS = MAX_PIXELS_PER_COMMAND;

// Parsed EDID summary. Only what is needed to pick a mode.
struct EdidInfo
{
  bool valid = false;
  bool checksumOk = false;
  char manufacturer[4] = {};
  uint16_t productCode = 0;
  uint8_t version = 0;
  uint8_t revision = 0;
  bool hasPreferredTiming = false;
  uint16_t preferredWidth = 0;
  uint16_t preferredHeight = 0;
  uint32_t preferredPixelClockKhz = 0;
};

// Decodes the 18-byte detailed timing descriptor at offset 54. Standard EDID
// 1.x layout, so this is independent of anything DisplayLink specific.
static inline bool parseEdidPreferredTiming(const uint8_t *edid, size_t length, Timing &out)
{
  if (!edid || length < EDID_BLOCK_SIZE)
  {
    return false;
  }
  const uint8_t *dtd = edid + 54;
  const uint32_t pixelClock10Khz = static_cast<uint32_t>(dtd[0]) | (static_cast<uint32_t>(dtd[1]) << 8);
  if (pixelClock10Khz == 0)
  {
    return false; // not a timing descriptor
  }

  const uint16_t hActive = static_cast<uint16_t>(dtd[2] | ((dtd[4] & 0xf0) << 4));
  const uint16_t hBlank = static_cast<uint16_t>(dtd[3] | ((dtd[4] & 0x0f) << 8));
  const uint16_t vActive = static_cast<uint16_t>(dtd[5] | ((dtd[7] & 0xf0) << 4));
  const uint16_t vBlank = static_cast<uint16_t>(dtd[6] | ((dtd[7] & 0x0f) << 8));
  const uint16_t hSyncOffset = static_cast<uint16_t>(dtd[8] | ((dtd[11] & 0xc0) << 2));
  const uint16_t hSyncWidth = static_cast<uint16_t>(dtd[9] | ((dtd[11] & 0x30) << 4));
  const uint16_t vSyncWidth = static_cast<uint16_t>((dtd[10] & 0x0f) | ((dtd[11] & 0x03) << 4));
  const uint16_t vSyncOffset = static_cast<uint16_t>(((dtd[10] & 0xf0) >> 4) | ((dtd[11] & 0x0c) << 2));

  if (hActive == 0 || vActive == 0 || hBlank <= hSyncOffset + hSyncWidth ||
      vBlank <= vSyncOffset + vSyncWidth)
  {
    return false;
  }

  out.name = "EDID preferred";
  out.width = hActive;
  out.height = vActive;
  out.hSyncWidth = hSyncWidth;
  out.hBackPorch = static_cast<uint16_t>(hBlank - hSyncOffset - hSyncWidth);
  out.hTotal = static_cast<uint16_t>(hActive + hBlank);
  out.vSyncWidth = vSyncWidth;
  out.vBackPorch = static_cast<uint16_t>(vBlank - vSyncOffset - vSyncWidth);
  out.vTotal = static_cast<uint16_t>(vActive + vBlank);
  out.pixelClockKhz = pixelClock10Khz * 10u;
  return true;
}

static inline void parseEdid(const uint8_t *edid, size_t length, EdidInfo &info)
{
  info = EdidInfo();
  if (!edid || length < EDID_BLOCK_SIZE)
  {
    return;
  }
  static const uint8_t HEADER[8] = {0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00};
  if (memcmp(edid, HEADER, sizeof(HEADER)) != 0)
  {
    return;
  }
  info.valid = true;

  uint8_t sum = 0;
  for (size_t i = 0; i < EDID_BLOCK_SIZE; i++)
  {
    sum = static_cast<uint8_t>(sum + edid[i]);
  }
  info.checksumOk = sum == 0;

  // Manufacturer ID: three 5-bit letters, big-endian, 1 = 'A'.
  const uint16_t id = static_cast<uint16_t>((edid[8] << 8) | edid[9]);
  info.manufacturer[0] = static_cast<char>('A' + ((id >> 10) & 0x1f) - 1);
  info.manufacturer[1] = static_cast<char>('A' + ((id >> 5) & 0x1f) - 1);
  info.manufacturer[2] = static_cast<char>('A' + (id & 0x1f) - 1);
  info.productCode = static_cast<uint16_t>(edid[10] | (edid[11] << 8));
  info.version = edid[18];
  info.revision = edid[19];

  Timing preferred;
  if (parseEdidPreferredTiming(edid, length, preferred))
  {
    info.hasPreferredTiming = true;
    info.preferredWidth = preferred.width;
    info.preferredHeight = preferred.height;
    info.preferredPixelClockKhz = preferred.pixelClockKhz;
  }
}

// One DL-1xx adapter.
//
// Commands accumulate in a buffer borrowed from the USB queue and are submitted
// when the buffer fills or when flush() is called, so a screen update becomes a
// few large transfers rather than many small ones.
class Dl1xxDevice
{
public:
  explicit Dl1xxDevice(EspUsbHost &host) : host_(host) {}

  // Finds a DL-1xx adapter, claims its vendor interface, selects the standard
  // channel and starts the bulk OUT queue. Does not program a mode.
  bool begin(uint8_t address = ESP_USB_HOST_ANY_ADDRESS)
  {
    const uint8_t found = findAdapter(address);
    if (found == 0)
    {
      return false;
    }
    if (!host_.vendorOpen(found))
    {
      return false;
    }
    // The adapter pairs its bulk OUT with an interrupt IN this driver does not
    // use, so vendorOpen() must have picked a bulk OUT.
    if (host_.vendorOutEndpoint(found) == 0)
    {
      return false;
    }
    if (!host_.vendorWriteQueueBegin(QUEUE_DEPTH, QUEUE_BUFFER_BYTES, found))
    {
      return false;
    }
    if (!host_.vendorControlOut(REQUEST_CHANNEL_KEY, 0, 0, CHANNEL_KEY, sizeof(CHANNEL_KEY), found))
    {
      host_.vendorWriteQueueEnd(found);
      return false;
    }

    address_ = found;
    hasMode_ = false;
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
    host_.vendorWriteQueueEnd(address_);
    address_ = 0;
    hasMode_ = false;
    generation_++;
  }

  bool ready() const { return address_ != 0 && hasMode_; }
  bool opened() const { return address_ != 0; }
  uint8_t address() const { return address_; }
  // The timing is stored by value, so a caller may pass a Timing that lives on
  // the stack (an EDID-derived one, for instance).
  const Timing *mode() const { return hasMode_ ? &mode_ : nullptr; }
  uint16_t width() const { return hasMode_ ? mode_.width : 0; }
  uint16_t height() const { return hasMode_ ? mode_.height : 0; }

  // Bumped whenever the device or its mode changes. A consumer that caches what
  // is on screen (a diff-transfer canvas, say) must discard that cache when this
  // changes.
  uint32_t generation() const { return generation_; }

  // Reads one 128-byte EDID block, one byte per control transfer.
  bool readEdid(uint8_t *edid, size_t length)
  {
    if (address_ == 0 || !edid || length < EDID_BLOCK_SIZE)
    {
      return false;
    }
    for (size_t i = 0; i < EDID_BLOCK_SIZE; i++)
    {
      uint8_t pair[2] = {0, 0};
      size_t actual = 0;
      if (!host_.vendorControlIn(REQUEST_EDID_BYTE, static_cast<uint16_t>(i << 8), EDID_INDEX, pair,
                                 sizeof(pair), &actual, address_))
      {
        return false;
      }
      // The second byte carries the value.
      edid[i] = pair[1];
    }
    return true;
  }

  // Programs a mode and remembers it. The unlock write at the end of the
  // sequence is what applies it.
  bool setMode(const Timing &timing)
  {
    if (address_ == 0)
    {
      return false;
    }
    // A mode set must go out as one uninterrupted sequence.
    if (!flush())
    {
      return false;
    }
    if (!ensureBuffer(MODE_SET_BYTES))
    {
      return false;
    }
    if (!writeModeSet(*buffer_, timing))
    {
      releaseBuffer();
      return false;
    }
    mode_ = timing;
    hasMode_ = true;
    generation_++;
    return flush();
  }

  // Re-sends the current mode. A monitor-side HPD event (unplugging the monitor,
  // a capture device closing) blanks the output permanently; pixel writes do not
  // bring it back, but re-running the mode registers does.
  bool resendMode()
  {
    if (!hasMode_)
    {
      return false;
    }
    const Timing current = mode_;
    return setMode(current);
  }

  // Byte address of a pixel in the device frame buffer.
  uint32_t pixelAddress(uint16_t x, uint16_t y) const
  {
    if (!hasMode_)
    {
      return 0;
    }
    return (static_cast<uint32_t>(y) * mode_.width + x) * 2u;
  }

  // Writes RGB565 pixels (host byte order) starting at a byte address. The
  // frame buffer is linear, so a run may cross row boundaries; that is what
  // makes full-width bands the most efficient update shape.
  bool writeSpan(uint32_t byteAddress, const uint16_t *pixels, size_t count)
  {
    if (address_ == 0 || !pixels)
    {
      return false;
    }
    size_t done = 0;
    while (done < count)
    {
      // Room for at least a minimal command, else start a new transfer.
      if (!ensureBuffer(maxRleCommandBytes(1)))
      {
        return false;
      }
      const size_t consumed =
          buffer_->writePixelsRle(byteAddress + static_cast<uint32_t>(done * 2), pixels + done,
                                  count - done);
      if (consumed == 0)
      {
        // The buffer could not take another command; submit and retry once.
        if (!submitBuffer())
        {
          return false;
        }
        if (!ensureBuffer(maxRleCommandBytes(1)))
        {
          return false;
        }
        if (buffer_->writePixelsRle(byteAddress + static_cast<uint32_t>(done * 2), pixels + done,
                                    count - done) == 0)
        {
          return false;
        }
        continue;
      }
      done += consumed;
    }
    return true;
  }

  bool writePixels(uint16_t x, uint16_t y, const uint16_t *pixels, size_t count)
  {
    return writeSpan(pixelAddress(x, y), pixels, count);
  }

  // Same as writeSpan() for pixels already stored as big-endian RGB565 byte
  // pairs, which is LovyanGFX's rgb565_2Byte layout. No per-pixel conversion.
  bool writeSpanBigEndian(uint32_t byteAddress, const uint8_t *pixels, size_t count)
  {
    if (address_ == 0 || !pixels)
    {
      return false;
    }
    size_t done = 0;
    while (done < count)
    {
      if (!ensureBuffer(maxRleCommandBytes(1)))
      {
        return false;
      }
      const uint32_t at = byteAddress + static_cast<uint32_t>(done * 2);
      size_t consumed = buffer_->writePixelsRleBigEndian(at, pixels + done * 2, count - done);
      if (consumed == 0)
      {
        if (!submitBuffer() || !ensureBuffer(maxRleCommandBytes(1)))
        {
          return false;
        }
        consumed = buffer_->writePixelsRleBigEndian(at, pixels + done * 2, count - done);
        if (consumed == 0)
        {
          return false;
        }
      }
      done += consumed;
    }
    return true;
  }

  // Fills a run with one color. The encoder turns 256 identical pixels into a
  // 10-byte command, so this is cheap on the wire.
  bool fillRun(uint32_t byteAddress, uint16_t color, size_t count)
  {
    uint16_t chunk[SOLID_CHUNK_PIXELS];
    for (size_t i = 0; i < SOLID_CHUNK_PIXELS; i++)
    {
      chunk[i] = color;
    }
    size_t done = 0;
    while (done < count)
    {
      const size_t step = (count - done) < SOLID_CHUNK_PIXELS ? (count - done) : SOLID_CHUNK_PIXELS;
      if (!writeSpan(byteAddress + static_cast<uint32_t>(done * 2), chunk, step))
      {
        return false;
      }
      done += step;
    }
    return true;
  }

  bool fillScreen(uint16_t color)
  {
    if (!hasMode_)
    {
      return false;
    }
    return fillRun(0, color, static_cast<size_t>(mode_.width) * mode_.height);
  }

  // Submits whatever is buffered plus the device-side flush command, without
  // waiting for completion. Keeps pixels moving without stalling the caller, so
  // this is what a panel calls when it finishes a drawing operation.
  bool push()
  {
    if (address_ == 0)
    {
      return false;
    }
    if (!buffer_ || buffer_->length() == 0)
    {
      releaseBuffer();
      return true;
    }
    if (buffer_->remaining() >= 2)
    {
      buffer_->flush();
    }
    return submitBuffer();
  }

  // push() plus a wait for the queue to drain. Call this only where the caller
  // genuinely needs the pixels to have landed.
  bool flush()
  {
    if (!push())
    {
      return false;
    }
    return host_.vendorWriteFlush(FLUSH_TIMEOUT_MS, address_);
  }

  EspUsbHostVendorWriteStats stats() const { return host_.vendorWriteStats(address_); }
  void resetStats() { host_.vendorWriteStatsReset(address_); }

  // Finds a DL-1xx adapter: VID 0x17e9 with a vendor-specific interface.
  uint8_t findAdapter(uint8_t address = ESP_USB_HOST_ANY_ADDRESS) const
  {
    EspUsbHostDeviceInfo devices[ESP_USB_HOST_MAX_DEVICES];
    const size_t count = host_.getDevices(devices, ESP_USB_HOST_MAX_DEVICES);
    for (size_t i = 0; i < count; i++)
    {
      if (devices[i].vid != VID)
      {
        continue;
      }
      if (address != ESP_USB_HOST_ANY_ADDRESS && devices[i].address != address)
      {
        continue;
      }
      EspUsbHostInterfaceInfo interfaces[ESP_USB_HOST_MAX_INTERFACES];
      const size_t interfaceCount =
          host_.getInterfaces(devices[i].address, interfaces, ESP_USB_HOST_MAX_INTERFACES);
      for (size_t j = 0; j < interfaceCount; j++)
      {
        if (interfaces[j].interfaceClass == 0xff)
        {
          return devices[i].address;
        }
      }
    }
    return 0;
  }

private:
  // Borrows a queue slot if there is no current buffer, or submits and takes a
  // new one when the current buffer cannot hold `needed` more bytes.
  bool ensureBuffer(size_t needed)
  {
    if (buffer_ && buffer_->remaining() < needed)
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
    size_t capacity = 0;
    raw_ = host_.vendorWriteAcquire(&capacity, ACQUIRE_TIMEOUT_MS, address_);
    if (!raw_)
    {
      return false;
    }
    storage_ = CommandBuffer(raw_, capacity);
    buffer_ = &storage_;
    return buffer_->remaining() >= needed;
  }

  bool submitBuffer()
  {
    if (!buffer_ || !raw_)
    {
      return true;
    }
    const size_t length = buffer_->length();
    uint8_t *raw = raw_;
    buffer_ = nullptr;
    raw_ = nullptr;
    if (length == 0)
    {
      host_.vendorWriteRelease(raw, address_);
      return true;
    }
    return host_.vendorWriteSubmit(raw, length, address_);
  }

  void releaseBuffer()
  {
    if (raw_)
    {
      host_.vendorWriteRelease(raw_, address_);
    }
    buffer_ = nullptr;
    raw_ = nullptr;
  }

  EspUsbHost &host_;
  uint8_t address_ = 0;
  Timing mode_ = {};
  bool hasMode_ = false;
  uint32_t generation_ = 0;
  uint8_t *raw_ = nullptr;
  CommandBuffer storage_{nullptr, 0};
  CommandBuffer *buffer_ = nullptr;
};

} // namespace dl1xx
