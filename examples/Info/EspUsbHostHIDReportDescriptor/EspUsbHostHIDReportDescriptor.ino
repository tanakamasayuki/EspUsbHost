#include <Arduino.h>
#include <EspUsbHost.h>

EspUsbHost usb;

static uint32_t reportDescriptorCount = 0;

void setup()
{
  Serial.begin(115200);
  delay(5000);
  Serial.println("EspUsbHostHIDReportDescriptor start");
  Serial.println("Connect a HID device to print its HID report descriptor.");

  usb.onDeviceConnected([](const EspUsbHostDeviceInfo &device)
                        {
                          Serial.print("CONNECTED ");
                          espUsbHostPrint(device);
                          // en: Print the regular USB descriptor summary first.
                          // ja: 先に通常のUSBディスクリプタ概要を表示します。
                          usb.printDeviceInfo(device.address); });

  usb.onDeviceDisconnected([](const EspUsbHostDeviceInfo &device)
                           {
                             Serial.print("DISCONNECTED ");
                             espUsbHostPrint(device); });

  usb.onHIDReportDescriptor([](const EspUsbHostHIDReportDescriptor &descriptor)
                            {
                              reportDescriptorCount++;
                              Serial.printf("\n===== HID Report Descriptor #%lu =====\n",
                                            static_cast<unsigned long>(reportDescriptorCount));
                              espUsbHostPrint(descriptor);
                              Serial.println("=== HID Report Descriptor End ===\n"); });

  if (!usb.begin())
  {
    Serial.printf("usb.begin failed: %s\n", usb.lastErrorName());
  }
}

void loop()
{
  delay(10);
}
