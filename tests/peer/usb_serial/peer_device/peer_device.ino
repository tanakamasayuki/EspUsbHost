#include "USB.h"

#if !ARDUINO_USB_CDC_ON_BOOT
USBCDC USBSerial;
#endif

void setup()
{
    Serial.begin(115200);
    USBSerial.begin();
    USB.begin();
}

void loop()
{
    if (Serial.available() > 0)
    {
        char command = Serial.read();
        if (command == 'd')
        {
            USBSerial.print("device to host");
        }
    }

    while (USBSerial.available() > 0)
    {
        Serial.print("DEVICE_RX ");
        while (USBSerial.available() > 0)
        {
            Serial.write(USBSerial.read());
        }
        Serial.println();
    }
    delay(1);
}
