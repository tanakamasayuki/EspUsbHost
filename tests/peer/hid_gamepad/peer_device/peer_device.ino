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
        case '1':
            Gamepad.send(0, 0, 0, 0, 0, 0, HAT_UP, 0);
            break;
        case '2':
            Gamepad.send(0, 0, 0, 0, 0, 0, HAT_UP_RIGHT, 0);
            break;
        case '3':
            Gamepad.send(0, 0, 0, 0, 0, 0, HAT_RIGHT, 0);
            break;
        case '4':
            Gamepad.send(0, 0, 0, 0, 0, 0, HAT_DOWN_RIGHT, 0);
            break;
        case '5':
            Gamepad.send(0, 0, 0, 0, 0, 0, HAT_DOWN, 0);
            break;
        case '6':
            Gamepad.send(0, 0, 0, 0, 0, 0, HAT_DOWN_LEFT, 0);
            break;
        case '7':
            Gamepad.send(0, 0, 0, 0, 0, 0, HAT_LEFT, 0);
            break;
        case '8':
            Gamepad.send(0, 0, 0, 0, 0, 0, HAT_UP_LEFT, 0);
            break;
        case 'b':
            Gamepad.send(0, 0, 0, 0, 0, 0, HAT_CENTER, 0x00007fff);
            break;
        }
    }
    delay(1);
}
