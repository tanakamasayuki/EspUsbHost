// Read an ALIENTEK DP100 digital power supply (ATK-MDP100, 2e3c:af01) over USB.
//
// The DP100 is a plain HID device: one interface with a 64-byte interrupt IN and a
// 64-byte interrupt OUT, carrying its own framed protocol. The library needs no
// additions for it - onHIDInput() delivers the answers and sendHIDVendorOutput()
// sends the requests - which is why this example lives under examples/HID/. See
// README.md.
//
// SAFETY: as shipped this sketch only reads. The setpoint frame is implemented but
// not confirmed against hardware, so it stays behind APPLY_SETPOINT, which is
// false by default. Do not enable it with anything connected to the output
// terminals.

#include "EspUsbHost.h"
#include "Dp100Power.hpp"

EspUsbHost usb;
dp100::Dp100Power supply(usb);

// Leave false unless you are deliberately testing the unverified write path with
// the output terminals disconnected.
static constexpr bool APPLY_SETPOINT = false;
static constexpr float SET_VOLTAGE = 5.0f;
static constexpr float SET_CURRENT = 0.5f;

static constexpr uint32_t READ_INTERVAL_MS = 1000;

static bool connected = false;

static void printHex(const uint8_t *data, size_t length)
{
  for (size_t i = 0; i < length; i++)
  {
    Serial.printf("%02x", data[i]);
  }
}

static bool connect()
{
  if (!supply.begin())
  {
    return false;
  }

  dp100::Dp100Device &device = supply.device();
  Serial.printf("DP100 ready address=%u interface=%u\n", device.address(), device.interfaceNumber());

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

  // Field meanings are not established for SYSTEM_INFO, so it is printed raw.
  uint8_t systemInfo[32] = {};
  size_t systemLength = 0;
  if (supply.device().readSystemInfo(systemInfo, sizeof(systemInfo), &systemLength))
  {
    Serial.print("system info raw=");
    printHex(systemInfo, systemLength);
    Serial.println();
  }

  if (!supply.refresh())
  {
    Serial.println("BASIC_INFO failed");
    return false;
  }
  Serial.printf("input %.3fV max_output %.3fV rail5v %.3fV temp %.1f/%.1fC mode=%u status=%u\n",
                supply.inputVoltage(),
                supply.maxOutputVoltage(),
                supply.rail5v(),
                supply.temperature1(),
                supply.temperature2(),
                supply.outputMode(),
                supply.workStatus());

  if (APPLY_SETPOINT)
  {
    // Unverified path. A failure here means the request form is not confirmed,
    // not that the supply is broken.
    Serial.printf("applying %.3fV %.3fA (unverified path)\n", SET_VOLTAGE, SET_CURRENT);
    if (!supply.apply(SET_VOLTAGE, SET_CURRENT, false))
    {
      Serial.println("BASIC_SET was refused or went unanswered");
    }
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
        if (connected && device.address == supply.device().address())
        {
          supply.end();
          connected = false;
        } });

  // Dp100Power::begin() takes over onHIDInput(), so it must be set up after
  // usb.begin() has enumerated the device; connect() below does both in order.
  usb.begin();
  Serial.println("EspUsbHostDp100Power start");
  Serial.println("Connect an ALIENTEK DP100 directly to the USB host port.");
}

void loop()
{
  static uint32_t lastAttemptMs = 0;
  static uint32_t lastReadMs = 0;

  if (!connected)
  {
    if (millis() - lastAttemptMs >= 1000)
    {
      lastAttemptMs = millis();
      connected = connect();
    }
    return;
  }

  if (millis() - lastReadMs >= READ_INTERVAL_MS)
  {
    lastReadMs = millis();
    if (supply.refresh())
    {
      Serial.printf("out %.3fV %.3fA  in %.3fV  temp %.1fC  mode=%u status=%u\n",
                    supply.outputVoltage(),
                    supply.outputCurrent(),
                    supply.inputVoltage(),
                    supply.temperature1(),
                    supply.outputMode(),
                    supply.workStatus());
    }
    else
    {
      Serial.printf("BASIC_INFO failed (refusals=%lu received=%lu)\n",
                    static_cast<unsigned long>(supply.device().refusalCount()),
                    static_cast<unsigned long>(supply.device().receivedCount()));
    }
  }
}
