#include "EspUsbHost.h"

EspUsbHost usb;

static void printHex(const uint8_t *data, size_t length)
{
  for (size_t i = 0; i < length; i++)
  {
    if (data[i] < 0x10)
    {
      Serial.print('0');
    }
    Serial.print(data[i], HEX);
    if (i + 1 < length)
    {
      Serial.print(' ');
    }
  }
}

void setup()
{
  Serial.begin(115200);
  delay(500);

  Serial.println("EspUsbHost gamepad example start");

  usb.onDeviceConnected([](const EspUsbHostDeviceInfo &device)
                        {
                          Serial.print("connected: ");
                          espUsbHostPrint(device); });

  usb.onDeviceDisconnected([](const EspUsbHostDeviceInfo &device)
                           {
                             Serial.print("disconnected: ");
                             espUsbHostPrint(device); });

  usb.onGamepad([](const EspUsbHostGamepadEvent &event)
                {
    Serial.printf("report[%u]=", (unsigned)event.reportLength);
    printHex(event.reportData, event.reportLength);
    Serial.println();

    for (size_t i = 0; i < event.fieldCount; i++)
    {
      const EspUsbHostHIDFieldValue &field = event.fields[i];
      Serial.printf("  field[%u] page=0x%04x usage=0x%04x value=%ld logical=%ld..%ld bits=%u@%u flags=0x%02x\n",
                    (unsigned)i,
                    field.usagePage,
                    field.usage,
                    (long)field.value,
                    (long)field.logicalMin,
                    (long)field.logicalMax,
                    field.bitSize,
                    field.bitOffset,
                    field.flags);
    }

    // en: Example mapping. Replace VID/PID and usage choices with your controller.
    // ja: マッピング例です。VID/PIDとusageの選択はコントローラーに合わせて置き換えてください。
    if (event.vid == 0x054c)
    {
      int32_t lx = 0;
      int32_t ly = 0;
      uint32_t buttons = 0;
      for (size_t i = 0; i < event.fieldCount; i++)
      {
        const EspUsbHostHIDFieldValue &field = event.fields[i];
        if (field.usagePage == 0x01 && field.usage == 0x30)
        {
          lx = field.value;
        }
        else if (field.usagePage == 0x01 && field.usage == 0x31)
        {
          ly = field.value;
        }
        else if (field.usagePage == 0x09 && field.value && field.usage > 0 && field.usage <= 32)
        {
          buttons |= 1UL << (field.usage - 1);
        }
      }
      Serial.printf("  mapped lx=%ld ly=%ld buttons=0x%08lx\n",
                    (long)lx,
                    (long)ly,
                    (unsigned long)buttons);
    } });

  if (!usb.begin())
  {
    Serial.printf("usb.begin() failed: %s\n", usb.lastErrorName());
  }
}

void loop()
{
  delay(1);
}
