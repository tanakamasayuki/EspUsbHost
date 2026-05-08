#include "USB.h"
#include "USBHIDGamepad.h"

USBHIDGamepad Gamepad;

void setup()
{
    Serial.begin(115200);
    Gamepad.begin();
    USB.begin();
}

void loop()
{
    if (Serial.available() > 0)
    {
        char command = Serial.read();
        switch (command)
        {
        case 'a':
            Gamepad.send(10, -10, 20, -20, 30, -30, HAT_RIGHT, 0x00000005);
            break;
        case '0':
            Gamepad.send(0, 0, 0, 0, 0, 0, HAT_CENTER, 0);
            break;
        }
    }
    delay(1);
}
