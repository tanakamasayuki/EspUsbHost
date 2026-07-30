// LovyanGFX panel for a DL-1xx USB graphics adapter.
//
// Include LovyanGFX before this header. The panel holds no frame buffer: the
// adapter has its own, so every drawing operation turns straight into RLE pixel
// commands. That also means read-back is not possible.
//
// The panel's color depth is fixed at rgb565_2Byte, whose memory layout is
// big-endian RGB565 -- exactly the DL-1xx wire format. Converted pixels therefore
// reach the encoder with no per-pixel byte swapping.

#pragma once

#include "Dl1xxDevice.hpp"

#include <stdlib.h>

namespace dl1xx
{

class Panel_Dl1xx : public lgfx::Panel_Device
{
public:
  Panel_Dl1xx(Dl1xxDevice *device = nullptr) : device_(device)
  {
    _write_depth = lgfx::color_depth_t::rgb565_2Byte;
    _read_depth = lgfx::color_depth_t::rgb565_2Byte;
  }

  ~Panel_Dl1xx() { free(line_); }

  void setDevice(Dl1xxDevice *device) { device_ = device; }
  Dl1xxDevice *device() const { return device_; }

  // Adopts the size of the mode the device has programmed, so setMode() must run
  // before init(). Allocates one row of pixel-conversion scratch space (3840
  // bytes at Full HD), which is where LovyanGFX's converters write before the
  // encoder reads them.
  bool init(bool use_reset) override
  {
    if (!device_ || !device_->ready())
    {
      return false;
    }
    _cfg.panel_width = device_->width();
    _cfg.panel_height = device_->height();
    _cfg.memory_width = device_->width();
    _cfg.memory_height = device_->height();
    _cfg.offset_x = 0;
    _cfg.offset_y = 0;

    free(line_);
    line_ = static_cast<uint8_t *>(malloc(static_cast<size_t>(device_->width()) * 2));
    if (!line_)
    {
      return false;
    }

    if (!lgfx::Panel_Device::init(use_reset))
    {
      return false;
    }
    generation_ = device_->generation();
    return true;
  }

  // True when the device was reopened or its mode changed since init(). Anything
  // caching what is on screen must drop that cache and redraw.
  bool invalidated() const
  {
    return !device_ || !device_->ready() || device_->generation() != generation_;
  }

  void acknowledgeInvalidation() { generation_ = device_ ? device_->generation() : 0; }

  void beginTransaction(void) override {}

  // Hands the buffered commands to USB without waiting, so drawing keeps flowing.
  void endTransaction(void) override
  {
    if (device_)
    {
      device_->push();
    }
  }

  lgfx::color_depth_t setColorDepth(lgfx::color_depth_t) override
  {
    // The wire format is 16 bpp RGB565; 24 bpp would need the second plane.
    _write_depth = lgfx::color_depth_t::rgb565_2Byte;
    _read_depth = lgfx::color_depth_t::rgb565_2Byte;
    return _write_depth;
  }

  // Rotation is not supported: the write paths address the device frame buffer
  // linearly, which only holds for rotation 0. Anything else is forced back to 0
  // so a caller cannot silently get a mirrored image. Panel_Device leaves
  // setRotation pure virtual, so the state is set here rather than delegated.
  void setRotation(uint_fast8_t r) override
  {
    (void)r;
    _rotation = 0;
    _internal_rotation = 0;
    _width = _cfg.panel_width;
    _height = _cfg.panel_height;
  }

  void setInvert(bool) override {}
  void setSleep(bool) override {}
  void setPowerSave(bool) override {}
  void writeCommand(uint32_t, uint_fast8_t) override {}
  void writeData(uint32_t, uint_fast8_t) override {}
  void initBus(void) override {}
  void releaseBus(void) override {}
  void initDMA(void) override {}
  void waitDMA(void) override {}
  bool dmaBusy(void) override { return false; }
  bool displayBusy(void) override { return false; }
  bool isReadable(void) const override { return false; }
  bool isBusShared(void) const override { return false; }

