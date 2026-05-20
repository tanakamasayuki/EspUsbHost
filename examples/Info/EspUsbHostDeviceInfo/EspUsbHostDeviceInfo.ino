#include <Arduino.h>
#include <EspUsbHost.h>

EspUsbHost usb;

static bool printedInitialTopology = false;
static uint32_t lastDeviceEventMs = 0;

void setup()
{
  Serial.begin(115200);
  delay(500);
  Serial.println("EspUsbHostDeviceInfo start");
  Serial.println("Press 'r' to reprint connected devices and hub status.");

  usb.onDeviceConnected([](const EspUsbHostDeviceInfo &device)
                        {
                          lastDeviceEventMs = millis();
                          Serial.printf("CONNECTED address=%u\n", device.address);
                          usb.printDeviceInfo(device.address); });

  usb.onDeviceDisconnected([](const EspUsbHostDeviceInfo &device)
                           {
                             lastDeviceEventMs = millis();
                             Serial.printf("DISCONNECTED address=%u vid=%04x pid=%04x\n",
                                           device.address,
                                           device.vid,
                                           device.pid); });

  if (!usb.begin())
  {
    Serial.printf("usb.begin failed: %s\n", usb.lastErrorName());
  }
  lastDeviceEventMs = millis();
}

void loop()
{
  if (Serial.available() > 0 && Serial.read() == 'r')
  {
    usb.printAllDeviceInfo();
  }
  if (!printedInitialTopology && millis() - lastDeviceEventMs > 2000)
  {
    printedInitialTopology = true;
    usb.printAllDeviceInfo();
  }
  delay(10);
}
