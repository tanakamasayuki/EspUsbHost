// Probe: which USB Printer Class requests an ESC/POS printer actually answers.
//
// The read-only manual test found two things on an Xprinter XP-C58K that need
// pinning down before they are written up as device behaviour rather than as a
// mistake in the host code:
//
//   GET_DEVICE_ID   failed outright. Either the printer does not implement it, or
//                   the request is addressed wrongly. wValue is the configuration
//                   index and wIndex packs the interface in the *high* byte, which
//                   is unlike every other request here, so both are swept - along
//                   with the device recipient a few printers are documented to
//                   want instead of the interface.
//   GET_PORT_STATUS answered with 0x00, which decodes as "deselected, error". The
//                   real-time status says the printer is fine, so either the byte
//                   means nothing on this model or it wakes up after some other
//                   exchange. It is read before and after everything else.
//
// Nothing is queued for printing: no paper moves. Nothing is asserted - the log is
// the output.

#include "../../../examples/Vendor/EspUsbHostPrinterEscPos/PrinterDevice.hpp"

static EspUsbHost usb;
static printer::PrinterDevice slip(usb);

struct Variant
{
  const char *what;
  uint8_t requestType;
  uint16_t value;
  uint16_t index;
};

static uint8_t interfaceNumber = 0;

static void tryDeviceId(const Variant &variant)
{
  uint8_t data[128] = {};
  size_t actual = 0;
  const bool ok = usb.vendorControlTransfer(variant.requestType,
                                            printer::REQ_GET_DEVICE_ID,
                                            variant.value,
                                            variant.index,
                                            data,
                                            sizeof(data),
                                            &actual,
                                            slip.address(),
                                            1000);
  Serial.printf("GET_DEVICE_ID %-28s type=0x%02x value=0x%04x index=0x%04x -> %s len=%u",
                variant.what,
                variant.requestType,
                variant.value,
                variant.index,
                ok ? "ok" : "FAILED",
                static_cast<unsigned>(actual));
  if (ok && actual > 0)
  {
    Serial.print(" raw:");
    for (size_t i = 0; i < actual && i < 32; i++)
    {
      Serial.printf(" %02x", data[i]);
    }
    char text[128] = {};
    if (printer::decodeDeviceId(data, actual, text, sizeof(text)) > 0)
    {
      Serial.printf(" text=\"%s\"", text);
    }
  }
  Serial.println();
  delay(100);
}

static void readPortStatus(const char *when, uint16_t index)
{
  uint8_t raw = 0xaa;
  size_t actual = 0;
  const bool ok = usb.vendorControlTransfer(printer::CLASS_REQUEST_IN,
                                            printer::REQ_GET_PORT_STATUS,
                                            0,
                                            index,
                                            &raw,
                                            1,
                                            &actual,
                                            slip.address(),
                                            1000);
  Serial.printf("GET_PORT_STATUS %-20s index=0x%04x -> %s len=%u raw=0x%02x\n",
                when,
                index,
                ok ? "ok" : "FAILED",
                static_cast<unsigned>(actual),
                ok ? raw : 0);
  delay(100);
}

static void probe()
{
  interfaceNumber = slip.interfaceNumber();
  Serial.printf("[probe] address=%u interface=%u protocol=0x%02x\n",
                slip.address(),
                interfaceNumber,
                slip.protocol());

  // Before anything else, in case some later exchange is what wakes it up.
  readPortStatus("first", interfaceNumber);
  readPortStatus("device recipient", 0);

  const Variant variants[] = {
      // The spec-correct form: class IN to the interface, interface in the high
      // byte of wIndex, configuration index 0.
      {"spec, config 0", printer::CLASS_REQUEST_IN, 0, printer::deviceIdIndex(interfaceNumber)},
      // Some printers count configurations from 1 rather than indexing from 0.
      {"config 1", printer::CLASS_REQUEST_IN, 1, printer::deviceIdIndex(interfaceNumber)},
      // The wIndex bytes the wrong way round, which is what a buggy firmware
      // written against the other class requests would expect.
      {"interface in the low byte", printer::CLASS_REQUEST_IN, 0, interfaceNumber},
      {"zero index", printer::CLASS_REQUEST_IN, 0, 0},
      // Class IN to the *device* rather than the interface.
      {"device recipient", 0xa0, 0, 0},
      {"device recipient, config 1", 0xa0, 1, 0},
      // A vendor-type request, in case the firmware reuses the code that way.
      {"vendor type", 0xc0, 0, interfaceNumber},
  };
  for (size_t i = 0; i < sizeof(variants) / sizeof(variants[0]); i++)
  {
    tryDeviceId(variants[i]);
  }

  // Real-time status for comparison: this is the path that works, and having it in
  // the same log makes clear the printer was alive and answering throughout.
  uint8_t value = 0;
  for (uint8_t which = 1; which <= 4; which++)
  {
    if (slip.realtimeStatus(which, value))
    {
      Serial.printf("DLE EOT %u -> 0x%02x\n", which, value);
    }
    else
    {
      Serial.printf("DLE EOT %u -> no answer\n", which);
    }
  }

  readPortStatus("after status", interfaceNumber);

  // SOFT_RESET, then the port status again: on some printers the byte only becomes
  // meaningful once the interface has been reset.
  Serial.printf("SOFT_RESET -> %s\n", slip.softReset() ? "ok" : "FAILED");
  delay(300);
  readPortStatus("after soft reset", interfaceNumber);
  tryDeviceId({"after soft reset", printer::CLASS_REQUEST_IN, 0,
               printer::deviceIdIndex(interfaceNumber)});

  Serial.println("PROBE_DONE");
}

void setup()
{
  Serial.begin(115200);
  delay(3000);
  usb.begin();
  Serial.println("printer_class probe start");
}

void loop()
{
  static uint32_t lastAttemptMs = 0;
  static bool done = false;

  if (!done && millis() - lastAttemptMs >= 1000)
  {
    lastAttemptMs = millis();
    if (slip.begin())
    {
      done = true;
      probe();
    }
  }
  delay(10);
}
