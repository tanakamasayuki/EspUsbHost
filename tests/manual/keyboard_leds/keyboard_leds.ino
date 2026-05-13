#include "EspUsbHost.h"

EspUsbHost usb;

static void waitForAck()
{
    while (Serial.read() < 0)
    {
        delay(10);
    }
}

void setup()
{
    Serial.begin(115200);
    delay(5000);
    usb.begin();
}

void loop()
{
    usb.setKeyboardLeds(true, false, false);
    Serial.println("NumLock ON");
    waitForAck();

    usb.setKeyboardLeds(false, false, false);
    Serial.println("NumLock OFF");
    waitForAck();

    usb.setKeyboardLeds(false, true, false);
    Serial.println("CapsLock ON");
    waitForAck();

    usb.setKeyboardLeds(false, false, false);
    Serial.println("CapsLock OFF");
    waitForAck();

    usb.setKeyboardLeds(false, false, true);
    Serial.println("ScrollLock ON");
    waitForAck();

    usb.setKeyboardLeds(false, false, false);
    Serial.println("ScrollLock OFF");
    waitForAck();
}
