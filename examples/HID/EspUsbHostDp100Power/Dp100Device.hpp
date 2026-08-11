// Device layer for the ALIENTEK DP100: everything that needs the USB host.
//
// Dp100Protocol.hpp only formats bytes. This header connects it to EspUsbHost:
// it finds the HID interface, sends request reports on the interrupt OUT endpoint
// and pairs them with the reports that come back on the interrupt IN endpoint.
//
// Two properties of the library's HID API shape the design:
//
//   - onHIDInput() delivers every HID IN report, and it runs on the USB task. It
//     is the callback to use here: the report-ID dispatch behind it would never
//     match a frame whose first byte is a protocol direction marker, so
//     onHIDVendorInput() never fires for this device. The callback only latches;
//     everything else happens from the caller's task.
//   - sendHIDVendorOutput() writes raw bytes to the HID interrupt OUT endpoint and
//     does not wait for completion. Pairing a request with its answer is therefore
//     this layer's job: it clears the latch, sends, then waits for a report whose
//     opcode matches.

#pragma once

#include "EspUsbHost.h"
#include "Dp100Protocol.hpp"

#include <string.h>

namespace dp100
{

static constexpr uint16_t VID = 0x2e3c;
static constexpr uint16_t PID = 0xaf01;

// The device answers within a couple of poll intervals (the endpoints are 1 ms),
// so this is generous. It bounds the wait after a frame the device drops.
static constexpr uint32_t RESPONSE_TIMEOUT_MS = 500;
// A refused frame gets a one-byte answer rather than silence, so a retry is worth
// one attempt before the caller sees a failure.
static constexpr uint8_t REQUEST_ATTEMPTS = 2;

// One DP100. Single instance per sketch: the latch is a member, but the callback
// that fills it is registered against this object.
class Dp100Device
{
public:
  explicit Dp100Device(EspUsbHost &host) : host_(host) {}

