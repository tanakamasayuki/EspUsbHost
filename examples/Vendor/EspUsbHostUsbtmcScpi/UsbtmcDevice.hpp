// Device layer for a USBTMC instrument: everything that needs the USB host.
//
// UsbtmcProtocol.hpp only formats bytes. This header connects it to EspUsbHost:
// it finds the USBTMC interface among the enumerated devices, claims it with the
// vendor bulk API, runs the class requests on EP0 and turns a command string
// into a request/response pair on the bulk endpoints.
//
// Nothing here is instrument-specific - no SCPI, no VID/PID. ScpiPmx.hpp adds
// that layer on top, and swapping it is what retargets the example at another
// instrument.
//
// The USBTMC interface class is 0xfe (Application Specific), not the
// vendor-specific 0xff. It lives under examples/Vendor/ because it is built on
// the library's vendor bulk/control API, which is how that directory is
// organised; see README.md.

#pragma once

#include "EspUsbHost.h"
#include "UsbtmcProtocol.hpp"

#include <stdio.h>
#include <string.h>

namespace usbtmc
{

static constexpr uint8_t INTERFACE_CLASS = 0xfe;
static constexpr uint8_t INTERFACE_SUBCLASS = 0x03;
static constexpr uint8_t INTERFACE_PROTOCOL_USB488 = 0x01;

// Bulk scratch size. Full-speed USBTMC endpoints are 64 bytes, so this holds
// eight packets: enough for a *IDN? string or a measurement in one transfer,
// while a longer response is fetched in further rounds.
static constexpr size_t SCRATCH_SIZE = 512;
// Largest payload asked for per REQUEST_DEV_DEP_MSG_IN. Leaves room for the
// header plus 4-byte padding inside SCRATCH_SIZE.
static constexpr uint32_t RESPONSE_CHUNK = 496;

static constexpr uint32_t READ_TIMEOUT_MS = 2000;
static constexpr uint32_t CONTROL_TIMEOUT_MS = 1000;
// How long a CHECK_CLEAR_STATUS poll keeps returning PENDING before giving up.
static constexpr uint32_t CLEAR_TIMEOUT_MS = 2000;

// One USBTMC instrument.
class UsbtmcDevice
{
public:
  explicit UsbtmcDevice(EspUsbHost &host) : host_(host) {}

  // Finds a USBTMC interface: class 0xfe / subclass 0x03 on any enumerated
  // device. vid/pid of 0 match anything, so an instrument can be selected either
  // by identity or by being the only USBTMC device on the bus.
  bool find(uint8_t &address,
            uint8_t &interfaceNumber,
            uint8_t &protocol,
            uint16_t vid = 0,
            uint16_t pid = 0) const
  {
    EspUsbHostDeviceInfo devices[ESP_USB_HOST_MAX_DEVICES];
    const size_t count = host_.getDevices(devices, ESP_USB_HOST_MAX_DEVICES);
    for (size_t i = 0; i < count; i++)
    {
      if ((vid != 0 && devices[i].vid != vid) || (pid != 0 && devices[i].pid != pid))
      {
        continue;
      }
      EspUsbHostInterfaceInfo interfaces[ESP_USB_HOST_MAX_INTERFACES];
      const size_t interfaceCount =
          host_.getInterfaces(devices[i].address, interfaces, ESP_USB_HOST_MAX_INTERFACES);
      for (size_t n = 0; n < interfaceCount; n++)
      {
        if (interfaces[n].interfaceClass != INTERFACE_CLASS ||
            interfaces[n].interfaceSubClass != INTERFACE_SUBCLASS)
        {
          continue;
        }
        address = devices[i].address;
        interfaceNumber = interfaces[n].number;
        protocol = interfaces[n].interfaceProtocol;
        return true;
      }
    }
    return false;
  }

