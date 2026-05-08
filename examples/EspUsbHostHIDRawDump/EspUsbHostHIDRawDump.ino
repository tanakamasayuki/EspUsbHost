#include "EspUsbHost.h"

EspUsbHost usb;

static void printHex(const uint8_t *data, size_t length)
{
  for (size_t i = 0; i < length; i++) {
    if (data[i] < 0x10) {
      Serial.print('0');
    }
    Serial.print(data[i], HEX);
    if (i + 1 < length) {
      Serial.print(' ');
    }
  }
}

void setup()
{
  Serial.begin(115200);
  delay(500);

  usb.onDeviceConnected([](const EspUsbHostDeviceInfo &device) {
    Serial.printf("connected: address=%u vid=%04x pid=%04x product=%s\n",
                  device.address,
                  device.vid,
                  device.pid,
                  device.product);
  });

  usb.onDeviceDisconnected([](const EspUsbHostDeviceInfo &device) {
    Serial.printf("disconnected: address=%u vid=%04x pid=%04x\n",
                  device.address,
                  device.vid,
                  device.pid);
  });

  usb.onHIDInput([](const EspUsbHostHIDInput &input) {
    Serial.printf("hid: address=%u iface=%u subclass=0x%02x protocol=0x%02x len=%u data=",
                  input.address,
                  input.interfaceNumber,
                  input.subclass,
                  input.protocol,
                  static_cast<unsigned>(input.length));
    printHex(input.data, input.length);
    Serial.println();
  });

  if (!usb.begin()) {
    Serial.printf("usb.begin() failed: %s\n", usb.lastErrorName());
  }
}

void loop()
{
}