  // Waits for everything drawn so far to reach the adapter.
  void waitDisplay(void) override
  {
    if (device_)
    {
      device_->flush();
    }
  }

  void display(uint_fast16_t, uint_fast16_t, uint_fast16_t, uint_fast16_t) override
  {
    if (device_)
    {
      device_->flush();
    }
  }

  void setWindow(uint_fast16_t xs, uint_fast16_t ys, uint_fast16_t xe, uint_fast16_t ye) override
  {
    _xs = static_cast<uint16_t>(xs);
    _ys = static_cast<uint16_t>(ys);
    _xe = static_cast<uint16_t>(xe);
    _ye = static_cast<uint16_t>(ye);
    _xpos = static_cast<uint16_t>(xs);
    _ypos = static_cast<uint16_t>(ys);
  }

  void drawPixelPreclipped(uint_fast16_t x, uint_fast16_t y, uint32_t rawcolor) override
  {
    if (!device_)
    {
      return;
    }
    const uint16_t pixel = wireValue(rawcolor);
    device_->writeSpan(device_->pixelAddress(static_cast<uint16_t>(x), static_cast<uint16_t>(y)),
                       &pixel, 1);
  }

  void writeFillRectPreclipped(uint_fast16_t x, uint_fast16_t y, uint_fast16_t w, uint_fast16_t h,
                               uint32_t rawcolor) override
  {
    if (!device_ || w == 0 || h == 0)
    {
      return;
    }
    const uint16_t pixel = wireValue(rawcolor);
    // A full-width rectangle is one contiguous region of the device frame buffer,
    // so it goes out as a single run. This is the common case for the full-width
    // bands a tiled canvas produces.
    if (x == 0 && w == device_->width())
    {
      device_->fillRun(device_->pixelAddress(0, static_cast<uint16_t>(y)), pixel,
                       static_cast<size_t>(w) * h);
      return;
    }
    for (uint_fast16_t row = 0; row < h; row++)
    {
      device_->fillRun(device_->pixelAddress(static_cast<uint16_t>(x),
                                             static_cast<uint16_t>(y + row)),
                       pixel, w);
    }
  }

  // Stream of same-colored pixels following the window set by setWindow(),
  // wrapping row by row.
  void writeBlock(uint32_t rawcolor, uint32_t length) override
  {
    while (length != 0)
    {
      const uint32_t rowRemaining = static_cast<uint32_t>(_xe) + 1 - _xpos;
      uint32_t w = length < rowRemaining ? length : rowRemaining;
      uint32_t h = 1;
      // Whole rows of the same color collapse into one rectangle.
      if (_xpos == _xs && w == rowRemaining)
      {
        const uint32_t rows = length / rowRemaining;
        const uint32_t rowsLeft = static_cast<uint32_t>(_ye) + 1 - _ypos;
        h = rows < rowsLeft ? rows : rowsLeft;
        if (h == 0)
        {
          h = 1;
        }
      }
      writeFillRectPreclipped(_xpos, _ypos, w, h, rawcolor);
      const uint32_t painted = w * h;
      length -= painted;
      advance(painted);
    }
  }

  void writePixels(lgfx::pixelcopy_t *param, uint32_t length, bool use_dma) override
  {
    (void)use_dma;
    if (!device_ || !param || !line_)
    {
      return;
    }
    while (length != 0)
    {
      const uint32_t rowRemaining = static_cast<uint32_t>(_xe) + 1 - _xpos;
      const uint32_t take = length < rowRemaining ? length : rowRemaining;
      // fp_copy converts into the panel's write format, which is already the wire
      // format, so the encoder takes these bytes as they are.
      param->fp_copy(line_, 0, take, param);
      device_->writeSpanBigEndian(device_->pixelAddress(_xpos, _ypos), line_, take);
      length -= take;
      advance(take);
    }
  }

