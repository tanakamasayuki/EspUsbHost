// Device layer for a USB Printer Class device: everything that needs the USB host.
//
// PrinterProtocol.hpp formats the class requests and EscPos.hpp formats the print
// data; this header connects both to EspUsbHost. It finds the printer interface,
// claims it with the vendor bulk API, runs the class requests on EP0, and sends
// print data on bulk OUT while reading real-time status on bulk IN.
//
// Nothing here is model-specific - no VID/PID, and no ESC/POS beyond the
// real-time status request, which is the one command whose answer this layer has
// to interpret to know whether it is safe to keep printing.
//
// The printer interface class is 0x07, not the vendor-specific 0xff. This example
// lives under examples/Vendor/ because it is built on the library's vendor
// bulk/control API, which is how that directory is organised; see README.md.

#pragma once

#include "EscPos.hpp"
#include "EspUsbHost.h"
#include "PrinterProtocol.hpp"

#include <stdio.h>
#include <string.h>

namespace printer
{

static constexpr uint32_t CONTROL_TIMEOUT_MS = 1000;
// Real-time status is answered out of turn, so it comes back quickly or not at
// all. A short timeout keeps a unidirectional or non-Epson printer from stalling
// the sketch for seconds on every poll.
static constexpr uint32_t STATUS_TIMEOUT_MS = 300;

// One printer.
class PrinterDevice
{
public:
  explicit PrinterDevice(EspUsbHost &host) : host_(host) {}

  // Finds a printer interface: class 0x07 on any enumerated device. vid/pid of 0
  // match anything, so a printer can be selected either by identity or by being
  // the only one on the bus.
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
        if (interfaces[n].interfaceClass != INTERFACE_CLASS)
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

  // Claims the interface and reads the device ID. Print data is a one-way stream
  // and status is only read when asked for, so READ_ON_DEMAND is the right mode:
  // a continuous IN transfer would sit there NAKing until the next status poll.
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
    deviceId_[0] = '\0';

    // Print data is a byte stream of any length, so a receipt that happens to be
    // a multiple of the endpoint size still has to be terminated with a short
    // packet or the printer keeps waiting for more.
    host_.vendorSetAutoZlp(true, address_);

    // Best effort: a printer that does not implement GET_DEVICE_ID still prints.
    readDeviceId();
    return true;
  }

  void end()
  {
    address_ = 0;
    interfaceNumber_ = 0;
    protocol_ = 0;
    deviceId_[0] = '\0';
  }

  bool ready() const { return address_ != 0; }
  uint8_t address() const { return address_; }
  uint8_t interfaceNumber() const { return interfaceNumber_; }
  uint8_t protocol() const { return protocol_; }
  // Only a bidirectional interface has a bulk IN endpoint, and therefore only a
  // bidirectional interface can report paper-out over the data path.
  bool isBidirectional() const { return protocol_ != PROTOCOL_UNIDIRECTIONAL; }
  const char *deviceId() const { return deviceId_; }

  // GET_DEVICE_ID. Refreshes deviceId(), and hands back the raw response when the
  // caller wants to log it.
  bool readDeviceId(uint8_t *raw = nullptr, size_t rawSize = 0, size_t *rawLength = nullptr)
  {
    uint8_t data[128] = {};
    size_t actual = 0;
    if (!host_.vendorControlTransfer(CLASS_REQUEST_IN,
                                     REQ_GET_DEVICE_ID,
                                     0, // configuration index
                                     deviceIdIndex(interfaceNumber_),
                                     data,
                                     sizeof(data),
                                     &actual,
                                     address_,
                                     CONTROL_TIMEOUT_MS))
    {
      return false;
    }
    if (raw && rawSize > 0)
    {
      const size_t copy = actual < rawSize ? actual : rawSize;
      memcpy(raw, data, copy);
      if (rawLength)
      {
        *rawLength = copy;
      }
    }
    // True means the request was answered with a well-formed ID, which may be
    // empty: some printers implement the request and have nothing to say.
    // deviceId()[0] == '\0' is how a caller tells the two apart.
    return decodeDeviceId(data, actual, deviceId_, sizeof(deviceId_));
  }

  // Copies one IEEE 1284 field out of the cached device ID, e.g. "MDL" or "CMD".
  size_t deviceIdField(const char *key, char *value, size_t valueSize) const
  {
    return printer::deviceIdField(deviceId_, key, value, valueSize);
  }

