// Turns a drawn RGB565 tile into the JPEG a key image upload expects, using the
// ESP32-P4's JPEG encoder peripheral.
//
// The pad decodes JPEG on the device, and the ESP32-P4 is the only target this
// example runs on anyway (its 1024-byte interrupt OUT endpoint needs the P4's
// high-speed port), so the hardware encoder is free to use here. Both buffers come
// from jpeg_alloc_encoder_mem() because the peripheral is a DMA master with its own
// alignment requirements.

#pragma once

#include "MacroPadN3Protocol.hpp"

#include <driver/jpeg_encode.h>
#include <stdint.h>
#include <string.h>

namespace n3
{

// Quality is a tradeoff against upload time: one packet leaves every millisecond,
// so a 4 kB image is four packets and about as many milliseconds on the wire.
static constexpr uint8_t KEY_IMAGE_QUALITY = 80;

// The panel does not display an uploaded tile the way it was stored: on the test
// device a bar drawn across the tile comes out running up it, so the key screens are
// rotated a quarter turn. The drawing calls below compensate by writing rotated,
// which keeps sketch coordinates upright.
//
// This is per-device and the only way to check it is to look at the panel. If what
// you draw comes out sideways, try 90 and 270; if it comes out upside down, 180; if
// your unit needs none of this, 0.
static constexpr uint16_t KEY_IMAGE_ROTATION = 90;

class KeyImageEncoder
{
public:
  ~KeyImageEncoder() { end(); }

  bool begin()
  {
    if (encoder_)
    {
      return true;
    }
    jpeg_encode_engine_cfg_t engineConfig = {};
    engineConfig.timeout_ms = 1000;
    if (jpeg_new_encoder_engine(&engineConfig, &encoder_) != ESP_OK)
    {
      encoder_ = nullptr;
      return false;
    }

    jpeg_encode_memory_alloc_cfg_t inputConfig = {};
    inputConfig.buffer_direction = JPEG_ENC_ALLOC_INPUT_BUFFER;
    pixels_ = static_cast<uint16_t *>(
        jpeg_alloc_encoder_mem(KEY_IMAGE_WIDTH * KEY_IMAGE_HEIGHT * sizeof(uint16_t), &inputConfig, &pixelsSize_));

    jpeg_encode_memory_alloc_cfg_t outputConfig = {};
    outputConfig.buffer_direction = JPEG_ENC_ALLOC_OUTPUT_BUFFER;
    jpeg_ = static_cast<uint8_t *>(jpeg_alloc_encoder_mem(JPEG_CAPACITY, &outputConfig, &jpegSize_));

    if (!pixels_ || !jpeg_)
    {
      end();
      return false;
    }
    return true;
  }

  void end()
  {
    if (encoder_)
    {
      jpeg_del_encoder_engine(encoder_);
      encoder_ = nullptr;
    }
    free(pixels_);
    free(jpeg_);
    pixels_ = nullptr;
    jpeg_ = nullptr;
    pixelsSize_ = 0;
    jpegSize_ = 0;
  }

  // The tile to draw into: KEY_IMAGE_WIDTH * KEY_IMAGE_HEIGHT pixels of RGB565,
  // row by row.
  uint16_t *pixels() { return pixels_; }
  static constexpr size_t pixelCount() { return KEY_IMAGE_WIDTH * KEY_IMAGE_HEIGHT; }

  void fill(uint16_t color)
  {
    if (!pixels_)
    {
      return;
    }
    for (size_t i = 0; i < pixelCount(); i++)
    {
      pixels_[i] = color;
    }
  }

  // Coordinates are the ones the sketch wants to see on the key, with y running
  // down. KEY_IMAGE_ROTATION is applied here, so nothing else has to know about it.
  void drawPixel(uint16_t x, uint16_t y, uint16_t color)
  {
    if (!pixels_ || x >= KEY_IMAGE_WIDTH || y >= KEY_IMAGE_HEIGHT)
    {
      return;
    }
    uint16_t px = x;
    uint16_t py = y;
    switch (KEY_IMAGE_ROTATION)
    {
    case 90:
      px = static_cast<uint16_t>(KEY_IMAGE_WIDTH - 1 - y);
      py = x;
      break;
    case 180:
      px = static_cast<uint16_t>(KEY_IMAGE_WIDTH - 1 - x);
      py = static_cast<uint16_t>(KEY_IMAGE_HEIGHT - 1 - y);
      break;
    case 270:
      px = y;
      py = static_cast<uint16_t>(KEY_IMAGE_HEIGHT - 1 - x);
      break;
    default:
      break;
    }
    pixels_[py * KEY_IMAGE_WIDTH + px] = color;
  }

  void fillRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color)
  {
    for (uint16_t row = 0; row < h; row++)
    {
      for (uint16_t col = 0; col < w; col++)
      {
        drawPixel(x + col, y + row, color);
      }
    }
  }

  // Encodes what has been drawn. The returned pointer stays valid until the next
  // encode() call.
  bool encode(const uint8_t *&jpeg, size_t &length)
  {
    if (!encoder_ || !pixels_ || !jpeg_)
    {
      return false;
    }
    jpeg_encode_cfg_t config = {};
    config.width = KEY_IMAGE_WIDTH;
    config.height = KEY_IMAGE_HEIGHT;
    config.src_type = JPEG_ENCODE_IN_FORMAT_RGB565;
    config.sub_sample = JPEG_DOWN_SAMPLING_YUV420;
    config.image_quality = KEY_IMAGE_QUALITY;

    uint32_t encoded = 0;
    if (jpeg_encoder_process(encoder_,
                             &config,
                             reinterpret_cast<const uint8_t *>(pixels_),
                             static_cast<uint32_t>(KEY_IMAGE_WIDTH * KEY_IMAGE_HEIGHT * sizeof(uint16_t)),
                             jpeg_,
                             static_cast<uint32_t>(jpegSize_),
                             &encoded) != ESP_OK)
    {
      return false;
    }
    jpeg = jpeg_;
    length = encoded;
    return true;
  }

  static uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b)
  {
    return static_cast<uint16_t>(((r & 0xf8) << 8) | ((g & 0xfc) << 3) | (b >> 3));
  }

private:
  // A 64x64 tile at quality 80 encodes to a couple of kB; this leaves room for a
  // busy pattern that compresses badly.
  static constexpr size_t JPEG_CAPACITY = 16 * 1024;

  jpeg_encoder_handle_t encoder_ = nullptr;
  uint16_t *pixels_ = nullptr;
  uint8_t *jpeg_ = nullptr;
  size_t pixelsSize_ = 0;
  size_t jpegSize_ = 0;
};

} // namespace n3
