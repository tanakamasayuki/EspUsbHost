// Manual test: the USB Printer Class request layer, without using any paper.
//
// Everything here is EP0 class requests and DLE EOT real-time status, which a
// printer answers ahead of its print buffer. Nothing is queued for printing, so
// the paper never moves.
//
// Verified: the interface is found and claimed, GET_DEVICE_ID parses into IEEE
// 1284 fields, GET_PORT_STATUS reports paper and no error, the four real-time
// status bytes come back with their fixed bits correct, SOFT_RESET succeeds and
// the endpoints still work afterwards, and repeated polling stays in sync.

// The protocol, device, ESC/POS and receipt layers live with the example so there
// is one source of truth; tests/ is stripped from release archives while examples/
// is not, so the dependency only ever points this way.
#include "../../../examples/Vendor/EspUsbHostPrinterEscPos/PrinterDevice.hpp"

EspUsbHost usb;
printer::PrinterDevice slip(usb);

static constexpr int STATUS_ROUNDS = 20;

static bool failed = false;

static void fail(const char *what)
{
  Serial.printf("FAILURE: %s\n", what);
  failed = true;
}

// The two bits every DLE EOT reply has fixed: bit 0 clear, bit 1 set. They are how
// a status byte is told apart from print data a confused device echoed back, so
// checking them is checking that the byte read really is the status.
static bool statusByteShapeOk(uint8_t value)
{
  return (value & 0x01) == 0 && (value & 0x02) != 0;
}

static void testDeviceId()
{
  uint8_t raw[128] = {};
  size_t rawLength = 0;
  if (!slip.readDeviceId(raw, sizeof(raw), &rawLength))
  {
    fail("GET_DEVICE_ID");
    return;
  }
  Serial.printf("device id raw %u bytes:", static_cast<unsigned>(rawLength));
  for (size_t i = 0; i < rawLength; i++)
  {
    Serial.printf(" %02x", raw[i]);
  }
  Serial.println();
  Serial.printf("device id \"%s\"\n", slip.deviceId());

  // An empty ID is a legal answer, and is what the XP-C58K this was developed
  // against gives: `00 02`, a length field and nothing else. The request working is
  // what is checked here; having something to say is the printer's business.
  if (slip.deviceId()[0] == '\0')
  {
    Serial.println("device id is empty - the request works, the printer has no ID string");
    return;
  }

  // A printer names itself with one of the two spellings of each key. Not finding
  // either in a *non-empty* string would mean it is not an IEEE 1284 device ID at
  // all, which is what a wrong wIndex packing produces.
  char value[64] = {};
  const bool haveMfg = slip.deviceIdField("MFG", value, sizeof(value)) > 0 ||
                       slip.deviceIdField("MANUFACTURER", value, sizeof(value)) > 0;
  Serial.printf("manufacturer \"%s\"\n", value);
  const bool haveModel = slip.deviceIdField("MDL", value, sizeof(value)) > 0 ||
                         slip.deviceIdField("MODEL", value, sizeof(value)) > 0;
  Serial.printf("model \"%s\"\n", value);
  if (slip.deviceIdField("CMD", value, sizeof(value)) > 0 ||
      slip.deviceIdField("COMMAND SET", value, sizeof(value)) > 0)
  {
    Serial.printf("command set \"%s\"\n", value);
  }
  if (!haveMfg && !haveModel)
  {
    fail("device id has neither a manufacturer nor a model field");
  }
}

static void testPortStatus()
{
  printer::PortStatus port;
  if (!slip.readPortStatus(port))
  {
    fail("GET_PORT_STATUS");
    return;
  }
  Serial.printf("port status 0x%02x unknown=%u paper_empty=%u selected=%u error=%u\n",
                port.raw,
                port.unknown ? 1 : 0,
                port.paperEmpty ? 1 : 0,
                port.selected ? 1 : 0,
                port.error ? 1 : 0);

  // 0x00 means the printer does not fill the byte in - measured on an XP-C58K,
  // which answers 0x00 before and after every other exchange while its real-time
  // status reports it ready. The request being answered is what is checked; the
  // paper verdict comes from the real-time status below.
  if (port.unknown)
  {
    Serial.println("port status carries no information on this printer");
    return;
  }
  if (port.paperEmpty)
  {
    fail("printer reports no paper - load a roll and run again");
  }
  if (port.error)
  {
    fail("printer reports an error - check the cover and the cutter");
  }
}

