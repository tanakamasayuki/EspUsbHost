#include "EspUsbDevice.h"

// Attaches and detaches on its own random schedule once 'T' is received, so the
// host sees bus events at arbitrary points of its begin()/end() cycle.

EspUsbDevice device;
EspUsbDeviceVendor Vendor(device);

static bool toggling = false;
static bool attached = false;
static uint32_t nextToggleMs = 0;

static bool startDevice()
{
  EspUsbDeviceConfig config;
  config.vid = 0x303a;
  config.pid = 0x4019;
  config.manufacturer = "EspUsbDevice";
  config.product = "EspUsbDevice race peer";
  config.serialNumber = "espusb-race";
  return device.begin(config);
}

void setup()
{
  Serial.begin(115200);
  delay(500);
  attached = startDevice();
  Serial.printf("DEVICE_BEGIN %u\n", attached ? 1 : 0);
}

void loop()
{
  if (Serial.available() > 0)
  {
    const char command = Serial.read();
    if (command == 'T')
    {
      toggling = true;
      nextToggleMs = millis();
      Serial.println("DEVICE_TOGGLING");
    }
    else if (command == 'U')
    {
      toggling = false;
      Serial.println("DEVICE_TOGGLE_STOP");
    }
  }
  if (toggling && static_cast<int32_t>(millis() - nextToggleMs) >= 0)
  {
    if (attached)
    {
      device.end();
      attached = false;
    }
    else
    {
      attached = startDevice();
    }
    nextToggleMs = millis() + 20 + esp_random() % 400;
  }
  delay(1);
}
