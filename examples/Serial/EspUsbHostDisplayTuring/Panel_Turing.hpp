// LovyanGFX panel for the 3.5-inch USB smart screen.
//
// Include LovyanGFX before this header. The panel holds no frame buffer: the
// panel has its own, so every drawing operation turns straight into a
// DISPLAY_BITMAP rectangle followed by its pixels. That also means read-back is
// not possible.
//
// The panel's color depth is fixed at rgb565_nonswapped, whose memory layout is
// little-endian RGB565 -- exactly the wire format. Converted pixels therefore
// reach the USB buffer with no byte swapping.
//
// The protocol has no compression, so a rectangle always costs 2 bytes per pixel
// plus a 6-byte header. Sending fewer, larger rectangles is the only lever, and
// that is what makes the full-width bands of a tiled canvas the right update
// shape here.

#pragma once

#include "TuringDevice.hpp"

#include <stdlib.h>

namespace turing
{

class Panel_Turing : public lgfx::Panel_Device
{
public:
  Panel_Turing(TuringDevice *device = nullptr) : device_(device)
  {
    _write_depth = lgfx::color_depth_t::rgb565_nonswapped;
    _read_depth = lgfx::color_depth_t::rgb565_nonswapped;
  }

  ~Panel_Turing() { free(line_); }

  void setDevice(TuringDevice *device) { device_ = device; }
  TuringDevice *device() const { return device_; }

  // Adopts the size the device reports, so setOrientation() must run before
  // init() if the default portrait is not wanted. Allocates one row of
  // pixel-conversion scratch space, which is where LovyanGFX's converters write
  // before the bytes are handed to USB.
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

  // True when the device was reopened or rotated since init(). Anything caching
  // what is on screen must drop that cache and redraw.
  bool invalidated() const
  {
    return !device_ || !device_->ready() || device_->generation() != generation_;
  }

  void acknowledgeInvalidation() { generation_ = device_ ? device_->generation() : 0; }

  void beginTransaction(void) override {}

  // Hands the buffered bytes to USB without waiting, so drawing keeps flowing.
  void endTransaction(void) override
  {
    if (device_)
    {
      device_->push();
    }
  }

  lgfx::color_depth_t setColorDepth(lgfx::color_depth_t) override
  {
    // The wire format is 16 bpp RGB565 little-endian and nothing else.
    _write_depth = lgfx::color_depth_t::rgb565_nonswapped;
    _read_depth = lgfx::color_depth_t::rgb565_nonswapped;
    return _write_depth;
  }

  // Rotation is not supported here: the panel rotates itself, and mixing that
  // with a host-side rotation would apply it twice. Anything other than 0 is
  // forced back so a caller cannot silently get a mirrored image; use
  // TuringDevice::setOrientation() instead. Panel_Device leaves setRotation
  // pure virtual, so the state is set here rather than delegated.
  void setRotation(uint_fast8_t r) override
  {
    (void)r;
    _rotation = 0;
    _internal_rotation = 0;
    _width = _cfg.panel_width;
    _height = _cfg.panel_height;
  }

  void setInvert(bool) override {}
  void setSleep(bool enable) override
  {
    if (device_)
    {
      enable ? device_->screenOff() : device_->screenOn();
    }
  }
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

  // Waits for everything drawn so far to reach the panel.
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
    device_->fillRect(static_cast<uint16_t>(x), static_cast<uint16_t>(y), 1, 1,
                      static_cast<uint16_t>(rawcolor));
  }

  void writeFillRectPreclipped(uint_fast16_t x, uint_fast16_t y, uint_fast16_t w, uint_fast16_t h,
                               uint32_t rawcolor) override
  {
    if (!device_ || w == 0 || h == 0)
    {
      return;
    }
    // One rectangle covers the whole area: unlike a linear frame buffer this
    // protocol addresses in 2D, so there is nothing to split.
    device_->fillRect(static_cast<uint16_t>(x), static_cast<uint16_t>(y), static_cast<uint16_t>(w),
                      static_cast<uint16_t>(h), static_cast<uint16_t>(rawcolor));
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
      // format, so these bytes go out as they are.
      param->fp_copy(line_, 0, take, param);
      if (device_->beginBitmap(_xpos, _ypos, static_cast<uint16_t>(take), 1))
      {
        device_->writePixelBytes(line_, take);
      }
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

    // An opaque image is one rectangle whose pixels stream row by row, so the
    // whole band costs a single 6-byte header. This is the path a tiled canvas
    // takes, and the reason a band is much cheaper than its rows would be.
    const bool opaque = param->transp == lgfx::pixelcopy_t::NON_TRANSP;
    if (opaque)
    {
      if (!device_->beginBitmap(static_cast<uint16_t>(x), static_cast<uint16_t>(y),
                                static_cast<uint16_t>(w), static_cast<uint16_t>(h)))
      {
        return;
      }
    }

    for (uint_fast16_t row = 0; row < h; row++)
    {
      // fp_copy advances the source as it converts, so remember where this row
      // started and step one source row on afterwards.
      const uint32_t sourceX32 = param->src_x32;
      const uint32_t sourceY32 = param->src_y32;

      if (opaque)
      {
        param->fp_copy(line_, 0, w, param);
        device_->writePixelBytes(line_, w);
      }
      else
      {
        int32_t pos = 0;
        const int32_t end = static_cast<int32_t>(w);
        while (pos != end)
        {
          const int32_t copied = param->fp_copy(line_, pos, end, param);
          if (copied != pos)
          {
            // Only the converted range is written; transparent pixels are skipped
            // below and left as they are on screen. Each opaque run is its own
            // rectangle.
            const uint16_t runWidth = static_cast<uint16_t>(copied - pos);
            if (device_->beginBitmap(static_cast<uint16_t>(x + pos), static_cast<uint16_t>(y + row),
                                     runWidth, 1))
            {
              device_->writePixelBytes(line_ + static_cast<size_t>(pos) * 2, runWidth);
            }
            pos = copied;
            continue;
          }
          pos = param->fp_skip(pos, end, param);
        }
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

  // The protocol has no on-screen copy, and read-back is not possible, so there
  // is no way to implement this. Doing nothing beats writing garbage.
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

  TuringDevice *device_ = nullptr;
  uint8_t *line_ = nullptr;
  uint16_t _xpos = 0;
  uint16_t _ypos = 0;
  uint32_t generation_ = 0;
};

// Ready-made LGFX device wrapping the panel.
class LGFX_Turing : public lgfx::LGFX_Device
{
public:
  LGFX_Turing(TuringDevice *device = nullptr) : panel_(device) { setPanel(&panel_); }

  void setDevice(TuringDevice *device) { panel_.setDevice(device); }
  Panel_Turing &panel() { return panel_; }

private:
  Panel_Turing panel_;
};

} // namespace turing
