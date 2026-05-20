#include <Arduino.h>
#include <EspUsbHost.h>

EspUsbHost usb;

static bool printed = false;

void setup()
{
  Serial.begin(115200);
  delay(5000);

  usb.onHIDReportDescriptor([](const EspUsbHostHIDReportDescriptor &descriptor)
                            {
                              espUsbHostPrint(descriptor);
                              if (!printed)
                              {
                                printed = true;
                                Serial.println("[PASS]");
                              } });

  usb.onDeviceConnected([](const EspUsbHostDeviceInfo &device)
                        { Serial.printf("CONNECTED address=%u class=0x%02x supported=%s\n",
                                        device.address,
                                        device.deviceClass,
                                        device.supported ? "yes" : "no"); });

  if (!usb.begin())
  {
    Serial.printf("usb.begin failed: %s\n", usb.lastErrorName());
  }
  Serial.println("HID report descriptor test start");
}

void loop()
{
  delay(10);
}
