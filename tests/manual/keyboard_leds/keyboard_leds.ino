#include "EspUsbHost.h"

EspUsbHost usb;

static volatile bool keyboardConnected = false;
static constexpr uint32_t LED_RETRY_DELAY_MS = 100;
static constexpr uint32_t LED_SETTLE_DELAY_MS = 300;
static constexpr uint32_t INITIAL_LED_SETTLE_DELAY_MS = 500;

static void waitForAck()
{
    while (Serial.read() < 0)
    {
        delay(10);
    }
}

static void waitForKeyboard()
{
    while (!keyboardConnected)
    {
        delay(10);
    }
    delay(INITIAL_LED_SETTLE_DELAY_MS);
}

static void setKeyboardLeds(bool numLock, bool capsLock, bool scrollLock)
{
    while (!usb.setKeyboardLeds(numLock, capsLock, scrollLock))
    {
        delay(LED_RETRY_DELAY_MS);
    }
    delay(LED_SETTLE_DELAY_MS);
}

static void setKeyboardLedsAndPrompt(bool numLock, bool capsLock, bool scrollLock, const char *prompt)
{
    setKeyboardLeds(numLock, capsLock, scrollLock);
    Serial.println(prompt);
    waitForAck();
}

void setup()
{
    Serial.begin(115200);
    delay(5000);
    usb.onDeviceConnected([](const EspUsbHostDeviceInfo &)
                          { keyboardConnected = true; });
    usb.onDeviceDisconnected([](const EspUsbHostDeviceInfo &)
                             { keyboardConnected = false; });
    usb.begin();
}

void loop()
{
    waitForKeyboard();
    setKeyboardLeds(false, false, false);

    setKeyboardLedsAndPrompt(true, false, false, "NumLock ON");
    setKeyboardLedsAndPrompt(false, false, false, "NumLock OFF");

    setKeyboardLedsAndPrompt(false, true, false, "CapsLock ON");
    setKeyboardLedsAndPrompt(false, false, false, "CapsLock OFF");

    setKeyboardLedsAndPrompt(false, false, true, "ScrollLock ON");
    setKeyboardLedsAndPrompt(false, false, false, "ScrollLock OFF");
}