  // Claims the interface and brings the message layer to a known state:
  // GET_CAPABILITIES, then CLEAR so that a half-finished transfer left by a
  // previous run (or by whatever host had the instrument before) is discarded.
  //
  // READ_ON_DEMAND is the right read mode here: USBTMC is strictly
  // request/response, so a continuous IN transfer would sit there NAKing.
  bool begin(uint16_t vid = 0, uint16_t pid = 0)
  {
    uint8_t address = 0;
    uint8_t interfaceNumber = 0;
    uint8_t protocol = 0;
    if (!find(address, interfaceNumber, protocol, vid, pid))
    {
      return false;
    }
    if (!host_.vendorOpen(address, interfaceNumber, ESP_USB_HOST_VENDOR_READ_ON_DEMAND))
    {
      return false;
    }

    address_ = address;
    interfaceNumber_ = interfaceNumber;
    protocol_ = protocol;
    tag_ = 0;

    // The host has to terminate every bulk OUT transfer with a short packet. A
    // command padded to a multiple of the 64-byte endpoint would not do that by
    // itself, so let the library append the zero-length packet.
    host_.vendorSetAutoZlp(true, address_);

    if (!readCapabilities())
    {
      end();
      return false;
    }
    if (!clear())
    {
      end();
      return false;
    }
    return true;
  }

  void end()
  {
    address_ = 0;
    interfaceNumber_ = 0;
    protocol_ = 0;
  }

  bool ready() const { return address_ != 0; }
  uint8_t address() const { return address_; }
  uint8_t interfaceNumber() const { return interfaceNumber_; }
  bool isUsb488() const { return protocol_ == INTERFACE_PROTOCOL_USB488; }
  const Capabilities &capabilities() const { return capabilities_; }

  // Sends one command with EOM set. Text only: USBTMC carries the string as-is,
  // and SCPI needs no trailing newline over USB (the message boundary is the
  // terminator), though instruments accept one.
  bool write(const char *command)
  {
    if (!ready() || !command)
    {
      return false;
    }
    const size_t length = strlen(command);
    const size_t total = paddedLength(HEADER_SIZE + length);
    if (total > SCRATCH_SIZE)
    {
      return false;
    }
    const uint8_t tag = takeTag();
    if (encodeDevDepMsgOut(scratch_, SCRATCH_SIZE, tag, reinterpret_cast<const uint8_t *>(command), length) == 0)
    {
      return false;
    }
    if (!host_.vendorWrite(scratch_, total, address_))
    {
      // The OUT transfer failed part way, so the device may still be expecting
      // the rest of the message. Tell it to drop what it has.
      abortBulkOut(tag);
      return false;
    }
    return true;
  }

  // Sends a query and copies the response into response, NUL-terminated.
  // Trailing CR/LF from the instrument is stripped.
  bool query(const char *command, char *response, size_t responseSize, size_t *responseLength = nullptr)
  {
    if (!response || responseSize == 0)
    {
      return false;
    }
    response[0] = '\0';
    if (!write(command))
    {
      return false;
    }
    size_t length = 0;
    if (!read(reinterpret_cast<uint8_t *>(response), responseSize - 1, &length))
    {
      return false;
    }
    while (length > 0 && (response[length - 1] == '\n' || response[length - 1] == '\r'))
    {
      length--;
    }
    response[length] = '\0';
    if (responseLength)
    {
      *responseLength = length;
    }
    return true;
  }

  // Reads one complete response message. Each round is a REQUEST_DEV_DEP_MSG_IN
  // followed by the DEV_DEP_MSG_IN it produces; a response longer than the
  // requested chunk clears EOM and is continued by the next round.
  bool read(uint8_t *buffer, size_t bufferSize, size_t *actualLength)
  {
    if (!ready() || !buffer)
    {
      return false;
    }
    size_t total = 0;
    for (;;)
    {
      const uint32_t want = static_cast<uint32_t>(bufferSize - total < RESPONSE_CHUNK ? bufferSize - total : RESPONSE_CHUNK);
      if (want == 0)
      {
        break; // caller's buffer is full; the rest of the message is dropped
      }
      const uint8_t tag = takeTag();
      if (encodeRequestDevDepMsgIn(scratch_, SCRATCH_SIZE, tag, want) == 0)
      {
        return false;
      }
      if (!host_.vendorWrite(scratch_, HEADER_SIZE, address_))
      {
        return false;
      }

      size_t received = 0;
      MsgInHeader header;
      for (;;)
      {
        size_t chunk = 0;
        if (!host_.vendorReadSync(scratch_ + received, SCRATCH_SIZE - received, &chunk, READ_TIMEOUT_MS, address_))
        {
          abortBulkIn(tag);
          return false;
        }
        received += chunk;
        if (received < HEADER_SIZE)
        {
          if (chunk == 0)
          {
            // The device terminated the transfer without a full header.
            abortBulkIn(tag);
            return false;
          }
          continue; // header split across packets
        }
        if (!decodeMsgInHeader(scratch_, received, header) || header.bTag != tag)
        {
          // Out of sync: the payload cannot be trusted, so abort the IN
          // transfer and let the caller retry from a clean state.
          abortBulkIn(tag);
          return false;
        }
        if (received >= HEADER_SIZE + header.transferSize || chunk == 0)
        {
          break;
        }
      }
      if (received < HEADER_SIZE + header.transferSize)
      {
        abortBulkIn(tag);
        return false;
      }

      memcpy(buffer + total, scratch_ + HEADER_SIZE, header.transferSize);
      total += header.transferSize;
      if (header.endOfMessage)
      {
        break;
      }
    }
    if (actualLength)
    {
      *actualLength = total;
    }
    return true;
  }

