// FeliCa Polling framing (JIS X 6319-4).
//
// Pure byte formatting: no USB, no Arduino. Every function works on a
// caller-owned buffer, so it can be unit tested on a host compiler
// (tests/unit/felica_idm).
//
// Polling is the command that asks the targets in the field to identify
// themselves, and its System Code is the filter this example exists for. CCID
// itself has no notion of a polling target, so nothing in the library can select
// one -- the frame has to be built here and handed to the reader as a raw frame.
//
// See README.md for the references.

#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace felica
{

static constexpr size_t IDM_BYTES = 8;
static constexpr size_t PMM_BYTES = 8;

// The System Code selects which of the target's systems answers.
//
// 0xffff is the wildcard: every FeliCa target answers it with whichever system
// it puts first. That is what a reader polls with on its own, and on a phone it
// is the wallet's own card rather than the transit card -- the reason an iPhone
// reports an Apple Pay identifier instead of the Suica one.
//
// 0x0003 is the system the JR-compatible transit cards live in (Suica, PASMO and
// the rest), so it is the code that reaches a Suica whether it is a plain card or
// a phone.
static constexpr uint16_t SYSTEM_CODE_ANY = 0xffff;
static constexpr uint16_t SYSTEM_CODE_TRANSIT = 0x0003;

// Request Code. 0x00 asks for nothing beyond the IDm and PMm; 0x01 asks the
// target to also report the System Code it answered with, which is what makes a
// wildcard poll's answer self-describing.
static constexpr uint8_t REQUEST_NONE = 0x00;
static constexpr uint8_t REQUEST_SYSTEM_CODE = 0x01;

static constexpr uint8_t COMMAND_POLLING = 0x00;
static constexpr uint8_t RESPONSE_POLLING = 0x01;

// LEN, command, two System Code bytes, Request Code, Time Slot.
static constexpr size_t POLLING_FRAME_BYTES = 6;
// LEN, response code, IDm, PMm, and the two request data bytes when they were
// asked for.
static constexpr size_t POLLING_RESPONSE_MIN_BYTES = 2 + IDM_BYTES + PMM_BYTES;
static constexpr size_t POLLING_RESPONSE_MAX_BYTES = POLLING_RESPONSE_MIN_BYTES + 2;

struct Target
{
  uint8_t idm[IDM_BYTES] = {};
  uint8_t pmm[PMM_BYTES] = {};
  // True when the Polling asked for request data and the target supplied it.
  bool hasRequestData = false;
  // The System Code the target answered with, for REQUEST_SYSTEM_CODE. Worth
  // reading back after a wildcard poll: it says which system was reached.
  uint16_t requestData = 0;
};

// Builds "LEN 00 SC1 SC2 RC TSN". Every FeliCa frame starts with its own length,
// and that length counts the length byte itself. Returns the bytes written, or 0
// when the buffer is too small.
inline size_t pollingFrame(uint8_t *out,
                           size_t capacity,
                           uint16_t systemCode,
                           uint8_t requestCode = REQUEST_SYSTEM_CODE,
                           uint8_t timeSlot = 0x00)
{
  if (!out || capacity < POLLING_FRAME_BYTES)
  {
    return 0;
  }
  out[0] = static_cast<uint8_t>(POLLING_FRAME_BYTES);
  out[1] = COMMAND_POLLING;
  out[2] = static_cast<uint8_t>(systemCode >> 8);
  out[3] = static_cast<uint8_t>(systemCode & 0xff);
  out[4] = requestCode;
  out[5] = timeSlot;
  return POLLING_FRAME_BYTES;
}

// Parses "LEN 01 IDm[8] PMm[8] [RD[2]]". The declared length is what decides
// whether request data is present, so a frame that claims more or less than a
// Polling response can hold is rejected rather than read past.
inline bool parsePollingResponse(const uint8_t *data, size_t length, Target &target)
{
  if (!data || length < POLLING_RESPONSE_MIN_BYTES)
  {
    return false;
  }
  const size_t declared = data[0];
  if (declared < POLLING_RESPONSE_MIN_BYTES || declared > POLLING_RESPONSE_MAX_BYTES ||
      declared > length)
  {
    return false;
  }
  if (data[1] != RESPONSE_POLLING)
  {
    return false;
  }

  target = Target();
  memcpy(target.idm, data + 2, IDM_BYTES);
  memcpy(target.pmm, data + 2 + IDM_BYTES, PMM_BYTES);
  if (declared == POLLING_RESPONSE_MAX_BYTES)
  {
    target.hasRequestData = true;
    target.requestData = static_cast<uint16_t>((data[POLLING_RESPONSE_MIN_BYTES] << 8) |
                                              data[POLLING_RESPONSE_MIN_BYTES + 1]);
  }
  return true;
}

} // namespace felica
