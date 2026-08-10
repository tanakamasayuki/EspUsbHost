#include "EspUsbHost.h"
#include "Rcs300Device.hpp"

// Reads a FeliCa IDm with a System Code of your choosing, through a Sony RC-S300.
//
// Why this is not just ccidPowerOn() + Get UID: a CCID reader polls the field on
// its own, and CCID gives the host no way to say what to poll for. Whatever
// answers the reader's own wildcard poll is what the host gets. On a phone that
// is the wallet's own card, which is why an iPhone reports an Apple Pay
// identifier and not the Suica sitting next to it.
//
// Selecting a system means polling for it, so this example takes the RF field
// over with the reader's transparent session and sends the FeliCa Polling frame
// itself. Two polls are run for comparison: the wildcard 0xffff, which is what
// the reader would have done, and 0x0003, the transit system Suica lives in.
//
// The command set is Sony's, not CCID's, so this only works on an RC-S300 --
// see README.md.

EspUsbHost usb;
rcs300::Rcs300Reader reader(usb);

static constexpr uint32_t POLL_INTERVAL_MS = 1000;

static void printTarget(const char *label, uint16_t systemCode, const felica::Target &target)
{
  Serial.printf("%s SC=%04x IDm=", label, systemCode);
  rcs300::Rcs300Reader::printHex(target.idm, sizeof(target.idm));
  Serial.print("  PMm=");
  rcs300::Rcs300Reader::printHex(target.pmm, sizeof(target.pmm));
  if (target.hasRequestData)
  {
    // With Request Code 0x01 the target reports the system it answered from. After
    // a wildcard poll this is the interesting part: it names what was reached.
    Serial.printf("  answering system code=%04x\n", target.requestData);
  }
}

static void pollFor(const char *label, uint16_t systemCode)
{
  felica::Target target;
  if (reader.readTarget(systemCode, target))
  {
    printTarget(label, systemCode, target);
    return;
  }
  if (reader.noAnswer())
  {
    Serial.printf("%s SC=%04x no target answered\n", label, systemCode);
    return;
  }
  Serial.printf("%s SC=%04x failed result=0x%02x sw=%04x\n",
                label,
                systemCode,
                reader.lastResult(),
                reader.lastStatusWord());
}

void setup()
{
  Serial.begin(115200);
  delay(1000);

  usb.onDeviceConnected([](const EspUsbHostDeviceInfo &device)
                        { Serial.printf("connected: address=%u vid=%04x pid=%04x product=\"%s\"\n",
                                        device.address, device.vid, device.pid, device.product); });

  usb.begin();
  Serial.println("EspUsbHost FeliCa IDm example start");
}

void loop()
{
  if (!reader.isOpen())
  {
    if (reader.open())
    {
      Serial.printf("RC-S300 ready: address=%u\n", reader.address());
    }
    else
    {
      delay(500);
      return;
    }
  }

  // Wildcard first, then the transit system. On a plain Suica both answer with
  // the same IDm; on a phone they are what differ.
  pollFor("wildcard", felica::SYSTEM_CODE_ANY);
  pollFor("transit ", felica::SYSTEM_CODE_TRANSIT);

  delay(POLL_INTERVAL_MS);
}
