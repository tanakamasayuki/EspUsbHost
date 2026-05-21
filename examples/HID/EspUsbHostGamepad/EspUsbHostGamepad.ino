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
    // en: Interpret reportData/rawData in application code for your controller.
    // ja: コントローラーごとの解釈は、アプリケーション側でreportData/rawDataを見て行います。
    uint32_t pressed  = event.buttons & ~event.previousButtons;
    uint32_t released = event.previousButtons & ~event.buttons;

    Serial.printf("report[%u]=", (unsigned)event.reportLength);
    printHex(event.reportData, event.reportLength);
    Serial.print(' ');
    if (event.hasHat)
    {
      Serial.printf("hat=%u ", event.hat);
    }
    else
    {
      Serial.print("hat=- ");
    }
    Serial.printf("buttons=0x%08lx", (unsigned long)event.buttons);
    if (pressed)  Serial.printf(" +0x%lx", (unsigned long)pressed);
    if (released) Serial.printf(" -0x%lx", (unsigned long)released);
    Serial.println(); });

  if (!usb.begin())
  {
    Serial.printf("usb.begin() failed: %s\n", usb.lastErrorName());
  }
}

void loop()
{
  delay(1);
}