  // GET_PORT_STATUS. This is the paper/error check that works on any printer-class
  // device, including a unidirectional one, because it runs on EP0.
  bool readPortStatus(PortStatus &status)
  {
    uint8_t raw = 0;
    size_t actual = 0;
    if (!host_.vendorControlTransfer(CLASS_REQUEST_IN,
                                     REQ_GET_PORT_STATUS,
                                     0,
                                     interfaceNumber_,
                                     &raw,
                                     1,
                                     &actual,
                                     address_,
                                     CONTROL_TIMEOUT_MS) ||
        actual != 1)
    {
      return false;
    }
    status = decodePortStatus(raw);
    return true;
  }

  // SOFT_RESET: flushes the printer's buffers and returns it to its power-on
  // state. Worth sending when a previous run left a half-finished command in the
  // printer - a truncated GS ( k, say - which would otherwise eat the start of the
  // next receipt as its missing arguments.
  bool softReset()
  {
    return host_.vendorControlTransfer(CLASS_REQUEST_OUT,
                                       REQ_SOFT_RESET,
                                       0,
                                       interfaceNumber_,
                                       nullptr,
                                       0,
                                       nullptr,
                                       address_,
                                       CONTROL_TIMEOUT_MS);
  }

  // Sends print data as it is.
  bool write(const uint8_t *data, size_t length)
  {
    if (!ready() || !data || length == 0)
    {
      return false;
    }
    return host_.vendorWrite(data, length, address_);
  }

  bool write(const escpos::Builder &builder)
  {
    // An overflowed builder holds a receipt that is missing its tail, which may
    // be a cut command or the arguments of the command before it. Never send it.
    if (!builder.ok())
    {
      return false;
    }
    return write(builder.data(), builder.length());
  }

  // DLE EOT n, sent on its own and answered on bulk IN. Returns false when the
  // printer is unidirectional, does not implement it, or does not answer in time,
  // which is not the same as a failure to print.
  bool realtimeStatus(uint8_t which, uint8_t &value)
  {
    if (!ready() || !isBidirectional())
    {
      return false;
    }
    uint8_t request[3] = {};
    escpos::Builder builder(request, sizeof(request));
    builder.realtimeStatus(which);
    if (!builder.ok() || !host_.vendorWrite(request, builder.length(), address_))
    {
      return false;
    }

    uint8_t reply[16] = {};
    size_t received = 0;
    if (!host_.vendorReadSync(reply, sizeof(reply), &received, STATUS_TIMEOUT_MS, address_) ||
        received == 0)
    {
      return false;
    }
    // Take the last byte: a printer that had an unread status byte queued from a
    // previous poll answers with both, oldest first.
    value = reply[received - 1];
    return true;
  }

  // The check a print loop wants before each receipt: paper present, no error.
  // Uses the data path when the printer is bidirectional and falls back to
  // GET_PORT_STATUS, so a caller gets an answer either way.
  //
  // paperOut and error are only meaningful when this returns true.
  bool checkPaper(bool &paperOut, bool &nearEnd, bool &error)
  {
    paperOut = false;
    nearEnd = false;
    error = false;

    uint8_t paper = 0;
    if (realtimeStatus(escpos::STATUS_PAPER_ROLL, paper))
    {
      const escpos::PaperStatus status = escpos::decodePaperStatus(paper);
      paperOut = status.out;
      nearEnd = status.nearEnd;
      uint8_t problems = 0;
      if (realtimeStatus(escpos::STATUS_ERROR, problems))
      {
        // DLE EOT 3 bit 3: an unrecoverable or auto-recoverable error, which
        // includes the cover being open and the cutter jamming.
        error = (problems & 0x08) != 0;
      }
      return true;
    }

    PortStatus port;
    if (!readPortStatus(port) || port.unknown)
    {
      // Nothing usable from either path. Saying so is the only honest answer: a
      // printer that implements neither cannot be asked whether it has paper, and
      // the caller has to decide whether to print blind.
      return false;
    }
    paperOut = port.paperEmpty;
    error = port.error;
    return true;
  }

private:
  EspUsbHost &host_;
  uint8_t address_ = 0;
  uint8_t interfaceNumber_ = 0;
  uint8_t protocol_ = 0;
  char deviceId_[128] = {};
};

} // namespace printer
