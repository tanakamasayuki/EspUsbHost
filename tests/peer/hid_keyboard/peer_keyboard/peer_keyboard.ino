#include "USB.h"
#include "USBHIDKeyboard.h"
USBHIDKeyboard Keyboard;

void setup()
{
    Serial.begin(115200);
    Keyboard.begin();
    USB.begin();
}

void loop()
{
    if (Serial.available() > 0)
    {
        Keyboard.write(Serial.read());
    }
    delay(1);
}