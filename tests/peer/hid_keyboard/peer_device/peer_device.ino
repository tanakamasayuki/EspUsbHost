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
        Keyboard.write(Serial.read());
    }
    delay(1);
}