  // GET_CAPABILITIES. Also refreshes capabilities(), and hands back the raw 24
  // bytes when the caller wants to log them.
  bool readCapabilities(uint8_t *raw = nullptr, size_t rawSize = 0)
  {
    uint8_t data[CAPABILITIES_SIZE] = {};
    size_t actual = 0;
    if (!controlIn(REQ_GET_CAPABILITIES, 0, data, sizeof(data), &actual))
    {
      return false;
    }
    if (raw && rawSize > 0)
    {
      memcpy(raw, data, rawSize < sizeof(data) ? rawSize : sizeof(data));
    }
    return decodeCapabilities(data, actual, capabilities_);
  }

  // The CLEAR sequence from USBTMC 4.2.1.6: INITIATE_CLEAR, then poll
  // CHECK_CLEAR_STATUS until the device is done, draining bulk IN while it says
  // it still has data queued.
  //
  // The spec ends the sequence with CLEAR_FEATURE(ENDPOINT_HALT) on bulk OUT.
  // That is deliberately left out here, see clearOutHalt(): sending it resets the
  // device's data toggle, and this host cannot resynchronise its own unless the
  // pipe is actually halted, which desynchronises the endpoint instead of
  // recovering it. Measured: with the halt clear in place, the query after a
  // CLEAR times out.
  bool clear()
  {
    uint8_t status[2] = {};
    size_t actual = 0;
    if (!controlIn(REQ_INITIATE_CLEAR, 0, status, 1, &actual) || actual < 1 || status[0] != STATUS_SUCCESS)
    {
      return false;
    }

    const uint32_t startedAt = millis();
    for (;;)
    {
      if (!controlIn(REQ_CHECK_CLEAR_STATUS, 0, status, 2, &actual) || actual < 2)
      {
        return false;
      }
      if (status[0] != STATUS_PENDING)
      {
        if (status[0] != STATUS_SUCCESS)
        {
          return false;
        }
        break;
      }
      if (status[1] & 0x01)
      {
        // bmClear bit 0: the device still has data queued on bulk IN. Read it
        // away; a failing read here just means there was nothing left.
        size_t chunk = 0;
        host_.vendorReadSync(scratch_, SCRATCH_SIZE, &chunk, READ_TIMEOUT_MS, address_);
      }
      if (millis() - startedAt > CLEAR_TIMEOUT_MS)
      {
        return false;
      }
      delay(10);
    }

    // The next message starts a fresh tag sequence, which is what the device
    // expects after a CLEAR.
    tag_ = 0;
    return true;
  }

  // INITIATE_ABORT_BULK_IN plus its status poll. Called when a response header
  // does not match what was requested, which leaves the device mid-transfer.
  bool abortBulkIn(uint8_t tag)
  {
    uint8_t data[8] = {};
    size_t actual = 0;
    if (!controlIn(REQ_INITIATE_ABORT_BULK_IN, tag, data, 2, &actual) || actual < 1)
    {
      return false;
    }
    if (data[0] != STATUS_SUCCESS)
    {
      // Nothing was in progress after all, so there is nothing to unwind.
      return data[0] == STATUS_TRANSFER_NOT_IN_PROGRESS;
    }
    for (;;)
    {
      // Keep draining: the device finishes the aborted transfer before it
      // reports the abort complete.
      size_t chunk = 0;
      host_.vendorReadSync(scratch_, SCRATCH_SIZE, &chunk, READ_TIMEOUT_MS, address_);
      if (!controlIn(REQ_CHECK_ABORT_BULK_IN_STATUS, 0, data, 8, &actual) || actual < 8)
      {
        return false;
      }
      if (data[0] != STATUS_PENDING)
      {
        return data[0] == STATUS_SUCCESS;
      }
      delay(10);
    }
  }

