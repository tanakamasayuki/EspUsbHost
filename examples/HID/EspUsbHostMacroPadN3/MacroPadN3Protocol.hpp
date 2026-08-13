// Byte formatting for the "CRT" protocol spoken by the Mirabox N3 / Ajazz AKP03
// family of LCD macro pads. No USB here: MacroPadN3Device.hpp puts these bytes on
// the wire.
//
// Every host->device packet is one interrupt OUT transfer of PACKET_SIZE bytes:
//
//   "CRT" 0x00 0x00 | ASCII command | command arguments | zero padding
//
// The command is ASCII of no fixed length - most are three letters, but "CONNECT" is
// seven and "QUCMD" five - and sizes in the arguments are big-endian. Every command
// and argument below was read out of a USB capture of the vendor application talking
// to a STREONOR S6; see README.md.

#pragma once

#include <stdint.h>
#include <string.h>

namespace n3
{

// Protocol version 3 devices use 1024-byte packets, which is also the max packet
// size of their interrupt OUT endpoint, so one packet is one USB transaction.
// Version 1 devices (Mirabox 293 and relatives) use 512 instead.
static constexpr size_t PACKET_SIZE = 1024;
static constexpr size_t PREFIX_LENGTH = 5;

// Six LCD keys. The three scene keys and the three encoders report through the input
// path but have no display of their own.
static constexpr uint8_t KEY_COUNT = 6;
static constexpr uint8_t SCENE_KEY_COUNT = 3;
static constexpr uint8_t ENCODER_COUNT = 3;
static constexpr uint8_t KEY_ALL = 0xff;

// Pixel size of one key image. Protocol version 3 devices are documented as
// 64x64; the encoder in KeyImage.hpp produces exactly this. The device is told
// the JPEG's byte size and nothing about its dimensions, so a wrong value here
// shows up as a scaled or offset picture rather than as an error.
static constexpr uint16_t KEY_IMAGE_WIDTH = 64;
static constexpr uint16_t KEY_IMAGE_HEIGHT = 64;

// Class request the firmware version is read with: HID Get_Report(Input, id 0) on
// the vendor HID interface. The answer is a NUL-terminated ASCII string whose
// first field is the protocol version, e.g. "V3.S6.02.011".
static constexpr uint8_t GET_REPORT_REQUEST_TYPE = 0xa1;
static constexpr uint8_t GET_REPORT_REQUEST = 0x01;
static constexpr uint16_t GET_REPORT_VALUE = 0x0100;

// One input event. `code` is the raw byte the device reported, and exactly one of the
// three identifying fields is set:
//
//   - LCD key: `keyIndex` is 1..KEY_COUNT, `pressed` says press or release.
//   - Scene key: `sceneKey` is 1..SCENE_KEY_COUNT, `pressed` applies.
//   - Encoder turned: `encoder` is 1..ENCODER_COUNT and `delta` is the direction.
//   - Encoder pushed: `encoder` is set, `delta` is zero and `pressed` applies.
//
// A code that is none of those leaves all of them at 0 and is still handed over, so an
// unrecognised control is visible rather than dropped.
struct InputEvent
{
  uint8_t code = 0;
  uint8_t state = 0;
  uint8_t keyIndex = 0;
  uint8_t sceneKey = 0;
  uint8_t encoder = 0;
  int8_t delta = 0;
  bool pressed = false;
};

// Writes "CRT" + letters + args into a PACKET_SIZE buffer. Returns false when the
// buffer is too small; the padding is written here so callers cannot forget it -
// the device reads a whole packet and a short one is ignored.
inline bool encodeCommand(uint8_t *packet,
                          size_t capacity,
                          const char *command,
                          const uint8_t *args = nullptr,
                          size_t argsLength = 0)
{
  if (!packet || !command || capacity < PACKET_SIZE)
  {
    return false;
  }
  const size_t commandLength = strlen(command);
  if (PREFIX_LENGTH + commandLength + argsLength > PACKET_SIZE)
  {
    return false;
  }
  memset(packet, 0, PACKET_SIZE);
  packet[0] = 'C';
  packet[1] = 'R';
  packet[2] = 'T';
  packet[3] = 0x00;
  packet[4] = 0x00;
  memcpy(packet + PREFIX_LENGTH, command, commandLength);
  if (args && argsLength > 0)
  {
    memcpy(packet + PREFIX_LENGTH + commandLength, args, argsLength);
  }
  return true;
}

// Opens the session. Until this arrives the pad acts on its keys itself and reports
// nothing to the host; afterwards it reports input and leaves its screens to the
// host. The vendor application sends it first, before anything else.
//
// It also starts a timer: with no further packets the pad drops off the bus and
// re-enumerates half a minute later, so a session has to be kept alive.
inline bool encodeSessionStart(uint8_t *packet, size_t capacity)
{
  return encodeCommand(packet, capacity, "DIS");
}

// Keeps the session open. The vendor application sends this about every ten seconds
// and nothing else in between when it has no screen updates to make.
inline bool encodeKeepalive(uint8_t *packet, size_t capacity)
{
  return encodeCommand(packet, capacity, "CONNECT");
}

// Sent once by the vendor application, right after the brightness and before the
// first image. Its purpose is not established and the pad works without it; the
// bytes are reproduced as captured in case a device needs it.
inline bool encodeStartupQuery(uint8_t *packet, size_t capacity)
{
  const uint8_t args[] = {0x1f, 0x11, 0x00, 0x11, 0x00, 0x11};
  return encodeCommand(packet, capacity, "QUCMD", args, sizeof(args));
}

// Backlight brightness, 0..100.
inline bool encodeBrightness(uint8_t *packet, size_t capacity, uint8_t percent)
{
  if (percent > 100)
  {
    percent = 100;
  }
  const uint8_t args[] = {0x00, 0x00, percent};
  return encodeCommand(packet, capacity, "LIG", args, sizeof(args));
}

// Clears one key's image, or every key with KEY_ALL.
inline bool encodeClear(uint8_t *packet, size_t capacity, uint8_t keyIndex)
{
  const uint8_t args[] = {0x00, 0x00, 0x00, keyIndex};
  return encodeCommand(packet, capacity, "CLE", args, sizeof(args));
}

// Makes the pending key images visible. Sent after an upload; the device does not
// redraw on its own.
inline bool encodeRefresh(uint8_t *packet, size_t capacity)
{
  const uint8_t args[] = {0x00, 0x00};
  return encodeCommand(packet, capacity, "STP", args, sizeof(args));
}

// Announces `length` bytes of JPEG for one key, which follow as raw packets.
inline bool encodeKeyImageHeader(uint8_t *packet, size_t capacity, uint8_t keyIndex, uint32_t length)
{
  const uint8_t args[] = {
      static_cast<uint8_t>((length >> 24) & 0xff),
      static_cast<uint8_t>((length >> 16) & 0xff),
      static_cast<uint8_t>((length >> 8) & 0xff),
      static_cast<uint8_t>(length & 0xff),
      keyIndex,
  };
  return encodeCommand(packet, capacity, "BAT", args, sizeof(args));
}

// Announces the boot logo, which follows the same way. The image is raw BGR888 of
// the panel's full size, not JPEG, and the size is part of the command.
inline bool encodeBootImageHeader(uint8_t *packet, size_t capacity, uint32_t length)
{
  const uint8_t args[] = {
      static_cast<uint8_t>((length >> 24) & 0xff),
      static_cast<uint8_t>((length >> 16) & 0xff),
      static_cast<uint8_t>((length >> 8) & 0xff),
      static_cast<uint8_t>(length & 0xff),
      0x01,
  };
  return encodeCommand(packet, capacity, "LOG", args, sizeof(args));
}

// An input report starts with this, then the code and its state.
//
//   "ACK" 00 00 "OK" 00 00 | code | state | zero padding
static constexpr size_t INPUT_CODE_OFFSET = 9;
static constexpr size_t INPUT_STATE_OFFSET = 10;
static constexpr uint8_t INPUT_HEADER[] = {'A', 'C', 'K', 0x00, 0x00, 'O', 'K', 0x00, 0x00};

// Rotating an encoder reports a code per direction with the state left at zero; the
// lower code of a pair is counter-clockwise and the higher one clockwise. Pushing it
// reports its own code with a state of 1 for the press and 0 for the release, the same
// way the keys do. The codes are not in an arithmetic relation to each other, so they
// are a table rather than a formula.
//
// The order here is the pad's own numbering, confirmed on a STREONOR S6: encoder 1 is
// the bottom-left knob, 2 the bottom-right, 3 the top one. Another brand's unit may
// place the same codes differently, so a sketch that cares about position should check
// its own.
struct EncoderCodes
{
  uint8_t counterClockwise;
  uint8_t clockwise;
  uint8_t push;
};
static constexpr EncoderCodes ENCODER_CODES[ENCODER_COUNT] = {
    {0x90, 0x91, 0x33},
    {0x60, 0x61, 0x34},
    {0x50, 0x51, 0x35},
};

// The three keys without a screen, in the order they sit after the LCD keys.
static constexpr uint8_t SCENE_KEY_CODES[SCENE_KEY_COUNT] = {0x25, 0x30, 0x31};

// Decodes one interrupt IN report. Returns false for a report that is too short or
// does not carry the header, which is what separates an event from any other traffic
// the endpoint may deliver.
inline bool decodeInput(const uint8_t *data, size_t length, InputEvent &event)
{
  if (!data || length <= INPUT_STATE_OFFSET || memcmp(data, INPUT_HEADER, sizeof(INPUT_HEADER)) != 0)
  {
    return false;
  }
  event = InputEvent();
  event.code = data[INPUT_CODE_OFFSET];
  event.state = data[INPUT_STATE_OFFSET];
  if (event.code >= 1 && event.code <= KEY_COUNT)
  {
    event.keyIndex = event.code;
    event.pressed = event.state != 0;
    return true;
  }
  for (uint8_t i = 0; i < ENCODER_COUNT; i++)
  {
    const EncoderCodes &codes = ENCODER_CODES[i];
    if (event.code == codes.counterClockwise || event.code == codes.clockwise)
    {
      event.encoder = static_cast<uint8_t>(i + 1);
      event.delta = (event.code == codes.counterClockwise) ? -1 : 1;
      return true;
    }
    if (event.code == codes.push)
    {
      event.encoder = static_cast<uint8_t>(i + 1);
      event.pressed = event.state != 0;
      return true;
    }
  }
  for (uint8_t i = 0; i < SCENE_KEY_COUNT; i++)
  {
    if (event.code == SCENE_KEY_CODES[i])
    {
      event.sceneKey = static_cast<uint8_t>(i + 1);
      event.pressed = event.state != 0;
      return true;
    }
  }
  return true;
}

} // namespace n3