  // Finds the DP100's HID interface. vid/pid of 0 match anything, so a caller can
  // select either by identity or by taking the only HID device present.
  bool find(uint8_t &address, uint8_t &interfaceNumber, uint16_t vid = VID, uint16_t pid = PID) const
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
        if (interfaces[n].interfaceClass != 0x03 || !interfaces[n].claimed)
        {
          continue;
        }
        address = devices[i].address;
        interfaceNumber = interfaces[n].number;
        return true;
      }
    }
    return false;
  }

  // Registers the input callback and records which device to talk to. The HID
  // interface is claimed by the library during enumeration, so there is nothing to
  // open here.
  bool begin(uint16_t vid = VID, uint16_t pid = PID)
  {
    uint8_t address = 0;
    uint8_t interfaceNumber = 0;
    if (!find(address, interfaceNumber, vid, pid))
    {
      return false;
    }
    address_ = address;
    interfaceNumber_ = interfaceNumber;

    // onHIDInput() holds a single callback rather than a listener list, so this
    // takes it over for the whole sketch. A sketch that also wants raw HID
    // reports for something else should call onHIDInput() itself and forward to
    // acceptReport().
    host_.onHIDInput([this](const EspUsbHostHIDInput &input)
                     { acceptReport(input); });
    return true;
  }

  void end()
  {
    address_ = 0;
    interfaceNumber_ = 0;
    latched_ = false;
  }

  // Feeds one HID report in, for a sketch that owns onHIDInput() itself. Runs on
  // whatever task delivered the report.
  void acceptReport(const EspUsbHostHIDInput &input) { latch(input); }

  bool ready() const { return address_ != 0; }
  uint8_t address() const { return address_; }
  uint8_t interfaceNumber() const { return interfaceNumber_; }
  // Reports received since begin(), including any this layer did not wait for.
  uint32_t receivedCount() const { return receivedCount_; }
  // Raw bytes of the last report, for logging a frame this layer could not use.
  const uint8_t *lastReport() const { return report_; }
  size_t lastReportLength() const { return reportLength_; }

  // Sends one request and waits for the answer to the same opcode. `response`
  // points into a buffer owned by this object and stays valid until the next
  // request. A refusal (the device's one-byte 0x00 answer) fails here rather than
  // being handed up as a payload.
  bool request(uint8_t opcode, Response &response, const uint8_t *data = nullptr, size_t dataLength = 0)
  {
    if (!ready())
    {
      return false;
    }
    uint8_t frame[REPORT_SIZE];
    if (encodeRequest(frame, sizeof(frame), opcode, data, dataLength) == 0)
    {
      return false;
    }

    for (uint8_t attempt = 0; attempt < REQUEST_ATTEMPTS; attempt++)
    {
      latched_ = false;
      if (!host_.sendHIDVendorOutput(frame, REPORT_SIZE, address_))
      {
        return false;
      }
      if (!waitForOpcode(opcode, response))
      {
        continue;
      }
      if (isRefusal(response))
      {
        refusals_++;
        continue;
      }
      return true;
    }
    return false;
  }

  bool readDeviceInfo(DeviceInfo &info)
  {
    Response response;
    return request(OP_DEVICE_INFO, response) && decodeDeviceInfo(response, info);
  }

  bool readBasicInfo(BasicInfo &info)
  {
    Response response;
    return request(OP_BASIC_INFO, response) && decodeBasicInfo(response, info);
  }

  // Raw SYSTEM_INFO bytes. The field meanings are not established, so they are
  // handed over as they arrived rather than guessed at.
  bool readSystemInfo(uint8_t *out, size_t capacity, size_t *length)
  {
    Response response;
    if (!request(OP_SYSTEM_INFO, response))
    {
      return false;
    }
    const size_t copied = response.length < capacity ? response.length : capacity;
    if (out && copied > 0)
    {
      memcpy(out, response.data, copied);
    }
    if (length)
    {
      *length = copied;
    }
    return true;
  }

  // The setpoint frame.
  //
  // UNVERIFIED against hardware, unlike everything above: this opcode carries the
  // output enable, so tests/probe/dp100 deliberately does not send it. Treat a
  // failure here as "the request form is not confirmed" rather than "the device
  // is broken", and never call it with a load connected until it has been proven
  // on your own bench.
  bool writeBasicSet(const BasicSet &set, Response &response)
  {
    if (!ready())
    {
      return false;
    }
    uint8_t frame[REPORT_SIZE];
    if (encodeBasicSet(frame, sizeof(frame), set) == 0)
    {
      return false;
    }
    latched_ = false;
    if (!host_.sendHIDVendorOutput(frame, REPORT_SIZE, address_))
    {
      return false;
    }
    return waitForOpcode(OP_BASIC_SET, response);
  }

  // How many answers were the device's one-byte refusal. A non-zero count with
  // working requests means frames are being built wrong somewhere.
  uint32_t refusalCount() const { return refusals_; }

private:
  // Runs on the USB task: copy and flag, nothing more.
  void latch(const EspUsbHostHIDInput &input)
  {
    if (address_ != 0 && input.address != address_)
    {
      return;
    }
    size_t length = input.length;
    if (length > sizeof(report_))
    {
      length = sizeof(report_);
    }
    memcpy(report_, input.data, length);
    reportLength_ = length;
    receivedCount_++;
    latched_ = true;
  }

  // Waits for a report that decodes and carries the opcode asked for. Frames for
  // another opcode are dropped: they are leftovers from a request that timed out,
  // and taking them would keep the pairing one answer behind forever.
  bool waitForOpcode(uint8_t opcode, Response &response)
  {
    const uint32_t startedAt = millis();
    while (millis() - startedAt < RESPONSE_TIMEOUT_MS)
    {
      if (!latched_)
      {
        delay(1);
        continue;
      }
      latched_ = false;
      if (decodeResponse(report_, reportLength_, response) && response.opcode == opcode)
      {
        return true;
      }
    }
    return false;
  }

  EspUsbHost &host_;
  uint8_t address_ = 0;
  uint8_t interfaceNumber_ = 0;
  volatile bool latched_ = false;
  uint8_t report_[REPORT_SIZE] = {};
  volatile size_t reportLength_ = 0;
  volatile uint32_t receivedCount_ = 0;
  volatile uint32_t refusals_ = 0;
};

} // namespace dp100
