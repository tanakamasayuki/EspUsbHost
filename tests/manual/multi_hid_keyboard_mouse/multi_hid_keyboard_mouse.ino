#include "EspUsbHost.h"

EspUsbHost usb;

static bool keyboardEvent = false;
static bool mouseEvent = false;
static bool passed = false;

static void checkPass()
{
    if (keyboardEvent && mouseEvent && !passed)
    {
        passed = true;
        Serial.println("[PASS]");
    }
}

void setup()
{
    Serial.begin(115200);
    delay(5000);

    usb.onKeyboard([](const EspUsbHostKeyboardEvent &event)
                   {
        if (!keyboardEvent)
        {
            keyboardEvent = true;
            Serial.println("Keyboard event received");
            checkPass();
        } });

    usb.onMouse([](const EspUsbHostMouseEvent &event)
                {
        if (!mouseEvent)
        {
            mouseEvent = true;
            Serial.println("Mouse event received");
            checkPass();
        } });

    usb.begin();
    Serial.println("Press a key and move the mouse...");
}

void loop()
{
    delay(1);
}
