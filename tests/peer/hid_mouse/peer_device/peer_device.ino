#include "USB.h"
#include "USBHIDMouse.h"

USBHIDMouse Mouse;

void setup()
{
    Serial.begin(115200);
    Mouse.begin();
    USB.begin();
}

void loop()
{
    if (Serial.available() > 0)
    {
        char command = Serial.read();
        switch (command)
        {
        case 'r':
            Mouse.move(40, 0, 0);
            break;
        case 'l':
            Mouse.move(-40, 0, 0);
            break;
        case 'u':
            Mouse.move(0, -40, 0);
            break;
        case 'd':
            Mouse.move(0, 40, 0);
            break;
        case 'w':
            Mouse.move(0, 0, 1);
            break;
        case 'm':
            Mouse.click(MOUSE_LEFT);
            break;
        }
    }
    delay(1);
}
