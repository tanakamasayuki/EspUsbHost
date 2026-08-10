// Wire format of the Sony RC-S300's transparent session.
//
// Pure byte formatting: no USB, no Arduino, so it can be unit tested on a host
// compiler (tests/unit/felica_idm).
//
// CCID carries APDUs to a slot and nothing else, so a reader that polls the field
// on its own gives a host no way to say what to poll for. The way in is a
// transparent session: the host takes the RF field over and exchanges raw frames
// with the target. The data objects are the ones PC/SC part 3 defines, carried by
// Sony's pseudo APDU FF 50 00 <P2>.
//
// Everything below was measured against an RC-S300 (`FeliCa Port/PaSoRi 4.0`,
// VID 0x054c PID 0x0dc8) by tests/probe/rcs300_felica:
//
//   - PC_to_RDR_Escape is not supported at all; the pseudo APDUs go out over
//     PC_to_RDR_XfrBlock, i.e. ccidTransfer().
//   - Answers are a status object C0 03 <result> <SW1 SW2>, then any response
//     objects, then the pseudo APDU's own SW. result 00 / 9000 = accepted,
//     01 / 6301 = refused, 01 / 6401 = malformed, 01 / 6700 = wrong object
//     length, 01 / 6A81 = unsupported value, 02 / 6401 = sent, nothing answered.
//   - The timeout object 5F 46 is mandatory in a transparent exchange.
//   - Switch protocol takes two value bytes; 8F 01 <p> is refused with 6700.
//     8F 02 03 00 is the value a FeliCa Polling is answered under. 8F 02 00 03 is
//     also accepted -- and once answered with a response object 8F 01 08 -- but a
//     Polling sent after it is never answered, so it selects something else.
//   - The field has to be cycled off and on before the exchange. A Polling sent
//     after RF on alone went unanswered with a Suica on the reader that the
//     reader's own path was reading fine.
//   - A frame in the transmit object carries its own FeliCa length byte. Without
//     it the target does not answer.
//   - A successful exchange answers with the status object, then 92 01 00 and
//     96 02 00 00, then the target's frame in a 97 object.

#pragma once

#include <stddef.h>
#include <stdint.h>

