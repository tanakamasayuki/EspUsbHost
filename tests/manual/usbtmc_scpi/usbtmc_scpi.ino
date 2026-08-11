// Bring-up test for a USBTMC instrument (KIKUSUI PMX18-5A, VID 0x0b3e PID 0x1029):
// claim the class 0xfe interface, run the class requests on EP0 and exchange SCPI
// messages over the bulk endpoints. This is where the 12-byte message header, the
// bTag handling and the CLEAR sequence are confirmed against real hardware.
//
// The output stays off throughout: this test only sets values and reads them
// back, so it is safe to run with a load connected.
//
// The protocol, device and SCPI layers live with the example so there is one
// source of truth; tests/ is stripped from release archives while examples/ is
// not, so the dependency only ever points this way.
#include "../../../examples/Vendor/EspUsbHostUsbtmcScpi/ScpiPmx.hpp"

static constexpr uint32_t TEST_TIMEOUT_MS = 30000;
static constexpr uint32_t SETTLE_MS = 1500;

// Values only, never applied to the output.
static constexpr float TEST_VOLTAGE = 3.300f;
static constexpr float TEST_CURRENT = 0.250f;
static constexpr float TOLERANCE = 0.02f;

static EspUsbHost usb;
static pmx::ScpiPmx supply(usb);
static bool finished = false;
static uint32_t lastDeviceEventMs = 0;

static bool nearly(float actual, float expected)
{
  const float difference = actual > expected ? actual - expected : expected - actual;
  return difference <= TOLERANCE;
}

