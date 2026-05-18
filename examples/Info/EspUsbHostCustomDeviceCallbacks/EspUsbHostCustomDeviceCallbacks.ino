#include <Arduino.h>
#include <EspUsbHost.h>

EspUsbHost usb;

static bool isKeyboardLike(const EspUsbHostDeviceInfo &device)
{
  EspUsbHostInterfaceInfo interfaces[ESP_USB_HOST_MAX_INTERFACES];
  const size_t count = usb.getInterfaces(device.address, interfaces, ESP_USB_HOST_MAX_INTERFACES);

  for (size_t i = 0; i < count; i++)
  {
    if (interfaces[i].interfaceClass == 0x03 && interfaces[i].interfaceProtocol == 0x01)
    {
      return true;
    }
  }
  return false;
}

static void onDeviceConnected(const EspUsbHostDeviceInfo &device)
{
  Serial.printf("connected #%u: %04x:%04x %s\n",
                device.address,
                device.vid,
                device.pid,
                device.product);

  if (isKeyboardLike(device))
  {
    Serial.println("  this device has a keyboard interface");
  }
  if (device.isHub)
  {
    Serial.println("  this device is a USB hub");
  }

  Serial.printf("  total devices: %u\n", static_cast<unsigned>(usb.deviceCount()));
}

static void onDeviceDisconnected(const EspUsbHostDeviceInfo &device)
{
  Serial.printf("disconnected #%u: %04x:%04x\n",
                device.address,
                device.vid,
                device.pid);
  Serial.printf("  total devices: %u\n", static_cast<unsigned>(usb.deviceCount()));
}

void setup()
{
  Serial.begin(115200);
  delay(500);
  Serial.println("EspUsbHost custom device callbacks example start");
  Serial.println("Press 'd' to print the full descriptor dump.");

  usb.onDeviceConnected(onDeviceConnected);
  usb.onDeviceDisconnected(onDeviceDisconnected);

  if (!usb.begin())
  {
    Serial.printf("usb.begin() failed: %s\n", usb.lastErrorName());
  }
}

void loop()
{
  if (Serial.available() > 0 && Serial.read() == 'd')
  {
    usb.printAllDeviceInfo();
  }
  delay(10);
}