namespace rcs300
{

static constexpr uint16_t VID = 0x054c;
static constexpr uint16_t PID = 0x0dc8;

// Pseudo APDU header: FF 50 00 <P2> <Lc> <data objects> <Le>.
static constexpr uint8_t CLA = 0xff;
static constexpr uint8_t INS = 0x50;
static constexpr size_t HEADER_BYTES = 5;
// P2 selects the object group.
static constexpr uint8_t P2_MANAGE_SESSION = 0x00;
static constexpr uint8_t P2_TRANSPARENT_EXCHANGE = 0x01;
static constexpr uint8_t P2_SWITCH_PROTOCOL = 0x02;

// Data object tags. 5F46 is the only two byte tag in use here.
static constexpr uint8_t TAG_START_SESSION = 0x81;
static constexpr uint8_t TAG_END_SESSION = 0x82;
static constexpr uint8_t TAG_RF_OFF = 0x83;
static constexpr uint8_t TAG_RF_ON = 0x84;
static constexpr uint8_t TAG_SWITCH_PROTOCOL = 0x8f;
static constexpr uint8_t TAG_TIMEOUT_FIRST = 0x5f;
static constexpr uint8_t TAG_TIMEOUT_SECOND = 0x46;
static constexpr uint8_t TAG_TRANSMIT = 0x95;
static constexpr uint8_t TAG_STATUS = 0xc0;
// The object a transparent exchange returns the target's answer in.
static constexpr uint8_t TAG_RECEIVE = 0x97;
// Two more objects ride along on a successful exchange, always as 92 01 00 and
// 96 02 00 00. Nothing here needs them, and parseResponse() skips any object it
// does not know, so they are named only so a reader of a raw log knows them.
static constexpr uint8_t TAG_EXCHANGE_STATUS = 0x92;
static constexpr uint8_t TAG_EXCHANGE_INFO = 0x96;

// Value of the switch protocol object a FeliCa Polling is answered under. The
// reader accepts the reversed 00 03 as well and answers it with 8F 01 08, but no
// Polling sent afterwards is ever answered.
static constexpr uint8_t PROTOCOL_FELICA[2] = {0x03, 0x00};

// Status object results and status words, as measured.
static constexpr uint8_t RESULT_OK = 0x00;
static constexpr uint8_t RESULT_ERROR = 0x01;
static constexpr uint8_t RESULT_NO_ANSWER = 0x02;
static constexpr uint16_t SW_OK = 0x9000;
static constexpr uint16_t SW_NO_ANSWER = 0x6401;

// What a transparent exchange holds besides the frame: the timeout object, the
// transmit object's tag and length byte.
static constexpr size_t TIMEOUT_OBJECT_BYTES = 7;
static constexpr size_t TRANSMIT_OBJECT_OVERHEAD = 2;

// Wraps data objects in the pseudo APDU. Returns the bytes written, or 0 when the
// buffer is too small or the objects do not fit in a short Lc.
inline size_t command(uint8_t *out,
                      size_t capacity,
                      uint8_t p2,
                      const uint8_t *objects,
                      size_t objectsLength)
{
  if (!out || !objects || objectsLength == 0 || objectsLength > 0xff ||
      capacity < HEADER_BYTES + objectsLength + 1)
  {
    return 0;
  }
  out[0] = CLA;
  out[1] = INS;
  out[2] = 0x00;
  out[3] = p2;
  out[4] = static_cast<uint8_t>(objectsLength);
  for (size_t i = 0; i < objectsLength; i++)
  {
    out[HEADER_BYTES + i] = objects[i];
  }
  out[HEADER_BYTES + objectsLength] = 0x00;
  return HEADER_BYTES + objectsLength + 1;
}

// Start session, end session, RF on and RF off are all a single empty object in
// the manage session group.
inline size_t manageSession(uint8_t *out, size_t capacity, uint8_t tag)
{
  const uint8_t object[] = {tag, 0x00};
  return command(out, capacity, P2_MANAGE_SESSION, object, sizeof(object));
}

// Switches the field to FeliCa. The reader accepts this with no response object,
// so there is nothing to read back.
inline size_t switchToFelica(uint8_t *out, size_t capacity)
{
  const uint8_t object[] = {TAG_SWITCH_PROTOCOL, 0x02, PROTOCOL_FELICA[0], PROTOCOL_FELICA[1]};
  return command(out, capacity, P2_SWITCH_PROTOCOL, object, sizeof(object));
}

// Sends one raw frame to the target and asks for its answer. The timeout is how
// long the reader waits for that answer, in microseconds, little endian.
inline size_t transparentExchange(uint8_t *out,
                                  size_t capacity,
                                  const uint8_t *frame,
                                  size_t frameLength,
                                  uint32_t timeoutMicroseconds)
{
  if (!frame || frameLength == 0 || frameLength > 0xff)
  {
    return 0;
  }
  uint8_t objects[TIMEOUT_OBJECT_BYTES + TRANSMIT_OBJECT_OVERHEAD + 0xff] = {};
  size_t length = 0;
  objects[length++] = TAG_TIMEOUT_FIRST;
  objects[length++] = TAG_TIMEOUT_SECOND;
  objects[length++] = 0x04;
  objects[length++] = static_cast<uint8_t>(timeoutMicroseconds & 0xff);
  objects[length++] = static_cast<uint8_t>((timeoutMicroseconds >> 8) & 0xff);
  objects[length++] = static_cast<uint8_t>((timeoutMicroseconds >> 16) & 0xff);
  objects[length++] = static_cast<uint8_t>((timeoutMicroseconds >> 24) & 0xff);
  objects[length++] = TAG_TRANSMIT;
  objects[length++] = static_cast<uint8_t>(frameLength);
  for (size_t i = 0; i < frameLength; i++)
  {
    objects[length++] = frame[i];
  }
  return command(out, capacity, P2_TRANSPARENT_EXCHANGE, objects, length);
}

struct Response
{
  // From the C0 status object.
  uint8_t result = 0xff;
  uint16_t statusWord = 0;
  // Value of the response object named by TAG_RECEIVE: the target's raw answer.
  // Null when the response carries none, which is every failure.
  const uint8_t *received = nullptr;
  size_t receivedLength = 0;
  // Value of the switch protocol response object, when present.
  const uint8_t *protocol = nullptr;
  size_t protocolLength = 0;
  bool accepted = false;
};

// Walks the response: a run of data objects followed by the pseudo APDU's own
// status word. Returns false for anything that is not that shape, so a truncated
// or unexpected answer is a failure rather than a partly filled Response.
inline bool parseResponse(const uint8_t *data, size_t length, Response &response)
{
  response = Response();
  if (!data || length < 2)
  {
    return false;
  }

  // The trailing two bytes are the pseudo APDU's SW1SW2; the objects sit before.
  const size_t objectsLength = length - 2;
  bool haveStatus = false;
  size_t offset = 0;
  while (offset < objectsLength)
  {
    uint8_t tag = data[offset++];
    // 5F46 is the only two byte tag; nothing else in this dialect uses one.
    if (tag == TAG_TIMEOUT_FIRST)
    {
      if (offset >= objectsLength)
      {
        return false;
      }
      offset++;
    }
    if (offset >= objectsLength)
    {
      return false;
    }
    const size_t objectLength = data[offset++];
    if (objectLength > objectsLength - offset)
    {
      return false;
    }
    const uint8_t *value = data + offset;
    offset += objectLength;

    switch (tag)
    {
    case TAG_STATUS:
      // <result> <SW1> <SW2>, and the reader has always sent exactly that.
      if (objectLength < 3)
      {
        return false;
      }
      response.result = value[0];
      response.statusWord = static_cast<uint16_t>((value[1] << 8) | value[2]);
      haveStatus = true;
      break;
    case TAG_RECEIVE:
      response.received = value;
      response.receivedLength = objectLength;
      break;
    case TAG_SWITCH_PROTOCOL:
      response.protocol = value;
      response.protocolLength = objectLength;
      break;
    default:
      break;
    }
  }

  if (!haveStatus)
  {
    return false;
  }
  response.accepted = response.result == RESULT_OK && response.statusWord == SW_OK;
  return true;
}

} // namespace rcs300
