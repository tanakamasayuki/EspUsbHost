#include "USB.h"
#include "USBHIDVendor.h"

USBHIDVendor Vendor;

void setup()
{
    Serial.begin(115200);
    Vendor.begin();
    USB.begin();
}

void loop()
{
    if (Serial.available() > 0)
    {
        char command = Serial.read();
        if (command == 'h')
        {
            Vendor.print("hello vendor");
        }
    }
    delay(1);
}
