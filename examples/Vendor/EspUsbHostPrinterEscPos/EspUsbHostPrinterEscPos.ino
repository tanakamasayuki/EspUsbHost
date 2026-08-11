// Print on a USB receipt printer: an ESC/POS thermal printer with a Japanese font
// and an auto cutter (developed against an Xprinter XP-C58K, 0483:070b).
//
// The interface class is 0x07 (Printer), not the vendor-specific 0xff. The library
// has no printer support of its own; this example builds the class requests and the
// print data stream on the vendor bulk/control API, which is why it lives under
// examples/Vendor/. See README.md.
//
// PAPER: printing consumes it. Both actions below are off by default, so the
// sketch as shipped only reads the device ID and the paper status - nothing moves.
// Turn PRINT_SAMPLE on to print, and CUT_PAPER on once you have confirmed your
// printer has a cutter.

#include "EspUsbHost.h"
#include "PrinterDevice.hpp"
#include "ReceiptJa.hpp"

EspUsbHost usb;
printer::PrinterDevice slip(usb);

// Set to true to actually print. Leave both false to exercise the class requests
// and the status path without using paper.
static constexpr bool PRINT_SAMPLE = false;
// Only meaningful with PRINT_SAMPLE. A printer without a cutter ignores GS V, but
// a printer with a *tear bar* and no cutter can jam on it, so it is separate.
static constexpr bool CUT_PAPER = false;
// The Japanese receipt needs a printer with a two-byte font ROM. On a printer
// without one, set this to false to print the ASCII-only slip instead.
static constexpr bool PRINT_JAPANESE = true;

static constexpr uint32_t STATUS_INTERVAL_MS = 5000;

static bool connected = false;
static bool printed = false;
static uint8_t printBuffer[printer::PRINT_BUFFER_SIZE];

static void printDeviceId()
{
  Serial.printf("device id \"%s\"\n", slip.deviceId());
  static const char *const keys[] = {"MFG", "MANUFACTURER", "MDL", "MODEL", "CMD", "COMMAND SET"};
  for (size_t i = 0; i < sizeof(keys) / sizeof(keys[0]); i++)
  {
    char value[64] = {};
    if (slip.deviceIdField(keys[i], value, sizeof(value)) > 0)
    {
      Serial.printf("  %s = %s\n", keys[i], value);
    }
  }
}

static void printStatus()
{
  printer::PortStatus port;
  if (slip.readPortStatus(port))
  {
    Serial.printf("port status 0x%02x paper_empty=%u selected=%u error=%u\n",
                  port.raw,
                  port.paperEmpty ? 1 : 0,
                  port.selected ? 1 : 0,
                  port.error ? 1 : 0);
  }
  else
  {
    Serial.println("GET_PORT_STATUS failed");
  }

  uint8_t value = 0;
  if (slip.realtimeStatus(escpos::STATUS_PRINTER, value))
  {
    Serial.printf("DLE EOT 1 printer 0x%02x offline=%u\n", value, (value & 0x08) ? 1 : 0);
  }
  if (slip.realtimeStatus(escpos::STATUS_PAPER_ROLL, value))
  {
    const escpos::PaperStatus paper = escpos::decodePaperStatus(value);
    Serial.printf("DLE EOT 4 paper 0x%02x near_end=%u out=%u\n",
                  paper.raw,
                  paper.nearEnd ? 1 : 0,
                  paper.out ? 1 : 0);
  }
}

// Prints one slip, refusing when the printer says it cannot. Checking first is not
// pedantry: data sent to a printer that is out of paper is buffered, and comes out
// mixed into whatever is printed after the roll is replaced.
static bool printSample()
{
  bool paperOut = false;
  bool nearEnd = false;
  bool error = false;
  if (!slip.checkPaper(paperOut, nearEnd, error))
  {
    Serial.println("cannot read the paper status; refusing to print");
    return false;
  }
  if (paperOut)
  {
    Serial.println("out of paper; refusing to print");
    return false;
  }
  if (error)
  {
    Serial.println("printer reports an error (cover open? cutter jam?); refusing to print");
    return false;
  }
  if (nearEnd)
  {
    Serial.println("paper near end");
  }

  escpos::Builder out(printBuffer, sizeof(printBuffer));
  if (PRINT_JAPANESE)
  {
    receipt::buildReceipt(out, CUT_PAPER, 12345678);
  }
  else
  {
    receipt::buildAsciiTest(out, CUT_PAPER);
  }
  if (!out.ok())
  {
    Serial.println("receipt does not fit in the print buffer");
    return false;
  }

  Serial.printf("printing %u bytes\n", static_cast<unsigned>(out.length()));
  if (!slip.write(out))
  {
    Serial.println("bulk write failed");
    return false;
  }
  return true;
}

static bool connect()
{
  if (!slip.begin())
  {
    return false;
  }

  Serial.printf("printer ready address=%u interface=%u protocol=0x%02x bidirectional=%u bulk_out=0x%02x bulk_in=0x%02x mps=%u\n",
                slip.address(),
                slip.interfaceNumber(),
                slip.protocol(),
                slip.isBidirectional() ? 1 : 0,
                usb.vendorOutEndpoint(slip.address()),
                usb.vendorInEndpoint(slip.address()),
                usb.vendorOutPacketSize(slip.address()));
  printDeviceId();
  printStatus();

  if (PRINT_SAMPLE && !printed)
  {
    printed = printSample();
  }
  else if (!PRINT_SAMPLE)
  {
    Serial.println("PRINT_SAMPLE is false; nothing was printed");
  }
  return true;
}

void setup()
{
  Serial.begin(115200);
  delay(3000);

  usb.onDeviceConnected([](const EspUsbHostDeviceInfo &device)
                        { Serial.printf("connected address=%u vid=%04x pid=%04x product=\"%s\"\n",
                                        device.address,
                                        device.vid,
                                        device.pid,
                                        device.product); });

  usb.onDeviceDisconnected([](const EspUsbHostDeviceInfo &device)
                           {
        Serial.printf("disconnected address=%u\n", device.address);
        if (connected && device.address == slip.address())
        {
          slip.end();
          connected = false;
        } });

  usb.begin();
  Serial.println("EspUsbHostPrinterEscPos start");
  Serial.println("Connect an ESC/POS USB receipt printer with paper loaded.");
}

void loop()
{
  static uint32_t lastAttemptMs = 0;
  static uint32_t lastStatusMs = 0;

  if (!connected)
  {
    if (millis() - lastAttemptMs >= 1000)
    {
      lastAttemptMs = millis();
      connected = connect();
    }
    return;
  }

  if (millis() - lastStatusMs >= STATUS_INTERVAL_MS)
  {
    lastStatusMs = millis();
    printStatus();
  }
}
