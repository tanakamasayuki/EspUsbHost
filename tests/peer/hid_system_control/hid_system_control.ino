#include "EspUsbHost.h"

EspUsbHost usb;

void setup()
{
    Serial.begin(115200);
    delay(500);

    usb.onSystemControl([](const EspUsbHostSystemControlEvent &event)
                        { Serial.printf("SYSTEM usage=0x%02x pressed=%u released=%u\n",
                                        event.usage,
                                        event.pressed ? 1 : 0,
                                        event.released ? 1 : 0); });

    if (!usb.begin())
    {
        Serial.printf("usb.begin() failed: %s\n", usb.lastErrorName());
    }
}

void loop()
{
    delay(1);
}
