// Device layer for a Mirabox N3 / Ajazz AKP03 family LCD macro pad: everything
// that needs the USB host.
//
// The pad exposes two HID interfaces. Interface 0 is vendor-defined and carries
// this protocol on a 512-byte interrupt IN and a 1024-byte interrupt OUT endpoint;
// interface 1 is a boot keyboard the library claims and reports as a keyboard on
// its own. This class only drives interface 0:
//
//   - sendHIDVendorOutput() writes raw bytes to the vendor HID interrupt OUT
//     endpoint, which is exactly one protocol packet. It does not wait for
//     completion, so an image upload queues its packets back to back; the endpoint
//     drains one per millisecond and PACKET_GAP_MS keeps the in-flight set small.
//   - onHIDInput() delivers the reports coming back. It is the callback to use here
//     rather than onHIDVendorInput(): the vendor path only fires for a report whose
//     first byte is the library's vendor report ID, and these reports start with an
//     "ACK" marker instead, so it would never fire. onHIDInput() sees every HID
//     report, so this class filters by device address and interface number.
//   - The firmware version is a control transfer, not a packet, so it works before
//     anything else has been sent and is the cheapest way to tell a device that
//     speaks this protocol from one that merely looks like it.
//
// A 1024-byte interrupt OUT endpoint is larger than the host driver's default FIFO
// split allows, so the sketch has to raise EspUsbHostConfig::fifo before begin() or
// interface 0 is never claimed. See README.md.

#pragma once

#include "EspUsbHost.h"
#include "MacroPadN3Protocol.hpp"

#include <functional>
#include <string.h>

namespace n3
{

// The white-label firmware in these pads is shared across brands, so identity is a
// poor filter: the STREONOR S6 reports 1500:3006 while Mirabox N3 units report
// 6602/6603:100x and Ajazz AKP03 units 0300:300x, all with the same descriptors and
// the same product string. begin() therefore matches on the interface shape by
// default and takes vid/pid only when a caller asks for a specific unit.
static constexpr uint16_t ANY_VID = 0;
static constexpr uint16_t ANY_PID = 0;

// Pause between queued OUT packets during an upload. The endpoint is polled every
// millisecond, so this keeps at most a couple of transfers outstanding instead of
// handing the whole image to the driver at once.
static constexpr uint32_t PACKET_GAP_MS = 2;

// Longest firmware version string the device is expected to answer with, plus
// room. The observed answer is 13 bytes including its NUL.
static constexpr size_t FIRMWARE_VERSION_MAX = 32;

class MacroPadN3Device
{
public:
  using InputCallback = std::function<void(const InputEvent &, const uint8_t *raw, size_t rawLength)>;

  explicit MacroPadN3Device(EspUsbHost &host) : host_(host) {}

  // Finds a pad: a claimed HID interface that has both a large interrupt OUT
  // endpoint and an interrupt IN endpoint. The keyboard interface fails the test
  // because it has no OUT endpoint, so this picks interface 0 without hardcoding
  // its number.
  bool find(uint8_t &address, uint8_t &interfaceNumber, uint16_t vid = ANY_VID, uint16_t pid = ANY_PID) const
  {
    EspUsbHostDeviceInfo devices[ESP_USB_HOST_MAX_DEVICES];
    const size_t deviceCount = host_.getDevices(devices, ESP_USB_HOST_MAX_DEVICES);
    for (size_t i = 0; i < deviceCount; i++)
    {
      if ((vid != ANY_VID && devices[i].vid != vid) || (pid != ANY_PID && devices[i].pid != pid))
      {
        continue;
      }

      EspUsbHostInterfaceInfo interfaces[ESP_USB_HOST_MAX_INTERFACES];
      const size_t interfaceCount =
          host_.getInterfaces(devices[i].address, interfaces, ESP_USB_HOST_MAX_INTERFACES);
      EspUsbHostEndpointInfo endpoints[ESP_USB_HOST_MAX_ENDPOINTS];
      const size_t endpointCount =
          host_.getEndpoints(devices[i].address, endpoints, ESP_USB_HOST_MAX_ENDPOINTS);

      for (size_t n = 0; n < interfaceCount; n++)
      {
        if (interfaces[n].interfaceClass != 0x03 || !interfaces[n].claimed)
        {
          continue;
        }
        bool hasIn = false;
        bool hasOut = false;
        for (size_t e = 0; e < endpointCount; e++)
        {
          if (endpoints[e].interfaceNumber != interfaces[n].number)
          {
            continue;
          }
          if ((endpoints[e].attributes & USB_BM_ATTRIBUTES_XFERTYPE_MASK) != USB_BM_ATTRIBUTES_XFER_INT)
          {
            continue;
          }
          if (endpoints[e].address & USB_B_ENDPOINT_ADDRESS_EP_DIR_MASK)
          {
            hasIn = true;
          }
          else if (endpoints[e].maxPacketSize >= PACKET_SIZE)
          {
            hasOut = true;
          }
        }
        if (hasIn && hasOut)
        {
          address = devices[i].address;
          interfaceNumber = interfaces[n].number;
          return true;
        }
      }
    }
    return false;
  }

