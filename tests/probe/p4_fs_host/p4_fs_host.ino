#include "EspUsbHost.h"

EspUsbHost usb;

static volatile bool deviceSeen = false;

void setup()
{
  Serial.begin(115200);
  delay(1000);

  Serial.println("TEST_BEGIN p4_fs_host_probe");

  usb.onDeviceConnected([](const EspUsbHostDeviceInfo &device)
                        {
                          Serial.print("HOST_DEVICE connected ");
                          espUsbHostPrint(device, Serial);
                          deviceSeen = true;
                        });

  usb.onDeviceDisconnected([](const EspUsbHostDeviceInfo &device)
                           {
                             Serial.print("HOST_DEVICE disconnected ");
                             espUsbHostPrint(device, Serial);
                           });

  EspUsbHostConfig config;
  config.port = ESP_USB_HOST_PORT_FULL_SPEED;
  if (!usb.begin(config))
  {
    Serial.printf("HOST_BEGIN_FAILED %s\n", usb.lastErrorName());
    Serial.println("TEST_END fail");
    Serial.println("NG");
    return;
  }

  Serial.println("HOST_READY fs");
  Serial.println("Attach a USB full-speed device to the P4 FS OTG port.");
}

void loop()
{
  static uint32_t lastPrint = 0;
  if (millis() - lastPrint >= 5000)
  {
    lastPrint = millis();
    Serial.printf("HOST_WAITING seen=%u last_error=%s\n",
                  deviceSeen ? 1 : 0,
                  usb.lastErrorName());
    if (deviceSeen)
    {
      usb.printAllDeviceInfo();
    }
  }
  delay(1);
}
