#include "EspUsbHost.h"

EspUsbHost usb;

static const char *consumerUsageName(uint16_t usage)
{
  switch (usage)
  {
  case 0x00e2:
    return "Mute";
  case 0x00e9:
    return "Volume Up";
  case 0x00ea:
    return "Volume Down";
  case 0x00cd:
    return "Play/Pause";
  case 0x00b5:
    return "Next";
  case 0x00b6:
    return "Previous";
  default:
    return "";
  }
}

void setup()
{
  Serial.begin(115200);
  delay(500);

  Serial.println("EspUsbHost consumer control example start");

  usb.onDeviceConnected(espUsbHostPrintDeviceConnected);
  usb.onDeviceDisconnected(espUsbHostPrintDeviceDisconnected);

  usb.onConsumerControl([](const EspUsbHostConsumerControlEvent &event)
                        { Serial.printf("consumer %s usage=0x%04x %s\n",
                                        event.pressed ? "press" : "release",
                                        event.usage,
                                        consumerUsageName(event.usage)); });

  if (!usb.begin())
  {
    Serial.printf("usb.begin() failed: %s\n", usb.lastErrorName());
  }
}

void loop()
{
  delay(1);
}
