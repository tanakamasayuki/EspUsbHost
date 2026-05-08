#include "USB.h"
#include "USBHIDKeyboard.h"
#include "USBHIDMouse.h"

USBHIDKeyboard Keyboard;
USBHIDMouse Mouse;

void setup()
{
    Serial.begin(115200);
    Keyboard.begin();
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
        case 'k':
            Keyboard.write('k');
            break;
        case 'r':
            Mouse.move(40, 0, 0);
            break;
        case 'm':
            Mouse.click(MOUSE_LEFT);
            break;
        }
    }
    delay(1);
}