  // Registers the input callback and records which device to talk to. The library
  // claimed the interface during enumeration, so nothing is opened here.
  bool begin(uint16_t vid = ANY_VID, uint16_t pid = ANY_PID)
  {
    uint8_t address = 0;
    uint8_t interfaceNumber = 0;
    if (!find(address, interfaceNumber, vid, pid))
    {
      return false;
    }
    address_ = address;
    interfaceNumber_ = interfaceNumber;

    // onHIDInput() holds one callback for the whole sketch. A sketch that needs raw
    // HID reports elsewhere too should register its own and forward to
    // acceptReport().
    host_.onHIDInput([this](const EspUsbHostHIDInput &input)
                     { acceptReport(input); });
    return true;
  }

  void end()
  {
    address_ = 0;
    interfaceNumber_ = 0;
  }

  bool ready() const { return address_ != 0; }
  uint8_t address() const { return address_; }
  uint8_t interfaceNumber() const { return interfaceNumber_; }
  uint32_t receivedCount() const { return receivedCount_; }

  // Runs on the USB task, so the callback it invokes must not block. Reports from the
  // pad's keyboard interface arrive here as well and are dropped by the interface
  // check; what is left is either an event or something this protocol does not
  // describe, and decodeInput() separates those on the header.
  void acceptReport(const EspUsbHostHIDInput &input)
  {
    if (input.address != address_ || input.interfaceNumber != interfaceNumber_)
    {
      return;
    }
    receivedCount_++;
    if (!inputCallback_)
    {
      return;
    }
    InputEvent event;
    if (!decodeInput(input.data, input.length, event))
    {
      return;
    }
    inputCallback_(event, input.data, input.length);
  }

  void onInput(InputCallback callback) { inputCallback_ = callback; }

  // Reads the firmware version string, e.g. "V3.S6.02.011" - the leading field is
  // the protocol version, which is what decides packet size and whether the device
  // reports key releases as well as presses. Returns false when the control
  // transfer fails, which is the sign of a device that does not speak this
  // protocol at all.
  bool readFirmwareVersion(char *out, size_t capacity)
  {
    if (!ready() || !out || capacity == 0)
    {
      return false;
    }
    uint8_t buffer[FIRMWARE_VERSION_MAX] = {};
    size_t actual = 0;
    if (!host_.vendorControlTransfer(GET_REPORT_REQUEST_TYPE,
                                     GET_REPORT_REQUEST,
                                     GET_REPORT_VALUE,
                                     interfaceNumber_,
                                     buffer,
                                     sizeof(buffer),
                                     &actual,
                                     address_))
    {
      return false;
    }
    size_t length = actual < sizeof(buffer) ? actual : sizeof(buffer);
    // The answer is NUL-terminated; keep only the printable head of it so a
    // caller can log the string without sanitising it.
    size_t used = 0;
    while (used < length && used + 1 < capacity && buffer[used] >= 0x20 && buffer[used] < 0x7f)
    {
      out[used] = static_cast<char>(buffer[used]);
      used++;
    }
    out[used] = '\0';
    return used > 0;
  }

