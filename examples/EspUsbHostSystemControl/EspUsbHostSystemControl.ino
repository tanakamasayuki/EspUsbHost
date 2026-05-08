#include "EspUsbHost.h"

EspUsbHost usb;

static const char *systemUsageName(uint8_t usage)
{
  switch (usage)
  {
  case ESP_USB_HOST_SYSTEM_CONTROL_POWER_OFF:
    return "Power Off";
  case ESP_USB_HOST_SYSTEM_CONTROL_STANDBY:
    return "Standby";
  case ESP_USB_HOST_SYSTEM_CONTROL_WAKE_HOST:
    return "Wake Host";
  default:
    return "";
  }
}

void setup()
{
  Serial.begin(115200);
  delay(500);

  Serial.println("EspUsbHost system control example start");

  usb.onSystemControl([](const EspUsbHostSystemControlEvent &event)
                      { Serial.printf("system %s usage=0x%02x %s\n",
                                      event.pressed ? "press" : "release",
                                      event.usage,
                                      systemUsageName(event.usage)); });

  if (!usb.begin())
  {
    Serial.printf("usb.begin() failed: %s\n", usb.lastErrorName());
  }
}

void loop()
{
  delay(1);
}