  void writeImage(uint_fast16_t x, uint_fast16_t y, uint_fast16_t w, uint_fast16_t h,
                  lgfx::pixelcopy_t *param, bool use_dma) override
  {
    (void)use_dma;
    if (!device_ || !param || !line_ || w == 0 || h == 0)
    {
      return;
    }
    for (uint_fast16_t row = 0; row < h; row++)
    {
      // fp_copy advances the source as it converts, so remember where this row
      // started and step one source row on afterwards.
      const uint32_t sourceX32 = param->src_x32;
      const uint32_t sourceY32 = param->src_y32;

      int32_t pos = 0;
      const int32_t end = static_cast<int32_t>(w);
      while (pos != end)
      {
        const int32_t copied = param->fp_copy(line_, pos, end, param);
        if (copied != pos)
        {
          // Only the converted range is written; transparent pixels are skipped
          // below and left as they are on screen.
          device_->writeSpanBigEndian(
              device_->pixelAddress(static_cast<uint16_t>(x + pos), static_cast<uint16_t>(y + row)),
              line_ + static_cast<size_t>(pos) * 2, static_cast<size_t>(copied - pos));
          pos = copied;
          continue;
        }
        pos = param->fp_skip(pos, end, param);
      }

      param->src_x32 = sourceX32;
      param->src_y32 = sourceY32 + (1u << lgfx::pixelcopy_t::FP_SCALE);
    }
  }

  // ARGB blending needs read-back, which this panel cannot do.
  void writeImageARGB(uint_fast16_t, uint_fast16_t, uint_fast16_t, uint_fast16_t,
                      lgfx::pixelcopy_t *) override
  {
  }

  // On-screen rectangle copies would need the AF 6A command; until that is
  // implemented, doing nothing beats reading back garbage.
  void copyRect(uint_fast16_t, uint_fast16_t, uint_fast16_t, uint_fast16_t, uint_fast16_t,
                uint_fast16_t) override
  {
  }

  uint32_t readCommand(uint_fast16_t, uint_fast8_t, uint_fast8_t) override { return 0; }
  uint32_t readData(uint_fast8_t, uint_fast8_t) override { return 0; }
  void readRect(uint_fast16_t, uint_fast16_t, uint_fast16_t, uint_fast16_t, void *,
                lgfx::pixelcopy_t *) override
  {
  }

private:
  // rgb565_2Byte keeps RGB565 byte-swapped in memory, so the raw color's low byte
  // is the first byte on the wire. The encoder takes a wire-order value (high
  // byte first), hence the swap here -- once per fill, not per pixel.
  static uint16_t wireValue(uint32_t rawcolor)
  {
    return static_cast<uint16_t>(((rawcolor & 0xff) << 8) | ((rawcolor >> 8) & 0xff));
  }

  // Walks the cursor `count` pixels through the current window.
  void advance(uint32_t count)
  {
    while (count != 0)
    {
      const uint32_t rowRemaining = static_cast<uint32_t>(_xe) + 1 - _xpos;
      if (count < rowRemaining)
      {
        _xpos = static_cast<uint16_t>(_xpos + count);
        return;
      }
      count -= rowRemaining;
      _xpos = _xs;
      if (++_ypos > _ye)
      {
        _ypos = _ys;
      }
    }
  }

  Dl1xxDevice *device_ = nullptr;
  uint8_t *line_ = nullptr;
  uint16_t _xpos = 0;
  uint16_t _ypos = 0;
  uint32_t generation_ = 0;
};

// Ready-made LGFX device wrapping the panel.
class LGFX_Dl1xx : public lgfx::LGFX_Device
{
public:
  LGFX_Dl1xx(Dl1xxDevice *device = nullptr) : panel_(device) { setPanel(&panel_); }

  void setDevice(Dl1xxDevice *device) { panel_.setDevice(device); }
  Panel_Dl1xx &panel() { return panel_; }

private:
  Panel_Dl1xx panel_;
};

} // namespace dl1xx