static void testRealtimeStatus()
{
  static const uint8_t which[] = {escpos::STATUS_PRINTER, escpos::STATUS_OFFLINE,
                                  escpos::STATUS_ERROR, escpos::STATUS_PAPER_ROLL};
  for (size_t i = 0; i < sizeof(which) / sizeof(which[0]); i++)
  {
    uint8_t value = 0;
    if (!slip.realtimeStatus(which[i], value))
    {
      Serial.printf("DLE EOT %u no answer\n", which[i]);
      fail("real-time status request");
      continue;
    }
    Serial.printf("DLE EOT %u 0x%02x\n", which[i], value);
    if (!statusByteShapeOk(value))
    {
      fail("real-time status byte does not have its fixed bits set");
    }
  }

  uint8_t paperByte = 0;
  if (slip.realtimeStatus(escpos::STATUS_PAPER_ROLL, paperByte))
  {
    const escpos::PaperStatus paper = escpos::decodePaperStatus(paperByte);
    Serial.printf("paper near_end=%u out=%u\n", paper.nearEnd ? 1 : 0, paper.out ? 1 : 0);
    if (paper.out)
    {
      fail("paper roll sensor reports out of paper - load a roll and run again");
    }
  }

  // The check a print loop makes, exercising the same path the example uses.
  bool paperOut = false;
  bool nearEnd = false;
  bool error = false;
  if (!slip.checkPaper(paperOut, nearEnd, error))
  {
    // On a bidirectional printer this cannot legitimately fail: the real-time path
    // answered above, and checkPaper() tries it first.
    fail("checkPaper() could not get an answer from either status path");
    return;
  }
  Serial.printf("checkPaper out=%u near_end=%u error=%u\n",
                paperOut ? 1 : 0,
                nearEnd ? 1 : 0,
                error ? 1 : 0);
  if (paperOut || error)
  {
    fail("checkPaper reports the printer is not ready to print");
  }
}

// Repeated polling is where a data-toggle problem shows up: the first exchange
// works and the next one times out. Each round is a bulk OUT and a bulk IN, so 20
// rounds without a miss means the endpoints stay in sync.
static void testRepeatedPolling()
{
  int answered = 0;
  for (int i = 0; i < STATUS_ROUNDS; i++)
  {
    uint8_t value = 0;
    if (slip.realtimeStatus(escpos::STATUS_PAPER_ROLL, value) && statusByteShapeOk(value))
    {
      answered++;
    }
    delay(20);
  }
  Serial.printf("repeated polling %d/%d answered\n", answered, STATUS_ROUNDS);
  if (answered != STATUS_ROUNDS)
  {
    fail("a repeated status poll went unanswered");
  }
}

// SOFT_RESET last, because it is the request most likely to disturb the endpoints:
// it flushes the printer's buffers. The point of the checks after it is that the
// data path still works, which is not a given - the USBTMC example had to drop
// CLEAR_FEATURE(ENDPOINT_HALT) for exactly this reason.
static void testSoftReset()
{
  if (!slip.softReset())
  {
    fail("SOFT_RESET");
    return;
  }
  Serial.println("SOFT_RESET ok");
  delay(200);

  printer::PortStatus port;
  if (!slip.readPortStatus(port))
  {
    fail("GET_PORT_STATUS after SOFT_RESET");
  }
  else
  {
    Serial.printf("port status after reset 0x%02x unknown=%u\n", port.raw, port.unknown ? 1 : 0);
  }

  uint8_t value = 0;
  if (!slip.realtimeStatus(escpos::STATUS_PAPER_ROLL, value))
  {
    fail("real-time status after SOFT_RESET - the bulk endpoints did not survive it");
  }
  else
  {
    Serial.printf("DLE EOT 4 after reset 0x%02x\n", value);
  }
}

static void runTest()
{
  Serial.printf("printer address=%u interface=%u protocol=0x%02x bidirectional=%u bulk_out=0x%02x bulk_in=0x%02x mps=%u\n",
                slip.address(),
                slip.interfaceNumber(),
                slip.protocol(),
                slip.isBidirectional() ? 1 : 0,
                usb.vendorOutEndpoint(slip.address()),
                usb.vendorInEndpoint(slip.address()),
                usb.vendorOutPacketSize(slip.address()));

  if (usb.vendorOutEndpoint(slip.address()) == 0)
  {
    fail("no bulk OUT endpoint");
  }
  if (slip.isBidirectional() && usb.vendorInEndpoint(slip.address()) == 0)
  {
    fail("a bidirectional interface with no bulk IN endpoint");
  }

  testDeviceId();
  testPortStatus();
  if (slip.isBidirectional())
  {
    testRealtimeStatus();
    testRepeatedPolling();
  }
  else
  {
    Serial.println("unidirectional interface: real-time status skipped");
  }
  testSoftReset();

  Serial.println(failed ? "[FAIL]" : "[PASS]");
}

void setup()
{
  Serial.begin(115200);
  delay(3000);

  usb.begin();
  Serial.println("printer_escpos test start");
}

void loop()
{
  static uint32_t lastAttemptMs = 0;
  static bool done = false;

  if (done)
  {
    delay(100);
    return;
  }

  if (millis() - lastAttemptMs >= 1000)
  {
    lastAttemptMs = millis();
    if (slip.begin())
    {
      done = true;
      runTest();
    }
  }
}
