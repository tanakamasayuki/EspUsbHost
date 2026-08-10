// LovyanGFX panel for an AX206 USB display.
//
// Include LovyanGFX before this header. The panel holds no frame buffer, and
// neither does the device layer: the display accepts only whole-screen blits, so
// a frame is opened on beginTransaction(), pixels are streamed straight to USB as
// they are drawn, and the transaction closes on endTransaction(). Read-back is
// not possible.
//
// That makes the drawing order part of the contract. Pixels must arrive in
// reading order -- left to right, top to bottom. Skipping ahead is fine, and the
// gap is padded; going back to a pixel already sent is not, and such a write is
// dropped and counted. This is exactly the order LGFXVirtualScreen renders its
// tiles in, which is what this panel is meant to be driven by. Code that needs
// arbitrary drawing order needs a frame buffer of its own to draw into.
//
// The panel's color depth is fixed at rgb565_2Byte, whose memory layout is
// big-endian RGB565 -- exactly the wire format. Converted pixels therefore reach
// the USB buffer with no byte swapping.
//
// The protocol has no compression and no partial update, so a frame always costs
// 307,200 bytes however little of it changed.

#pragma once

#include "Ax206Device.hpp"

#include <stdlib.h>

namespace ax206
{

class Panel_Ax206 : public lgfx::Panel_Device
{
public:
  Panel_Ax206(Ax206Device *device = nullptr) : device_(device)
  {
    _write_depth = lgfx::color_depth_t::rgb565_2Byte;
    _read_depth = lgfx::color_depth_t::rgb565_2Byte;
  }

  ~Panel_Ax206() { free(line_); }

  void setDevice(Ax206Device *device) { device_ = device; }
  Ax206Device *device() const { return device_; }

  // Allocates one row of pixel-conversion scratch space, which is where
  // LovyanGFX's converters write before the bytes are handed to USB.
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

  // True when the device was reopened since init(). Anything caching what is on
  // screen must drop that cache and redraw.
  bool invalidated() const
  {
    return !device_ || !device_->ready() || device_->generation() != generation_;
  }

  void acknowledgeInvalidation() { generation_ = device_ ? device_->generation() : 0; }

  // A LovyanGFX transaction is one frame. LGFXVirtualScreen wraps its whole tile
  // loop in one startWrite/endWrite, so a rendered screen becomes exactly one
  // blit however many tiles it was drawn in.
  void beginTransaction(void) override
  {
    if (device_)
    {
      device_->beginFrame();
    }
  }

  void endTransaction(void) override
  {
    if (device_)
    {
      device_->endFrame();
    }
  }

  lgfx::color_depth_t setColorDepth(lgfx::color_depth_t) override
  {
    // The wire format is 16 bpp RGB565 big-endian and nothing else.
    _write_depth = lgfx::color_depth_t::rgb565_2Byte;
    _read_depth = lgfx::color_depth_t::rgb565_2Byte;
    return _write_depth;
  }

  // Rotation is not supported: the blit command addresses the panel in its native
  // landscape orientation, and this panel does not transform coordinates. Other
  // values are forced back to 0 so a caller cannot silently get a mirrored image.
  // Panel_Device leaves setRotation pure virtual, so the state is set here rather
  // than delegated.
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

  // The frame is closed by endTransaction(), which LovyanGFX calls right after
  // display(), so there is nothing to force out here.
  void waitDisplay(void) override {}

  void display(uint_fast16_t, uint_fast16_t, uint_fast16_t, uint_fast16_t) override {}

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
    if (!device_ || !device_->seekTo(static_cast<uint16_t>(x), static_cast<uint16_t>(y)))
    {
      return;
    }
    device_->writeFill(static_cast<uint16_t>(rawcolor), 1);
  }

  void writeFillRectPreclipped(uint_fast16_t x, uint_fast16_t y, uint_fast16_t w, uint_fast16_t h,
                               uint32_t rawcolor) override
  {
    if (!device_ || w == 0 || h == 0)
    {
      return;
    }
    const uint16_t color = static_cast<uint16_t>(rawcolor);
    // Full-width rows are one contiguous run in the stream, so they go out
    // without a seek between them -- which is the common case, a cleared screen.
    if (x == 0 && w == ax206::WIDTH)
    {
      if (device_->seekTo(0, static_cast<uint16_t>(y)))
      {
        device_->writeFill(color, static_cast<size_t>(w) * h);
      }
      return;
    }
    for (uint_fast16_t row = 0; row < h; row++)
    {
      if (!device_->seekTo(static_cast<uint16_t>(x), static_cast<uint16_t>(y + row)))
      {
        continue;
      }
      device_->writeFill(color, w);
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
      // format, so these bytes go out as they are.
      param->fp_copy(line_, 0, take, param);
      if (device_->seekTo(_xpos, _ypos))
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

    // An opaque image streams row by row into the open frame. This is the path a
    // tiled canvas takes, and when the tile spans the full width its rows are one
    // contiguous run needing no seek at all.
    const bool opaque = param->transp == lgfx::pixelcopy_t::NON_TRANSP;

    for (uint_fast16_t row = 0; row < h; row++)
    {
      // fp_copy advances the source as it converts, so remember where this row
      // started and step one source row on afterwards.
      const uint32_t sourceX32 = param->src_x32;
      const uint32_t sourceY32 = param->src_y32;

      if (opaque)
      {
        param->fp_copy(line_, 0, w, param);
        if (device_->seekTo(static_cast<uint16_t>(x), static_cast<uint16_t>(y + row)))
        {
          device_->writePixelBytes(line_, w);
        }
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
            // Only the converted range is written. Transparent pixels cannot be
            // left as they are -- nothing on this side knows what is under them,
            // and the stream has to carry every pixel of the frame -- so they
            // become the pad color when the next run seeks past them.
            const uint16_t runWidth = static_cast<uint16_t>(copied - pos);
            if (device_->seekTo(static_cast<uint16_t>(x + pos), static_cast<uint16_t>(y + row)))
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
  // is no way to implement this.
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

  Ax206Device *device_ = nullptr;
  uint8_t *line_ = nullptr;
  uint16_t _xpos = 0;
  uint16_t _ypos = 0;
  uint32_t generation_ = 0;
};

// Ready-made LGFX device wrapping the panel.
class LGFX_Ax206 : public lgfx::LGFX_Device
{
public:
  LGFX_Ax206(Ax206Device *device = nullptr) : panel_(device) { setPanel(&panel_); }

  void setDevice(Ax206Device *device) { panel_.setDevice(device); }
  Panel_Ax206 &panel() { return panel_; }

private:
  Panel_Ax206 panel_;
};

} // namespace ax206
