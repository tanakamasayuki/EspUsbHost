// Host tests for the USB Video Class descriptor, payload and control helpers.
//
// Compiled and run on the host with g++ (no board required) by
// test_video_uvc.py, which extracts the helpers under test verbatim from
// src/EspUsbHost.h into "espusbhost_video_real.h". The checks therefore run
// against the production decoders, not a copy.
//
// Descriptor bytes below follow UVC 1.1 (the "USB Device Class Definition for
// Video Devices" 1.1 document) and its Payload Format documents. The MJPEG and
// uncompressed samples match what the sibling EspUsbDevice library emits, so the
// peer test and this test describe the same layouts.

#include "espusbhost_video_real.h"

#include <cstdio>
#include <cstring>
#include <vector>

namespace
{

int failures = 0;

void check(bool condition, const char *what)
{
  if (!condition)
  {
    printf("FAIL: %s\n", what);
    failures++;
  }
}

void checkEqual(unsigned long actual, unsigned long expected, const char *what)
{
  if (actual != expected)
  {
    printf("FAIL: %s (actual=%lu expected=%lu)\n", what, actual, expected);
    failures++;
  }
}

void appendU16(std::vector<uint8_t> &out, uint16_t value)
{
  out.push_back(static_cast<uint8_t>(value & 0xff));
  out.push_back(static_cast<uint8_t>(value >> 8));
}

void appendU32(std::vector<uint8_t> &out, uint32_t value)
{
  appendU16(out, static_cast<uint16_t>(value & 0xffff));
  appendU16(out, static_cast<uint16_t>(value >> 16));
}

// The 16-byte format GUIDs are {FourCC}-0000-0010-8000-00AA00389B71.
std::vector<uint8_t> formatGuid(const char *fourCc)
{
  std::vector<uint8_t> guid = {
      0, 0, 0, 0, 0x00, 0x00, 0x10, 0x00,
      0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71};
  for (int i = 0; i < 4; i++)
  {
    guid[i] = static_cast<uint8_t>(fourCc[i]);
  }
  return guid;
}

// VS_FORMAT_UNCOMPRESSED (bDescriptorSubtype 0x04), bLength 27.
std::vector<uint8_t> uncompressedFormat(uint8_t formatIndex,
                                        uint8_t frameCount,
                                        const char *fourCc,
                                        uint8_t bitsPerPixel,
                                        uint8_t defaultFrameIndex)
{
  std::vector<uint8_t> out = {27, 0x24, 0x04, formatIndex, frameCount};
  const std::vector<uint8_t> guid = formatGuid(fourCc);
  out.insert(out.end(), guid.begin(), guid.end());
  out.push_back(bitsPerPixel);
  out.push_back(defaultFrameIndex);
  out.push_back(0);  // bAspectRatioX
  out.push_back(0);  // bAspectRatioY
  out.push_back(0);  // bmInterlaceFlags
  out.push_back(0);  // bCopyProtect
  return out;
}

// VS_FORMAT_MJPEG (bDescriptorSubtype 0x06), bLength 11.
std::vector<uint8_t> mjpegFormat(uint8_t formatIndex,
                                 uint8_t frameCount,
                                 uint8_t defaultFrameIndex)
{
  return {11, 0x24, 0x06, formatIndex, frameCount, 0x01, defaultFrameIndex, 0, 0, 0, 0};
}

// VS_FRAME_UNCOMPRESSED (0x05) / VS_FRAME_MJPEG (0x07). A discrete descriptor
// lists bFrameIntervalType intervals; a continuous one (type 0) lists
// min/max/step instead.
std::vector<uint8_t> frameDescriptor(uint8_t subtype,
                                     uint8_t frameIndex,
                                     uint16_t width,
                                     uint16_t height,
                                     uint32_t maxFrameBufferSize,
                                     uint32_t defaultInterval,
                                     const std::vector<uint32_t> &intervals,
                                     bool continuous = false)
{
  std::vector<uint8_t> out = {0, 0x24, subtype, frameIndex, 0x00};
  appendU16(out, width);
  appendU16(out, height);
  appendU32(out, 1000000);             // dwMinBitRate
  appendU32(out, 100000000);           // dwMaxBitRate
  appendU32(out, maxFrameBufferSize);  // dwMaxVideoFrameBufferSize
  appendU32(out, defaultInterval);     // dwDefaultFrameInterval
  out.push_back(continuous ? 0 : static_cast<uint8_t>(intervals.size()));
  for (uint32_t interval : intervals)
  {
    appendU32(out, interval);
  }
  out[0] = static_cast<uint8_t>(out.size());
  return out;
}

// ---------------------------------------------------------------------------
// Format GUID decoding
// ---------------------------------------------------------------------------

void testFormatGuid()
{
  checkEqual(espUsbHostVideoFormatFromGuid(formatGuid("YUY2").data()),
             ESP_USB_HOST_VIDEO_FORMAT_YUY2, "YUY2 GUID");
  checkEqual(espUsbHostVideoFormatFromGuid(formatGuid("NV12").data()),
             ESP_USB_HOST_VIDEO_FORMAT_NV12, "NV12 GUID");

  // Some cameras spell the uncompressed YUY2 FourCC "UYVY"; it is a distinct
  // byte order and must not be reported as YUY2.
  checkEqual(espUsbHostVideoFormatFromGuid(formatGuid("UYVY").data()),
             ESP_USB_HOST_VIDEO_FORMAT_UNCOMPRESSED, "unknown FourCC falls back to uncompressed");

  // A GUID whose trailing 12 bytes are not the standard suffix is not a
  // FourCC-derived format at all.
  std::vector<uint8_t> foreign = formatGuid("YUY2");
  foreign[15] = 0x00;
  checkEqual(espUsbHostVideoFormatFromGuid(foreign.data()),
             ESP_USB_HOST_VIDEO_FORMAT_UNCOMPRESSED, "non-standard GUID suffix");

  checkEqual(espUsbHostVideoFormatFromGuid(nullptr),
             ESP_USB_HOST_VIDEO_FORMAT_UNKNOWN, "null GUID");

  check(strcmp(espUsbHostVideoFormatName(ESP_USB_HOST_VIDEO_FORMAT_MJPEG), "MJPEG") == 0,
        "MJPEG name");
  check(strcmp(espUsbHostVideoFormatName(ESP_USB_HOST_VIDEO_FORMAT_YUY2), "YUY2") == 0,
        "YUY2 name");
  check(espUsbHostVideoFormatName(0xfe) != nullptr, "unknown format still names something");
}

// ---------------------------------------------------------------------------
// Frame interval <-> frame rate
// ---------------------------------------------------------------------------

void testFrameIntervalConversion()
{
  // dwFrameInterval is in 100 ns units, so 30 fps is 333333.
  checkEqual(espUsbHostVideoFrameIntervalToFps(333333), 30, "333333 -> 30 fps");
  checkEqual(espUsbHostVideoFrameIntervalToFps(666666), 15, "666666 -> 15 fps");
  checkEqual(espUsbHostVideoFrameIntervalToFps(1000000), 10, "1000000 -> 10 fps");
  checkEqual(espUsbHostVideoFrameIntervalToFps(2000000), 5, "2000000 -> 5 fps");
  checkEqual(espUsbHostVideoFrameIntervalToFps(0), 0, "zero interval");

  checkEqual(espUsbHostVideoFpsToFrameInterval(30), 333333, "30 fps -> 333333");
  checkEqual(espUsbHostVideoFpsToFrameInterval(15), 666666, "15 fps -> 666666");
  checkEqual(espUsbHostVideoFpsToFrameInterval(0), 0, "zero fps");

  // The two are not exact inverses -- 10000000/30 does not divide evenly -- so
  // the round trip has to land back on the same rate rather than the same
  // interval.
  checkEqual(espUsbHostVideoFrameIntervalToFps(espUsbHostVideoFpsToFrameInterval(30)),
             30, "30 fps round trip");
  checkEqual(espUsbHostVideoFrameIntervalToFps(espUsbHostVideoFpsToFrameInterval(25)),
             25, "25 fps round trip");
}

// ---------------------------------------------------------------------------
// Isochronous packet size, including the high-speed high-bandwidth multiplier
// ---------------------------------------------------------------------------

void testIsocPayloadSize()
{
  // Bits 10:0 are the packet size; bits 12:11 are the number of *additional*
  // transactions per microframe. UVC alternate settings rely on this to offer
  // more than 1024 bytes, and picking an alternate by the raw wMaxPacketSize
  // would then understate its bandwidth by up to 3x.
  checkEqual(espUsbHostVideoIsocPayloadSize(0x0200), 512, "single transaction");
  checkEqual(espUsbHostVideoIsocPayloadSize(0x0C00), 2048, "two transactions of 1024");
  checkEqual(espUsbHostVideoIsocPayloadSize(0x13FC), 3 * 1020, "three transactions of 1020");
  checkEqual(espUsbHostVideoIsocPayloadSize(0x0000), 0, "unused endpoint");

  // 11b in bits 12:11 is reserved; treat it as a single transaction rather than
  // claiming bandwidth the device did not offer.
  checkEqual(espUsbHostVideoIsocPayloadSize(0x1BFC), 1020, "reserved multiplier");
}

// ---------------------------------------------------------------------------
// Format and frame descriptor decoding
// ---------------------------------------------------------------------------

void testDecodeMjpegFormat()
{
  EspUsbHostVideoStreamInfo stream;
  const std::vector<uint8_t> format = mjpegFormat(1, 2, 1);
  check(espUsbHostVideoDecodeFormatDescriptor(format.data(), stream), "MJPEG format accepted");
  checkEqual(stream.format, ESP_USB_HOST_VIDEO_FORMAT_MJPEG, "MJPEG format constant");
  checkEqual(stream.formatIndex, 1, "MJPEG bFormatIndex");
  checkEqual(stream.frameCount, 2, "MJPEG bNumFrameDescriptors");
  checkEqual(stream.defaultFrameIndex, 1, "MJPEG bDefaultFrameIndex");
  checkEqual(stream.bitsPerPixel, 0, "MJPEG has no bits per pixel");
}

void testDecodeUncompressedFormat()
{
  EspUsbHostVideoStreamInfo stream;
  const std::vector<uint8_t> format = uncompressedFormat(2, 1, "YUY2", 16, 1);
  check(espUsbHostVideoDecodeFormatDescriptor(format.data(), stream), "YUY2 format accepted");
  checkEqual(stream.format, ESP_USB_HOST_VIDEO_FORMAT_YUY2, "YUY2 format constant");
  checkEqual(stream.formatIndex, 2, "YUY2 bFormatIndex");
  checkEqual(stream.bitsPerPixel, 16, "YUY2 bBitsPerPixel");
  checkEqual(stream.defaultFrameIndex, 1, "YUY2 bDefaultFrameIndex");
}

void testDecodeFormatRejects()
{
  EspUsbHostVideoStreamInfo stream;
  check(!espUsbHostVideoDecodeFormatDescriptor(nullptr, stream), "null format");

  // Truncated: an MJPEG format descriptor shorter than its fixed 11 bytes.
  std::vector<uint8_t> shortMjpeg = mjpegFormat(1, 1, 1);
  shortMjpeg[0] = 6;
  shortMjpeg.resize(6);
  check(!espUsbHostVideoDecodeFormatDescriptor(shortMjpeg.data(), stream), "truncated MJPEG format");

  // Truncated uncompressed: the GUID would run past bLength.
  std::vector<uint8_t> shortUncompressed = uncompressedFormat(1, 1, "YUY2", 16, 1);
  shortUncompressed[0] = 20;
  check(!espUsbHostVideoDecodeFormatDescriptor(shortUncompressed.data(), stream),
        "truncated uncompressed format");

  // A subtype this host does not decode (VS_FORMAT_MPEG2TS).
  std::vector<uint8_t> mpeg2ts = {23, 0x24, 0x0A, 1, 0, 0, 0, 0};
  mpeg2ts.resize(23);
  check(!espUsbHostVideoDecodeFormatDescriptor(mpeg2ts.data(), stream), "unsupported format subtype");

  // A descriptor that is not CS_INTERFACE at all.
  std::vector<uint8_t> endpointDesc = {7, 0x05, 0x81, 0x05, 0x00, 0x04, 1};
  check(!espUsbHostVideoDecodeFormatDescriptor(endpointDesc.data(), stream), "endpoint descriptor");
}

void testDecodeDiscreteFrame()
{
  EspUsbHostVideoStreamInfo stream;
  const std::vector<uint8_t> frame = frameDescriptor(
      0x07, 1, 320, 240, 320 * 240 * 2, 666666, {333333, 666666, 1000000});
  check(espUsbHostVideoDecodeFrameDescriptor(frame.data(), stream), "MJPEG frame accepted");
  checkEqual(stream.frameIndex, 1, "bFrameIndex");
  checkEqual(stream.width, 320, "wWidth");
  checkEqual(stream.height, 240, "wHeight");
  checkEqual(stream.maxVideoFrameBufferSize, 320 * 240 * 2, "dwMaxVideoFrameBufferSize");
  checkEqual(stream.frameInterval, 666666, "dwDefaultFrameInterval");
  checkEqual(stream.frameIntervalCount, 3, "discrete interval count");
  checkEqual(stream.frameIntervals[0], 333333, "interval 0");
  checkEqual(stream.frameIntervals[2], 1000000, "interval 2");
  checkEqual(stream.frameIntervalMin, 0, "discrete leaves min unset");
  checkEqual(stream.frameIntervalStep, 0, "discrete leaves step unset");
}

void testDecodeContinuousFrame()
{
  EspUsbHostVideoStreamInfo stream;
  const std::vector<uint8_t> frame = frameDescriptor(
      0x05, 3, 640, 480, 640 * 480 * 2, 666666,
      {333333, 2000000, 166666}, /*continuous=*/true);
  check(espUsbHostVideoDecodeFrameDescriptor(frame.data(), stream), "continuous frame accepted");
  checkEqual(stream.frameIndex, 3, "continuous bFrameIndex");
  checkEqual(stream.width, 640, "continuous wWidth");
  checkEqual(stream.frameIntervalCount, 0, "continuous has no discrete list");
  checkEqual(stream.frameIntervalMin, 333333, "dwMinFrameInterval");
  checkEqual(stream.frameIntervalMax, 2000000, "dwMaxFrameInterval");
  checkEqual(stream.frameIntervalStep, 166666, "dwFrameIntervalStep");
}

void testDecodeFrameClampsIntervalList()
{
  // A camera may advertise more intervals than the fixed array holds. The
  // decoder must keep the first ESP_USB_HOST_MAX_VIDEO_FRAME_INTERVALS and
  // still report the descriptor as decoded rather than dropping the frame size.
  std::vector<uint32_t> many;
  for (size_t i = 0; i < ESP_USB_HOST_MAX_VIDEO_FRAME_INTERVALS + 3; i++)
  {
    many.push_back(static_cast<uint32_t>(333333 * (i + 1)));
  }
  EspUsbHostVideoStreamInfo stream;
  const std::vector<uint8_t> frame = frameDescriptor(0x07, 1, 1280, 720, 0, 333333, many);
  check(espUsbHostVideoDecodeFrameDescriptor(frame.data(), stream), "over-long interval list accepted");
  checkEqual(stream.width, 1280, "width survives clamping");
  checkEqual(stream.frameIntervalCount, ESP_USB_HOST_MAX_VIDEO_FRAME_INTERVALS,
             "interval count clamped");
  checkEqual(stream.frameIntervals[0], 333333, "first interval kept");
}

void testDecodeFrameRejects()
{
  EspUsbHostVideoStreamInfo stream;
  check(!espUsbHostVideoDecodeFrameDescriptor(nullptr, stream), "null frame");

  // bLength shorter than the 26-byte fixed part.
  std::vector<uint8_t> truncated = frameDescriptor(0x07, 1, 320, 240, 0, 333333, {333333});
  truncated[0] = 20;
  check(!espUsbHostVideoDecodeFrameDescriptor(truncated.data(), stream), "truncated frame");

  // bFrameIntervalType claims three intervals but bLength only covers one.
  std::vector<uint8_t> lying = frameDescriptor(0x07, 1, 320, 240, 0, 333333, {333333});
  lying[25] = 3;
  check(!espUsbHostVideoDecodeFrameDescriptor(lying.data(), stream), "interval count past bLength");

  // Continuous form needs 12 bytes of min/max/step past the fixed part.
  std::vector<uint8_t> shortContinuous =
      frameDescriptor(0x05, 1, 320, 240, 0, 333333, {333333, 666666}, true);
  shortContinuous[0] = 30;
  check(!espUsbHostVideoDecodeFrameDescriptor(shortContinuous.data(), stream),
        "truncated continuous range");

  // A still-image frame descriptor is not a frame descriptor.
  std::vector<uint8_t> still = frameDescriptor(0x03, 1, 320, 240, 0, 333333, {333333});
  check(!espUsbHostVideoDecodeFrameDescriptor(still.data(), stream), "still image subtype");
}

// ---------------------------------------------------------------------------
// Frame interval matching
// ---------------------------------------------------------------------------

EspUsbHostVideoStreamInfo discreteStream(uint8_t format,
                                         uint16_t width,
                                         uint16_t height,
                                         const std::vector<uint32_t> &intervals)
{
  EspUsbHostVideoStreamInfo stream;
  stream.format = format;
  stream.formatIndex = 1;
  stream.frameIndex = 1;
  stream.width = width;
  stream.height = height;
  stream.frameInterval = intervals.empty() ? 0 : intervals.front();
  stream.frameIntervalCount = static_cast<uint8_t>(intervals.size());
  for (size_t i = 0; i < intervals.size() && i < ESP_USB_HOST_MAX_VIDEO_FRAME_INTERVALS; i++)
  {
    stream.frameIntervals[i] = intervals[i];
  }
  return stream;
}

void testSupportsFrameInterval()
{
  const EspUsbHostVideoStreamInfo discrete =
      discreteStream(ESP_USB_HOST_VIDEO_FORMAT_MJPEG, 320, 240, {333333, 666666});
  check(espUsbHostVideoStreamSupportsFrameInterval(discrete, 333333), "discrete exact");
  check(espUsbHostVideoStreamSupportsFrameInterval(discrete, 666666), "discrete second");
  check(!espUsbHostVideoStreamSupportsFrameInterval(discrete, 400000), "discrete miss");
  check(espUsbHostVideoStreamSupportsFrameInterval(discrete, 0), "zero means any");

  EspUsbHostVideoStreamInfo continuous;
  continuous.format = ESP_USB_HOST_VIDEO_FORMAT_YUY2;
  continuous.frameIntervalMin = 333333;
  continuous.frameIntervalMax = 2000000;
  continuous.frameIntervalStep = 333333;
  check(espUsbHostVideoStreamSupportsFrameInterval(continuous, 333333), "continuous min");
  check(espUsbHostVideoStreamSupportsFrameInterval(continuous, 2000000), "continuous max");
  check(espUsbHostVideoStreamSupportsFrameInterval(continuous, 999999), "continuous on step");
  check(!espUsbHostVideoStreamSupportsFrameInterval(continuous, 200000), "continuous below min");
  check(!espUsbHostVideoStreamSupportsFrameInterval(continuous, 3000000), "continuous above max");
  check(!espUsbHostVideoStreamSupportsFrameInterval(continuous, 500000), "continuous off step");

  // A zero step means the device accepts anything in range; UVC allows it and
  // rejecting every value would make such a camera unusable.
  continuous.frameIntervalStep = 0;
  check(espUsbHostVideoStreamSupportsFrameInterval(continuous, 500000), "zero step accepts any");
}

void testNearestFrameInterval()
{
  const EspUsbHostVideoStreamInfo discrete =
      discreteStream(ESP_USB_HOST_VIDEO_FORMAT_MJPEG, 320, 240, {333333, 666666, 2000000});
  checkEqual(espUsbHostVideoStreamNearestFrameInterval(discrete, 666666), 666666, "nearest exact");
  checkEqual(espUsbHostVideoStreamNearestFrameInterval(discrete, 700000), 666666, "nearest below");
  checkEqual(espUsbHostVideoStreamNearestFrameInterval(discrete, 100000), 333333, "nearest fastest");
  checkEqual(espUsbHostVideoStreamNearestFrameInterval(discrete, 9000000), 2000000, "nearest slowest");

  // Zero means "no preference": answer with the stream's own default.
  checkEqual(espUsbHostVideoStreamNearestFrameInterval(discrete, 0), 333333, "no preference");

  EspUsbHostVideoStreamInfo continuous;
  continuous.frameIntervalMin = 333333;
  continuous.frameIntervalMax = 2000000;
  continuous.frameIntervalStep = 333333;
  continuous.frameInterval = 666666;
  checkEqual(espUsbHostVideoStreamNearestFrameInterval(continuous, 100000), 333333, "continuous clamps low");
  checkEqual(espUsbHostVideoStreamNearestFrameInterval(continuous, 9000000), 2000000, "continuous clamps high");
  checkEqual(espUsbHostVideoStreamNearestFrameInterval(continuous, 700000), 666666, "continuous snaps to step");
  checkEqual(espUsbHostVideoStreamNearestFrameInterval(continuous, 0), 666666, "continuous default");
}

// ---------------------------------------------------------------------------
// Stream selection
// ---------------------------------------------------------------------------

std::vector<EspUsbHostVideoStreamInfo> cameraStreams()
{
  // A typical UVC webcam: MJPEG at three sizes plus YUY2 at the smallest, which
  // is the shape the sibling EspUsbDevice UVC peer presents.
  std::vector<EspUsbHostVideoStreamInfo> streams;
  streams.push_back(discreteStream(ESP_USB_HOST_VIDEO_FORMAT_MJPEG, 320, 240, {333333, 666666}));
  streams.push_back(discreteStream(ESP_USB_HOST_VIDEO_FORMAT_MJPEG, 640, 480, {666666}));
  streams.push_back(discreteStream(ESP_USB_HOST_VIDEO_FORMAT_MJPEG, 1280, 720, {1000000}));
  streams.push_back(discreteStream(ESP_USB_HOST_VIDEO_FORMAT_YUY2, 160, 120, {666666}));
  for (size_t i = 0; i < streams.size(); i++)
  {
    streams[i].frameIndex = static_cast<uint8_t>(i + 1);
  }
  return streams;
}

void testSelectExact()
{
  const std::vector<EspUsbHostVideoStreamInfo> streams = cameraStreams();
  const EspUsbHostVideoStreamSelection selection = espUsbHostSelectVideoStream(
      streams.data(), streams.size(), ESP_USB_HOST_VIDEO_FORMAT_MJPEG, 640, 480, 15);
  check(static_cast<bool>(selection), "exact selection succeeded");
  checkEqual(selection.index, 1, "exact picked 640x480");
  checkEqual(selection.frameInterval, 666666, "exact interval");
}

void testSelectWildcards()
{
  const std::vector<EspUsbHostVideoStreamInfo> streams = cameraStreams();

  // Zero width/height/fps means "any": the highest-scoring stream wins, which is
  // the largest frame this host can name.
  const EspUsbHostVideoStreamSelection any = espUsbHostSelectVideoStream(
      streams.data(), streams.size(), ESP_USB_HOST_VIDEO_FORMAT_UNKNOWN, 0, 0, 0);
  check(static_cast<bool>(any), "wildcard selection succeeded");
  checkEqual(streams[any.index].width, 1280, "wildcard prefers the largest frame");

  // Naming only the format still picks the largest frame of that format.
  const EspUsbHostVideoStreamSelection yuy2 = espUsbHostSelectVideoStream(
      streams.data(), streams.size(), ESP_USB_HOST_VIDEO_FORMAT_YUY2, 0, 0, 0);
  check(static_cast<bool>(yuy2), "format-only selection succeeded");
  checkEqual(yuy2.index, 3, "format-only picked the YUY2 stream");

  // Naming only the size picks whichever format offers it.
  const EspUsbHostVideoStreamSelection size = espUsbHostSelectVideoStream(
      streams.data(), streams.size(), ESP_USB_HOST_VIDEO_FORMAT_UNKNOWN, 320, 240, 0);
  check(static_cast<bool>(size), "size-only selection succeeded");
  checkEqual(size.index, 0, "size-only picked 320x240");

  // Naming only the rate picks a stream that offers it.
  const EspUsbHostVideoStreamSelection fps = espUsbHostSelectVideoStream(
      streams.data(), streams.size(), ESP_USB_HOST_VIDEO_FORMAT_UNKNOWN, 0, 0, 30);
  check(static_cast<bool>(fps), "rate-only selection succeeded");
  checkEqual(fps.frameInterval, 333333, "rate-only interval");
  checkEqual(streams[fps.index].width, 320, "only 320x240 offers 30 fps");
}

void testSelectRejects()
{
  const std::vector<EspUsbHostVideoStreamInfo> streams = cameraStreams();

  // A size no stream offers must fail rather than silently substituting one:
  // a caller that sized its frame buffer for 800x600 would overflow it.
  check(!espUsbHostSelectVideoStream(streams.data(), streams.size(),
                                     ESP_USB_HOST_VIDEO_FORMAT_UNKNOWN, 800, 600, 0),
        "unavailable size rejected");
  check(!espUsbHostSelectVideoStream(streams.data(), streams.size(),
                                     ESP_USB_HOST_VIDEO_FORMAT_H264, 0, 0, 0),
        "unavailable format rejected");
  check(!espUsbHostSelectVideoStream(streams.data(), streams.size(),
                                     ESP_USB_HOST_VIDEO_FORMAT_MJPEG, 640, 480, 30),
        "unavailable rate rejected");
  check(!espUsbHostSelectVideoStream(nullptr, 0, ESP_USB_HOST_VIDEO_FORMAT_UNKNOWN, 0, 0, 0),
        "empty stream list rejected");
}

void testSelectSkipsUnstartable()
{
  // A format the camera advertises on an alternate this host did not claim must
  // not be selected: starting it would fail at the endpoint.
  std::vector<EspUsbHostVideoStreamInfo> streams = cameraStreams();
  streams[1].startable = false;
  const EspUsbHostVideoStreamSelection selection = espUsbHostSelectVideoStream(
      streams.data(), streams.size(), ESP_USB_HOST_VIDEO_FORMAT_MJPEG, 640, 480, 0);
  check(!selection, "unstartable stream not selected");
}

// ---------------------------------------------------------------------------
// Payload header decoding
// ---------------------------------------------------------------------------

void testPayloadHeaderMinimal()
{
  // The smallest legal header: bHeaderLength 2, bmHeaderInfo with EOH set.
  const std::vector<uint8_t> data = {2, 0x80};
  EspUsbHostVideoPayloadHeader header;
  check(espUsbHostVideoDecodePayloadHeader(data.data(), data.size(), header), "minimal header");
  checkEqual(header.headerLength, 2, "header length");
  check(!header.frameId, "FID clear");
  check(!header.endOfFrame, "EOF clear");
  check(!header.error, "error clear");
  check(header.endOfHeader, "EOH set");
  check(!header.hasPresentationTime, "no PTS");
  checkEqual(header.payloadOffset, 2, "payload starts after the header");
}

void testPayloadHeaderWithTimestamps()
{
  // bmHeaderInfo 0x8F = EOH | SCR | PTS | EOF | FID.
  std::vector<uint8_t> data = {12, 0x8F};
  appendU32(data, 0x11223344);  // dwPresentationTime
  appendU32(data, 0x55667788);  // scrSourceClock low
  appendU16(data, 0x99AA);      // scrSourceClock high
  EspUsbHostVideoPayloadHeader header;
  check(espUsbHostVideoDecodePayloadHeader(data.data(), data.size(), header), "PTS+SCR header");
  check(header.frameId, "FID set");
  check(header.endOfFrame, "EOF set");
  check(header.hasPresentationTime, "PTS present");
  checkEqual(header.presentationTime, 0x11223344, "PTS value");
  check(header.hasSourceClock, "SCR present");
  checkEqual(header.sourceClock, 0x55667788, "SCR value");
  checkEqual(header.payloadOffset, 12, "payload offset past PTS and SCR");
}

void testPayloadHeaderError()
{
  // bit 6 is the error bit; the payload after it is not usable.
  const std::vector<uint8_t> data = {2, 0xC0};
  EspUsbHostVideoPayloadHeader header;
  check(espUsbHostVideoDecodePayloadHeader(data.data(), data.size(), header), "error header decodes");
  check(header.error, "error bit set");
}

void testPayloadHeaderRejects()
{
  EspUsbHostVideoPayloadHeader header;
  check(!espUsbHostVideoDecodePayloadHeader(nullptr, 8, header), "null payload");

  // A zero-length isochronous packet is normal when the camera has nothing to
  // send; it is not a header.
  const std::vector<uint8_t> empty = {};
  check(!espUsbHostVideoDecodePayloadHeader(empty.data(), 0, header), "empty packet");

  // bHeaderLength below the 2-byte minimum, and above the packet.
  const std::vector<uint8_t> tooSmall = {1, 0x80};
  check(!espUsbHostVideoDecodePayloadHeader(tooSmall.data(), tooSmall.size(), header),
        "header length below minimum");
  const std::vector<uint8_t> tooBig = {12, 0x8F, 0, 0};
  check(!espUsbHostVideoDecodePayloadHeader(tooBig.data(), tooBig.size(), header),
        "header length past packet");

  // bHeaderLength that does not cover the PTS and SCR its flags claim: the
  // fields would be read from the caller's image data.
  const std::vector<uint8_t> lying = {4, 0x8C, 0, 0};
  check(!espUsbHostVideoDecodePayloadHeader(lying.data(), lying.size(), header),
        "header too short for its own flags");
}

// ---------------------------------------------------------------------------
// Probe / Commit control payload
// ---------------------------------------------------------------------------

void testProbeControlRoundTrip()
{
  EspUsbHostVideoProbeControl control;
  control.formatIndex = 1;
  control.frameIndex = 2;
  control.frameInterval = 666666;
  control.maxVideoFrameSize = 320 * 240 * 2;
  control.maxPayloadTransferSize = 1024;

  uint8_t buffer[ESP_USB_HOST_VIDEO_PROBE_MAX_LENGTH] = {};
  const size_t written = espUsbHostVideoEncodeProbeControl(control, buffer, sizeof(buffer));
  checkEqual(written, ESP_USB_HOST_VIDEO_PROBE_LENGTH_11, "encoded UVC 1.1 length");

  EspUsbHostVideoProbeControl decoded;
  check(espUsbHostVideoDecodeProbeControl(buffer, written, decoded), "decode round trip");
  checkEqual(decoded.formatIndex, 1, "round trip bFormatIndex");
  checkEqual(decoded.frameIndex, 2, "round trip bFrameIndex");
  checkEqual(decoded.frameInterval, 666666, "round trip dwFrameInterval");
  checkEqual(decoded.maxVideoFrameSize, 320 * 240 * 2, "round trip dwMaxVideoFrameSize");
  checkEqual(decoded.maxPayloadTransferSize, 1024, "round trip dwMaxPayloadTransferSize");
}

void testProbeControlShortForm()
{
  // A UVC 1.0 camera returns 26 bytes. The fields past dwMaxPayloadTransferSize
  // are absent and must read back as zero rather than as whatever the transfer
  // buffer held.
  EspUsbHostVideoProbeControl control;
  control.formatIndex = 3;
  control.frameIndex = 1;
  control.frameInterval = 333333;
  control.maxVideoFrameSize = 12345;
  control.maxPayloadTransferSize = 3072;
  control.clockFrequency = 48000000;

  uint8_t buffer[ESP_USB_HOST_VIDEO_PROBE_MAX_LENGTH] = {};
  const size_t written =
      espUsbHostVideoEncodeProbeControl(control, buffer, ESP_USB_HOST_VIDEO_PROBE_LENGTH_10);
  checkEqual(written, ESP_USB_HOST_VIDEO_PROBE_LENGTH_10, "encoded UVC 1.0 length");

  EspUsbHostVideoProbeControl decoded;
  check(espUsbHostVideoDecodeProbeControl(buffer, written, decoded), "short form decodes");
  checkEqual(decoded.formatIndex, 3, "short form bFormatIndex");
  checkEqual(decoded.maxPayloadTransferSize, 3072, "short form dwMaxPayloadTransferSize");
  checkEqual(decoded.clockFrequency, 0, "short form has no dwClockFrequency");
}

void testProbeControlRejects()
{
  EspUsbHostVideoProbeControl control;
  uint8_t buffer[ESP_USB_HOST_VIDEO_PROBE_MAX_LENGTH] = {};
  checkEqual(espUsbHostVideoEncodeProbeControl(control, nullptr, sizeof(buffer)), 0, "null buffer");
  checkEqual(espUsbHostVideoEncodeProbeControl(control, buffer, 20), 0, "buffer below UVC 1.0 length");

  EspUsbHostVideoProbeControl decoded;
  check(!espUsbHostVideoDecodeProbeControl(nullptr, 26, decoded), "null decode");
  check(!espUsbHostVideoDecodeProbeControl(buffer, 20, decoded), "decode below UVC 1.0 length");
}

}  // namespace

int main()
{
  testFormatGuid();
  testFrameIntervalConversion();
  testIsocPayloadSize();
  testDecodeMjpegFormat();
  testDecodeUncompressedFormat();
  testDecodeFormatRejects();
  testDecodeDiscreteFrame();
  testDecodeContinuousFrame();
  testDecodeFrameClampsIntervalList();
  testDecodeFrameRejects();
  testSupportsFrameInterval();
  testNearestFrameInterval();
  testSelectExact();
  testSelectWildcards();
  testSelectRejects();
  testSelectSkipsUnstartable();
  testPayloadHeaderMinimal();
  testPayloadHeaderWithTimestamps();
  testPayloadHeaderError();
  testPayloadHeaderRejects();
  testProbeControlRoundTrip();
  testProbeControlShortForm();
  testProbeControlRejects();

  if (failures != 0)
  {
    printf("%d check(s) failed\n", failures);
    return 1;
  }
  printf("all USB Video decoding and selection checks passed\n");
  return 0;
}
