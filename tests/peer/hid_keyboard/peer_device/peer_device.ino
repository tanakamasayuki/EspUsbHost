#include "USB.h"
#include "USBHIDKeyboard.h"
#include "USBHID.h"

static volatile bool gLedReceived = false;
static volatile uint8_t gLedValue = 0;

class KeyboardWithLed : public USBHIDKeyboard {
public:
    void _onSetFeature(uint8_t report_id, const uint8_t *buffer, uint16_t len) override {
        if (len >= 1) {
            gLedValue = buffer[0];
            gLedReceived = true;
        }
    }
    void _onOutput(uint8_t report_id, const uint8_t *buffer, uint16_t len) override {
        if (len >= 1) {
            gLedValue = buffer[0];
            gLedReceived = true;
        }
    }
};
KeyboardWithLed Keyboard;

static void sendRawKey(uint8_t modifier, uint8_t keycode)
{
    KeyReport report = {};
    report.modifiers = modifier;
    report.keys[0] = keycode;
    Keyboard.sendReport(&report);
    delay(20);
    report = {};
    Keyboard.sendReport(&report);
}

void setup()
{
    Serial.begin(115200);
    Keyboard.begin();
    USB.begin();
}

void loop()
{
    if (gLedReceived)
    {
        gLedReceived = false;
        uint8_t leds = gLedValue;
        Serial.printf("LED numlock=%d capslock=%d scrolllock=%d\n",
                      (leds >> 0) & 1, (leds >> 1) & 1, (leds >> 2) & 1);
    }
    if (Serial.available() > 0)
    {
        const char command = Serial.read();
        if (command == '@')
        {
            sendRawKey(0x02, 0x1f);
            Serial.println("SEND @ 1");
        }
        else if (command == '_')
        {
            sendRawKey(0x02, 0x87);
            Serial.println("SEND _ 1");
        }
        else if (command >= 0x20 && command <= 0x7e)
        {
            Keyboard.write(command);
        }
    }
    delay(1);
}
