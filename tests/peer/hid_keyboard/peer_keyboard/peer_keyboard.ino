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
    const char *str = "hello, keyboard.\n";
    Keyboard.write((const uint8_t *)str, strlen(str));
    delay(1000);
}