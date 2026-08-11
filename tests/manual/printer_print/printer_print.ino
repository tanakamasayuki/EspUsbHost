// Manual test: actually print. One receipt per run, then cut.
//
// This is the test that confirms the print data path end to end: a 1 KB-ish ESC/POS
// stream in a single bulk transfer, Japanese text from the printer's own font ROM, a
// CODE128 barcode, a QR code, and the cutter.
//
// PAPER: each run uses a slip. The test refuses to print if the printer reports
// paper out or an error, and checks the same afterwards, so a run that empties the
// roll fails rather than silently printing nothing.
//
// What a human has to check is on the slip itself, not in the log: the log can only
// say the printer accepted the bytes and stayed happy.

// The protocol, device, ESC/POS and receipt layers live with the example so there
// is one source of truth; tests/ is stripped from release archives while examples/
// is not, so the dependency only ever points this way.
#include "../../../examples/Vendor/EspUsbHostPrinterEscPos/PrinterDevice.hpp"
#include "../../../examples/Vendor/EspUsbHostPrinterEscPos/ReceiptJa.hpp"

static EspUsbHost usb;
static printer::PrinterDevice slip(usb);

// One slip per run. Both are what the example ships as opt-in, exercised here.
static constexpr bool CUT_PAPER = true;
static constexpr uint32_t SERIAL_NUMBER = 20260811;

// How long the printer is given to work through the receipt before the final status
// check. A 58 mm thermal printer takes a couple of seconds for a slip this long,
// and the cut is the last thing it does.
static constexpr uint32_t PRINT_TIME_MS = 4000;

static uint8_t printBuffer[printer::PRINT_BUFFER_SIZE];

static bool failed = false;

static void fail(const char *what)
{
  Serial.printf("FAILURE: %s\n", what);
  failed = true;
}

static bool statusByteShapeOk(uint8_t value)
{
  return (value & 0x01) == 0 && (value & 0x02) != 0;
}

// Reports the printer's own verdict. Returns false when it cannot be asked.
static bool reportStatus(const char *when, bool &paperOut, bool &error)
{
  paperOut = false;
  error = false;

  uint8_t printerByte = 0;
  uint8_t errorByte = 0;
  uint8_t paperByte = 0;
  if (!slip.realtimeStatus(escpos::STATUS_PRINTER, printerByte) ||
      !slip.realtimeStatus(escpos::STATUS_ERROR, errorByte) ||
      !slip.realtimeStatus(escpos::STATUS_PAPER_ROLL, paperByte))
  {
    Serial.printf("status %s: no answer\n", when);
    return false;
  }
  if (!statusByteShapeOk(printerByte) || !statusByteShapeOk(errorByte) ||
      !statusByteShapeOk(paperByte))
  {
    Serial.printf("status %s: a reply is not a status byte (%02x %02x %02x)\n",
                  when, printerByte, errorByte, paperByte);
    return false;
  }

  const escpos::PaperStatus paper = escpos::decodePaperStatus(paperByte);
  // DLE EOT 1 bit 3 is offline; DLE EOT 3 bit 3 is an error state, which covers the
  // cover being open and the cutter jamming - the two things a cut can provoke.
  const bool offline = (printerByte & 0x08) != 0;
  error = (errorByte & 0x08) != 0;
  paperOut = paper.out;
  Serial.printf("status %s: printer=0x%02x offline=%u error=0x%02x error_state=%u paper=0x%02x near_end=%u out=%u\n",
                when,
                printerByte,
                offline ? 1 : 0,
                errorByte,
                error ? 1 : 0,
                paperByte,
                paper.nearEnd ? 1 : 0,
                paper.out ? 1 : 0);
  return true;
}

static void runTest()
{
  Serial.printf("printer address=%u interface=%u protocol=0x%02x bulk_out=0x%02x mps=%u id=\"%s\"\n",
                slip.address(),
                slip.interfaceNumber(),
                slip.protocol(),
                usb.vendorOutEndpoint(slip.address()),
                usb.vendorOutPacketSize(slip.address()),
                slip.deviceId());

  bool paperOut = false;
  bool error = false;
  if (!reportStatus("before", paperOut, error))
  {
    fail("cannot read the printer status; refusing to print");
    Serial.println("[FAIL]");
    return;
  }
  if (paperOut)
  {
    // Not a bug in anything: the roll is empty. Printing anyway would queue the
    // receipt in the printer, to come out mixed into whatever prints next.
    fail("out of paper - load a roll and run again");
    Serial.println("[FAIL]");
    return;
  }
  if (error)
  {
    fail("printer reports an error state - check the cover and the cutter");
    Serial.println("[FAIL]");
    return;
  }

  escpos::Builder out(printBuffer, sizeof(printBuffer));
  receipt::buildReceipt(out, CUT_PAPER, SERIAL_NUMBER);
  if (!out.ok())
  {
    fail("the receipt does not fit in the print buffer");
    Serial.println("[FAIL]");
    return;
  }
  Serial.printf("receipt %u bytes, cut=%u\n", static_cast<unsigned>(out.length()), CUT_PAPER ? 1 : 0);

  // One transfer for the whole receipt. The library appends the zero-length packet
  // that terminates it, which is what tells the printer the stream is complete.
  if (!slip.write(out))
  {
    fail("bulk write failed");
    Serial.println("[FAIL]");
    return;
  }
  Serial.println("receipt sent");

  delay(PRINT_TIME_MS);

  if (!reportStatus("after", paperOut, error))
  {
    fail("no status after printing - the printer stopped answering");
  }
  else
  {
    if (error)
    {
      fail("printer reports an error state after printing - check the cutter");
    }
    if (paperOut)
    {
      // The roll ran out during this slip: the receipt is incomplete, so this is a
      // failure and a request for paper, not a pass.
      fail("out of paper after printing - the slip is incomplete, load a roll");
    }
  }

  // Printing is a long bulk transfer, so a repeat afterwards is worth having: it is
  // where a data-toggle problem provoked by the transfer would show up.
  int answered = 0;
  for (int i = 0; i < 5; i++)
  {
    uint8_t value = 0;
    if (slip.realtimeStatus(escpos::STATUS_PAPER_ROLL, value) && statusByteShapeOk(value))
    {
      answered++;
    }
    delay(50);
  }
  Serial.printf("status after printing %d/5 answered\n", answered);
  if (answered != 5)
  {
    fail("the status path did not survive the print transfer");
  }

  Serial.println("check the slip: Japanese text, a barcode, a QR code, and a clean cut");
  Serial.println(failed ? "[FAIL]" : "[PASS]");
}

void setup()
{
  Serial.begin(115200);
  delay(3000);

  usb.begin();
  Serial.println("printer_print test start");
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
