#include "EspUsbHost.h"

EspUsbHost usb;

void setup()
{
    // Event prints can burst faster than the default serial TX buffer
    // drains; enlarge it so lines are not truncated mid-flight.
    Serial.setTxBufferSize(4096);
    Serial.begin(115200);
    delay(500);

    // Intentionally use listeners without onSystemControl() to verify that a
    // listener by itself enables parsing and delivery.
    usb.addSystemControlListener([](const EspUsbHostSystemControlEvent &event)
                                 { Serial.printf("SYSTEM usage=0x%02x pressed=%u released=%u\n",
                                                 event.usage,
                                                 event.pressed ? 1 : 0,
                                                 event.released ? 1 : 0); });
    usb.addSystemControlListener([](const EspUsbHostSystemControlEvent &event)
                                 { Serial.printf("SYSTEM_LISTENER usage=0x%02x pressed=%u\n",
                                                 event.usage,
                                                 event.pressed ? 1 : 0); });

    if (!usb.begin())
    {
        Serial.printf("usb.begin() failed: %s\n", usb.lastErrorName());
    }
}

void loop()
{
    delay(1);
}
