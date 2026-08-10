// RC-S300 device layer: everything that needs the USB host.
//
// Rcs300Protocol.hpp only formats bytes. This header connects it to EspUsbHost:
// it opens the CCID interface, sends each pseudo APDU as a PC_to_RDR_XfrBlock
// (`ccidTransfer()`), and runs a FeliCa Polling with a System Code of the
// caller's choosing.
//
// Nothing was added to the library for this. A transparent session is a
// reader-specific protocol, so it lives in the example, on top of the raw CCID
// message APIs the library already exposes -- the same split the library's CCID
// design set out.
//
// See README.md for the measured command set.

#pragma once

#include "EspUsbHost.h"
#include "FelicaProtocol.hpp"
#include "Rcs300Protocol.hpp"

namespace rcs300
{

// The reader answers a pseudo APDU in a handful of milliseconds; this is only the
// ceiling for a wedged reader.
static constexpr uint32_t COMMAND_TIMEOUT_MS = 3000;
// How long the reader waits for the target's answer to one frame. 100 ms is far
// more than a FeliCa Polling needs and still short enough to poll in a loop.
static constexpr uint32_t FRAME_TIMEOUT_US = 100000;
// A FeliCa target needs a moment to power up once the field comes on. Polling
// straight after RF on can miss a card that a retry then finds.
static constexpr uint32_t FIELD_SETTLE_MS = 20;

// Longest pseudo APDU and response in play: a transparent exchange is the header,
// the timeout object, the transmit object and the frame.
static constexpr size_t MESSAGE_BYTES = 128;

// One RC-S300, driven through a transparent session.
class Rcs300Reader
{
public:
  explicit Rcs300Reader(EspUsbHost &host) : host_(host) {}

  // Opens the reader's CCID interface. The VID/PID check is what makes this
  // example honest: the command set below is Sony's, so another CCID reader must
  // not be driven with it.
  bool open()
  {
    EspUsbHostCcidInterface info;
    if (!host_.ccidOpen() || !host_.ccidGetInterface(info))
    {
      return false;
    }

    EspUsbHostDeviceInfo device;
    if (!host_.getDevice(info.address, device) || device.vid != VID || device.pid != PID)
    {
      return false;
    }
    address_ = info.address;
    open_ = true;
    return true;
  }

  bool isOpen() const { return open_; }
  uint8_t address() const { return address_; }

  // Result and status word of the last pseudo APDU, for diagnosing a refusal.
  uint8_t lastResult() const { return lastResponse_.result; }
  uint16_t lastStatusWord() const { return lastResponse_.statusWord; }

  bool startSession() { return manage(TAG_START_SESSION); }
  bool endSession() { return manage(TAG_END_SESSION); }
  bool rfOn() { return manage(TAG_RF_ON); }
  bool rfOff() { return manage(TAG_RF_OFF); }

  // Switches the field to FeliCa. The reader accepts this without naming the
  // protocol it selected, so there is normally nothing to read back; the object is
  // still picked up when it is there.
  bool selectFelica()
  {
    uint8_t message[MESSAGE_BYTES] = {};
    const size_t length = switchToFelica(message, sizeof(message));
    if (length == 0 || !exchange(message, length))
    {
      return false;
    }
    if (lastResponse_.protocol && lastResponse_.protocolLength >= 1)
    {
      selectedProtocol_ = lastResponse_.protocol[0];
    }
    return true;
  }

  // The protocol byte the reader reported for the last selectFelica(), 0 when it
  // reported none -- which is the normal case.
  uint8_t selectedProtocol() const { return selectedProtocol_; }

  // Sends one Polling and parses the answer. False when the reader refused the
  // command or nothing in the field answered; noAnswer() separates the two.
  bool poll(uint16_t systemCode,
            felica::Target &target,
            uint8_t requestCode = felica::REQUEST_SYSTEM_CODE)
  {
    noAnswer_ = false;

    uint8_t frame[felica::POLLING_FRAME_BYTES] = {};
    const size_t frameLength = felica::pollingFrame(frame, sizeof(frame), systemCode, requestCode);
    uint8_t message[MESSAGE_BYTES] = {};
    const size_t length =
        transparentExchange(message, sizeof(message), frame, frameLength, FRAME_TIMEOUT_US);
    if (length == 0 || !exchange(message, length))
    {
      return false;
    }
    if (lastResponse_.result == RESULT_NO_ANSWER || lastResponse_.statusWord == SW_NO_ANSWER)
    {
      noAnswer_ = true;
      return false;
    }
    if (!lastResponse_.accepted)
    {
      return false;
    }
    if (!lastResponse_.received)
    {
      // The exchange was accepted but the answer is not in the object this
      // example expects it in. The raw response is the only way to find out which
      // object it is, so it is printed rather than swallowed.
      Serial.print("no receive object in the response, raw: ");
      printHex(responseBuffer_, responseLength_);
      return false;
    }
    return felica::parsePollingResponse(lastResponse_.received, lastResponse_.receivedLength, target);
  }

  // True when the last poll() failed because the field was empty, as opposed to
  // the reader refusing the command.
  bool noAnswer() const { return noAnswer_; }

  // The whole sequence for one identification: take the field over, poll, and
  // hand it back. Retries because a target that has just entered the field can
  // miss the first Polling.
  bool readTarget(uint16_t systemCode, felica::Target &target, uint8_t attempts = 3)
  {
    if (!startSession())
    {
      return false;
    }
    bool ok = false;
    // The field is cycled rather than just switched on. The reader has already
    // been polling on its own, so the target is sitting in whatever state that
    // left it in, and a Polling sent into that field goes unanswered -- measured
    // with a Suica the reader's own path was reading fine.
    if (selectFelica() && rfOff() && rfOn())
    {
      delay(FIELD_SETTLE_MS);
      for (uint8_t attempt = 0; attempt < attempts && !ok; attempt++)
      {
        ok = poll(systemCode, target);
      }
    }
    // The field and the session are given back even when the poll failed, so the
    // reader is left able to poll on its own again.
    rfOff();
    endSession();
    return ok;
  }

  static void printHex(const uint8_t *data, size_t length)
  {
    for (size_t i = 0; i < length; i++)
    {
      Serial.printf("%02x", data[i]);
    }
    Serial.println();
  }

private:
  bool manage(uint8_t tag)
  {
    uint8_t message[MESSAGE_BYTES] = {};
    const size_t length = manageSession(message, sizeof(message), tag);
    return length != 0 && exchange(message, length) && lastResponse_.accepted;
  }

  // One pseudo APDU: XfrBlock out, response parsed. The response points into
  // responseBuffer_, so it stays valid until the next exchange.
  bool exchange(const uint8_t *message, size_t length)
  {
    lastResponse_ = Response();
    responseLength_ = 0;
    if (!open_)
    {
      return false;
    }
    if (!host_.ccidTransfer(message,
                            length,
                            responseBuffer_,
                            sizeof(responseBuffer_),
                            &responseLength_,
                            0,
                            address_,
                            COMMAND_TIMEOUT_MS))
    {
      return false;
    }
    return parseResponse(responseBuffer_, responseLength_, lastResponse_);
  }

  EspUsbHost &host_;
  bool open_ = false;
  uint8_t address_ = ESP_USB_HOST_ANY_ADDRESS;
  uint8_t responseBuffer_[MESSAGE_BYTES] = {};
  size_t responseLength_ = 0;
  Response lastResponse_;
  uint8_t selectedProtocol_ = 0;
  bool noAnswer_ = false;
};

} // namespace rcs300