  bool abortBulkOut(uint8_t tag)
  {
    uint8_t data[8] = {};
    size_t actual = 0;
    if (!controlIn(REQ_INITIATE_ABORT_BULK_OUT, tag, data, 2, &actual) || actual < 1)
    {
      return false;
    }
    if (data[0] != STATUS_SUCCESS)
    {
      return data[0] == STATUS_TRANSFER_NOT_IN_PROGRESS;
    }
    for (;;)
    {
      if (!controlIn(REQ_CHECK_ABORT_BULK_OUT_STATUS, 0, data, 8, &actual) || actual < 8)
      {
        return false;
      }
      if (data[0] != STATUS_PENDING)
      {
        return data[0] == STATUS_SUCCESS;
      }
      delay(10);
    }
  }

  // Optional USBTMC request: blinks the instrument's front-panel indicator.
  // Useful to confirm you are talking to the box in front of you.
  bool indicatorPulse()
  {
    if (!capabilities_.indicatorPulse)
    {
      return false;
    }
    uint8_t status = 0;
    size_t actual = 0;
    return controlIn(REQ_INDICATOR_PULSE, 0, &status, 1, &actual) && actual == 1 &&
           status == STATUS_SUCCESS;
  }

  // Standard CLEAR_FEATURE(ENDPOINT_HALT) on the bulk OUT endpoint. USBTMC ends
  // both the CLEAR and the ABORT_BULK_OUT sequence with it, but neither calls it
  // here, and it is exposed rather than used so the reason is in one place:
  //
  // Clearing the halt resets the data toggle at the device end. A host driver
  // resets its own to match (Linux does this inside usb_clear_halt()), but the
  // ESP-IDF host stack only resyncs a pipe that is actually halted, so on a
  // healthy endpoint this call desynchronises the toggle and every following OUT
  // is ignored by the device. Measured on a PMX18-5A: with this in the CLEAR
  // sequence, the query after a CLEAR times out; without it, CLEAR works.
  //
  // A genuinely stalled endpoint is recovered by the library itself, which
  // flushes and clears the pipe when a transfer fails. Call this only if you have
  // a device that needs the request and have confirmed the toggle survives it.
  //
  // It is also the reason vendorControlTransfer() takes a bmRequestType: this is
  // a standard request to an endpoint (0x02), while the class requests above are
  // 0xa1 to an interface, and neither fits the typed vendorControlIn/Out().
  bool clearOutHalt()
  {
    const uint8_t endpoint = host_.vendorOutEndpoint(address_);
    if (endpoint == 0)
    {
      return false;
    }
    return host_.vendorControlTransfer(STANDARD_ENDPOINT_OUT,
                                       STANDARD_CLEAR_FEATURE,
                                       FEATURE_ENDPOINT_HALT,
                                       endpoint,
                                       nullptr,
                                       0,
                                       nullptr,
                                       address_,
                                       CONTROL_TIMEOUT_MS);
  }

private:
  uint8_t takeTag()
  {
    tag_ = nextTag(tag_);
    return tag_;
  }

  // Every USBTMC class request is an IN transfer to the interface, which is what
  // vendorControlTransfer() exists for: vendorControlIn() would send 0xc0
  // (vendor type, device recipient) instead of 0xa1.
  bool controlIn(uint8_t request, uint16_t value, uint8_t *data, size_t length, size_t *actualLength)
  {
    return host_.vendorControlTransfer(CLASS_REQUEST_IN,
                                       request,
                                       value,
                                       interfaceNumber_,
                                       data,
                                       length,
                                       actualLength,
                                       address_,
                                       CONTROL_TIMEOUT_MS);
  }

  EspUsbHost &host_;
  uint8_t address_ = 0;
  uint8_t interfaceNumber_ = 0;
  uint8_t protocol_ = 0;
  uint8_t tag_ = 0;
  Capabilities capabilities_;
  uint8_t scratch_[SCRATCH_SIZE] = {};
};

} // namespace usbtmc
