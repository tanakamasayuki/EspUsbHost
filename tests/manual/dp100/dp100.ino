// Bring-up test for an ALIENTEK DP100 (ATK-MDP100, 2e3c:af01): drive its HID
// framed protocol through onHIDInput() and sendHIDVendorOutput(), and check the
// read opcodes against what the USB descriptors and physics say.
//
// Read only. The setpoint frame (BASIC_SET) carries the output enable and its
// request form is not confirmed, so this test never sends it; it is safe to run
// with a load connected.
//
// The protocol, device and model layers live with the example so there is one
// source of truth; tests/ is stripped from release archives while examples/ is
// not, so the dependency only ever points this way.
#include "../../../examples/HID/EspUsbHostDp100Power/Dp100Power.hpp"

static constexpr uint32_t TEST_TIMEOUT_MS = 30000;
static constexpr uint32_t SETTLE_MS = 1500;

static EspUsbHost usb;
static dp100::Dp100Power supply(usb);
static bool finished = false;
static uint32_t lastDeviceEventMs = 0;

static void printHex(const uint8_t *data, size_t length)
{
  for (size_t i = 0; i < length; i++)
  {
    Serial.printf("%02x", data[i]);
  }
}

static bool runChecks()
{
  bool ok = true;

  uint8_t address = 0;
  uint8_t interfaceNumber = 0;
  if (!supply.device().find(address, interfaceNumber))
  {
    Serial.println("no DP100 HID interface found");
    return false;
  }
  Serial.printf("dp100 interface address=%u interface=%u\n", address, interfaceNumber);

  if (!supply.begin())
  {
    Serial.println("begin failed");
    return false;
  }

  // DEVICE_INFO: the device's own name and build date. The type string is the
  // device's ("ATK-DP100"), not the USB product string ("ATK-MDP100"), so both are
  // printed to keep that difference on the record.
  dp100::DeviceInfo info;
  if (!supply.identify(info))
  {
    Serial.println("DEVICE_INFO failed");
    return false;
  }
  Serial.printf("device type=\"%s\" hw=%u app=%u boot=%u run_area=0x%04x built=%u-%02u-%02u serial=",
                info.type,
                info.hardwareVersion,
                info.applicationVersion,
                info.bootVersion,
                info.runArea,
                info.year,
                info.month,
                info.day);
  printHex(info.serial, sizeof(info.serial));
  Serial.println();

  if (strstr(info.type, "DP100") == nullptr)
  {
    Serial.println("DEVICE_INFO type does not name DP100");
    ok = false;
  }
  if (info.year < 2020 || info.year > 2100 || info.month < 1 || info.month > 12 ||
      info.day < 1 || info.day > 31)
  {
    Serial.println("DEVICE_INFO build date is out of range, so the offsets are wrong");
    ok = false;
  }

  // BASIC_INFO: values that must be physically sensible. The DP100 runs from a USB
  // PD / QC input, so the input rail is at least 4.5 V, and the internal 5 V rail
  // and the temperatures pin the scale of the other fields.
  if (!supply.refresh())
  {
    Serial.println("BASIC_INFO failed");
    return false;
  }
  Serial.printf("basic in=%.3fV out=%.3fV %.3fA max_out=%.3fV rail5v=%.3fV temp=%.1f/%.1fC mode=%u status=%u\n",
                supply.inputVoltage(),
                supply.outputVoltage(),
                supply.outputCurrent(),
                supply.maxOutputVoltage(),
                supply.rail5v(),
                supply.temperature1(),
                supply.temperature2(),
                supply.outputMode(),
                supply.workStatus());

  if (supply.inputVoltage() < 4.0f || supply.inputVoltage() > 30.0f)
  {
    Serial.println("input voltage is not a plausible USB input rail");
    ok = false;
  }
  if (supply.rail5v() < 4.0f || supply.rail5v() > 6.0f)
  {
    Serial.println("internal 5V rail is out of range, so the mV scale is wrong");
    ok = false;
  }
  if (supply.temperature1() < -20.0f || supply.temperature1() > 120.0f)
  {
    Serial.println("temperature is out of range, so the 0.1degC scale is wrong");
    ok = false;
  }
  if (supply.maxOutputVoltage() <= 0.0f)
  {
    Serial.println("max output voltage is not reported");
    ok = false;
  }

  uint8_t systemInfo[32] = {};
  size_t systemLength = 0;
  if (!supply.device().readSystemInfo(systemInfo, sizeof(systemInfo), &systemLength))
  {
    Serial.println("SYSTEM_INFO failed");
    ok = false;
  }
  else
  {
    Serial.print("system info raw=");
    printHex(systemInfo, systemLength);
    Serial.printf(" len=%u\n", static_cast<unsigned>(systemLength));
  }

  // Repeated reads are where a CRC or request/response pairing bug shows up: a
  // frame taken for the wrong request would fail the opcode match here.
  const float firstInput = supply.inputVoltage();
  for (int i = 0; i < 50; i++)
  {
    if (!supply.refresh())
    {
      Serial.printf("repeated BASIC_INFO %d failed\n", i);
      ok = false;
      break;
    }
    // The input rail drifts by millivolts but must not jump: a mispaired frame
    // would decode some other opcode's bytes as a voltage.
    if (supply.inputVoltage() < firstInput - 1.0f || supply.inputVoltage() > firstInput + 1.0f)
    {
      Serial.printf("repeated read %d gave an implausible jump: %.3fV vs %.3fV\n",
                    i, supply.inputVoltage(), firstInput);
      ok = false;
      break;
    }
  }
  Serial.printf("repeated reads done refusals=%lu received=%lu\n",
                static_cast<unsigned long>(supply.device().refusalCount()),
                static_cast<unsigned long>(supply.device().receivedCount()));
  if (supply.device().refusalCount() != 0)
  {
    Serial.println("the device refused at least one frame, so a request is malformed");
    ok = false;
  }

  // Interleaving opcodes checks the pairing rather than just the framing.
  for (int i = 0; i < 5; i++)
  {
    dp100::DeviceInfo again;
    dp100::BasicInfo basic;
    if (!supply.identify(again) || !supply.device().readBasicInfo(basic))
    {
      Serial.printf("interleaved round %d failed\n", i);
      ok = false;
      break;
    }
    if (strcmp(again.type, info.type) != 0 || basic.rail5vMillivolts < 4000)
    {
      Serial.printf("interleaved round %d returned the wrong frame\n", i);
      ok = false;
      break;
    }
  }
  Serial.println("interleaved reads done");

  return ok;
}

void setup()
{
  Serial.begin(115200);
  delay(5000);

  usb.onDeviceConnected([](const EspUsbHostDeviceInfo &device)
                        {
        lastDeviceEventMs = millis();
        Serial.printf("connected address=%u vid=%04x pid=%04x product=\"%s\" serial=\"%s\"\n",
                      device.address,
                      device.vid,
                      device.pid,
                      device.product,
                      device.serial); });

  usb.onDeviceDisconnected([](const EspUsbHostDeviceInfo &device)
                           {
        lastDeviceEventMs = millis();
        Serial.printf("disconnected address=%u\n", device.address); });

  usb.begin();
  lastDeviceEventMs = millis();
  Serial.println("dp100 test start");
  Serial.println("Connect an ALIENTEK DP100 directly to the USB host port.");
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
      Serial.println("No DP100 was enumerated.");
      finished = true;
    }
  }

  delay(10);
}