  bool setBrightness(uint8_t percent)
  {
    uint8_t packet[PACKET_SIZE];
    return encodeBrightness(packet, sizeof(packet), percent) && send(packet);
  }

  bool clearKey(uint8_t keyIndex)
  {
    uint8_t packet[PACKET_SIZE];
    return encodeClear(packet, sizeof(packet), keyIndex) && send(packet);
  }

  bool clearAll() { return clearKey(KEY_ALL); }

  bool refresh()
  {
    uint8_t packet[PACKET_SIZE];
    return encodeRefresh(packet, sizeof(packet)) && send(packet);
  }

  // Uploads one key's JPEG and makes it visible. The header carries the byte count
  // and the data follows as raw packets, the last one zero-padded, so the device
  // knows where the image ends without a terminator.
  bool setKeyImage(uint8_t keyIndex, const uint8_t *jpeg, size_t length)
  {
    if (!jpeg || length == 0)
    {
      return false;
    }
    uint8_t packet[PACKET_SIZE];
    if (!encodeKeyImageHeader(packet, sizeof(packet), keyIndex, static_cast<uint32_t>(length)) || !send(packet))
    {
      return false;
    }
    return sendPayload(jpeg, length) && refresh();
  }

  // Same for the boot logo, which is raw BGR888 at the panel's full size rather
  // than JPEG. Kept for completeness: the panel size is device-specific and this
  // example does not know it, so a caller has to supply a correctly sized buffer.
  bool setBootImage(const uint8_t *bgr888, size_t length)
  {
    if (!bgr888 || length == 0)
    {
      return false;
    }
    uint8_t packet[PACKET_SIZE];
    if (!encodeBootImageHeader(packet, sizeof(packet), static_cast<uint32_t>(length)) || !send(packet))
    {
      return false;
    }
    return sendPayload(bgr888, length) && refresh();
  }

  // Opens the session, which is what makes the pad hand its screens to the host and
  // start reporting input. Nothing before this produces input reports, and the
  // session has to be held open with keepalive() afterwards.
  //
  // The startup query is what the vendor application sends here as well; the pad
  // works without it, so a failure to send it is not treated as fatal.
  bool beginSession(uint8_t brightnessPercent)
  {
    uint8_t packet[PACKET_SIZE];
    if (!encodeSessionStart(packet, sizeof(packet)) || !send(packet))
    {
      return false;
    }
    if (!setBrightness(brightnessPercent))
    {
      return false;
    }
    if (encodeStartupQuery(packet, sizeof(packet)))
    {
      send(packet);
    }
    return true;
  }

  // Holds the session open. Send it more often than the pad's timeout: the vendor
  // application sends it about every ten seconds, and a session left completely
  // quiet ends with the pad leaving the bus after roughly half a minute.
  bool keepalive()
  {
    uint8_t packet[PACKET_SIZE];
    return encodeKeepalive(packet, sizeof(packet)) && send(packet);
  }

private:
  bool send(const uint8_t *packet)
  {
    if (!ready())
    {
      return false;
    }
    if (!host_.sendHIDVendorOutput(packet, PACKET_SIZE, address_))
    {
      return false;
    }
    delay(PACKET_GAP_MS);
    return true;
  }

  // Splits a payload into packets. A trailing partial packet is zero-padded rather
  // than sent short: the device counts bytes from the header, and the endpoint's
  // max packet size is the packet size, so a short transfer would end the stream
  // early on the device side.
  bool sendPayload(const uint8_t *data, size_t length)
  {
    uint8_t packet[PACKET_SIZE];
    size_t offset = 0;
    while (offset < length)
    {
      const size_t chunk = (length - offset < PACKET_SIZE) ? (length - offset) : PACKET_SIZE;
      memcpy(packet, data + offset, chunk);
      if (chunk < PACKET_SIZE)
      {
        memset(packet + chunk, 0, PACKET_SIZE - chunk);
      }
      if (!send(packet))
      {
        return false;
      }
      offset += chunk;
    }
    return true;
  }

  EspUsbHost &host_;
  uint8_t address_ = 0;
  uint8_t interfaceNumber_ = 0;
  volatile uint32_t receivedCount_ = 0;
  InputCallback inputCallback_;
};

} // namespace n3