static bool runChecks()
{
  bool ok = true;

  // Interface discovery, before anything is claimed.
  uint8_t address = 0;
  uint8_t interfaceNumber = 0;
  uint8_t protocol = 0;
  if (!supply.usbtmc().find(address, interfaceNumber, protocol, pmx::VID))
  {
    Serial.println("no USBTMC interface found");
    return false;
  }
  Serial.printf("usbtmc interface address=%u interface=%u protocol=0x%02x\n",
                address,
                interfaceNumber,
                protocol);

  // vendorOpen() on a class 0xfe interface, GET_CAPABILITIES and the CLEAR
  // sequence all happen here.
  if (!supply.begin())
  {
    Serial.println("begin failed");
    return false;
  }

  // Raw GET_CAPABILITIES bytes, so a zero in the USB488 fields can be told apart
  // from a decoding mistake.
  uint8_t raw[usbtmc::CAPABILITIES_SIZE] = {};
  if (!supply.usbtmc().readCapabilities(raw, sizeof(raw)))
  {
    Serial.println("GET_CAPABILITIES failed");
    return false;
  }
  Serial.print("capabilities raw:");
  espUsbHostPrintHex(raw, sizeof(raw));
  Serial.println();

  const usbtmc::Capabilities &capabilities = supply.usbtmc().capabilities();
  Serial.printf("capabilities usbtmc=%04x usb488=%04x scpi=%u usb488.2=%u indicator=%u termchar=%u\n",
                capabilities.bcdUsbtmc,
                capabilities.bcdUsb488,
                capabilities.scpi ? 1 : 0,
                capabilities.usb488_2 ? 1 : 0,
                capabilities.indicatorPulse ? 1 : 0,
                capabilities.termCharSupported ? 1 : 0);
  if (!supply.usbtmc().isUsb488())
  {
    Serial.println("interface protocol is not USB488");
    ok = false;
  }
  // The device declares SCPI in its USB488 capability byte, which is the check
  // that the byte is being read at the right offset: bcdUSB488 sits in front of
  // it, so a two-byte slip makes this instrument look like it supports nothing.
  if (!capabilities.scpi)
  {
    Serial.println("device does not report the USB488 SCPI capability");
    ok = false;
  }

  char idn[128] = {};
  if (!supply.identify(idn, sizeof(idn)))
  {
    Serial.println("*IDN? failed");
    return false;
  }
  Serial.printf("idn %s\n", idn);
  if (strstr(idn, "KIKUSUI") == nullptr)
  {
    Serial.println("*IDN? does not name KIKUSUI");
    ok = false;
  }

  // A setting written and read back proves both directions of the message layer.
  if (!supply.setVoltage(TEST_VOLTAGE) || !supply.setCurrent(TEST_CURRENT))
  {
    Serial.println("writing the voltage/current setting failed");
    return false;
  }
  float volts = 0.0f;
  float amps = 0.0f;
  if (!supply.voltageSetting(volts) || !supply.currentSetting(amps))
  {
    Serial.println("reading the voltage/current setting failed");
    return false;
  }
  Serial.printf("setting readback %.3fV %.3fA\n", volts, amps);
  if (!nearly(volts, TEST_VOLTAGE) || !nearly(amps, TEST_CURRENT))
  {
    Serial.println("setting readback does not match what was written");
    ok = false;
  }

  // Measurements are read with the output off, so the values themselves are not
  // asserted - only that the queries answer.
  float measuredVolts = 0.0f;
  float measuredAmps = 0.0f;
  if (!supply.measureVoltage(measuredVolts) || !supply.measureCurrent(measuredAmps))
  {
    Serial.println("measurement query failed");
    ok = false;
  }
  else
  {
    Serial.printf("measured %.3fV %.3fA\n", measuredVolts, measuredAmps);
  }

  bool outputOn = true;
  if (!supply.output(outputOn))
  {
    Serial.println("OUTP? failed");
    ok = false;
  }
  else
  {
    Serial.printf("output %s\n", outputOn ? "ON" : "OFF");
  }

  // Repeated queries are where a bTag or synchronisation bug shows up.
  for (int i = 0; i < 20; i++)
  {
    float value = 0.0f;
    if (!supply.measureVoltage(value))
    {
      Serial.printf("repeated query %d failed\n", i);
      ok = false;
      break;
    }
  }
  Serial.println("repeated queries done");

  // CLEAR must be usable on a live connection, not just at open time.
  if (!supply.usbtmc().clear())
  {
    Serial.println("CLEAR failed");
    ok = false;
  }
  char afterClear[128] = {};
  if (!supply.identify(afterClear, sizeof(afterClear)))
  {
    Serial.println("*IDN? after CLEAR failed");
    ok = false;
  }

  // An empty error queue is the instrument's own verdict on every command above.
  int code = -1;
  char text[96] = {};
  if (!supply.error(code, text, sizeof(text)))
  {
    Serial.println("SYST:ERR? failed");
    ok = false;
  }
  else
  {
    Serial.printf("error queue %d %s\n", code, text);
    if (code != 0)
    {
      ok = false;
    }
  }

  return ok;
}

void setup()
{
  Serial.begin(115200);
  delay(5000);

  usb.onDeviceConnected([](const EspUsbHostDeviceInfo &device)
                        {
        lastDeviceEventMs = millis();
        Serial.printf("connected address=%u vid=%04x pid=%04x product=\"%s\"\n",
                      device.address,
                      device.vid,
                      device.pid,
                      device.product); });

  usb.onDeviceDisconnected([](const EspUsbHostDeviceInfo &device)
                           {
        lastDeviceEventMs = millis();
        Serial.printf("disconnected address=%u\n", device.address); });

  usb.begin();
  lastDeviceEventMs = millis();
  Serial.println("usbtmc_scpi test start");
  Serial.println("Connect a KIKUSUI PMX series power supply.");
}

void loop()
{
  static uint32_t startedAt = millis();

  if (!finished)
  {
    if (usb.deviceCount() > 0 && millis() - lastDeviceEventMs >= SETTLE_MS)
    {
      const bool ok = runChecks();
      Serial.println(ok ? "[PASS]" : "[FAIL]");
      finished = true;
    }
    else if (millis() - startedAt > TEST_TIMEOUT_MS)
    {
      usb.printAllDeviceInfo();
      Serial.println("[FAIL]");
      Serial.println("No USBTMC instrument was enumerated.");
      finished = true;
    }
  }

  delay(10);
}
