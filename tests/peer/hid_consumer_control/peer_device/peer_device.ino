#include "USB.h"
#include "USBHIDConsumerControl.h"

USBHIDConsumerControl ConsumerControl;

void setup()
{
    Serial.begin(115200);
    ConsumerControl.begin();
    USB.begin();
}

void loop()
{
    if (Serial.available() > 0)
    {
        char command = Serial.read();
        switch (command)
        {
        case 'u':
            ConsumerControl.press(CONSUMER_CONTROL_VOLUME_INCREMENT);
            ConsumerControl.release();
            break;
        case 'd':
            ConsumerControl.press(CONSUMER_CONTROL_VOLUME_DECREMENT);
            ConsumerControl.release();
            break;
        case 'p':
            ConsumerControl.press(CONSUMER_CONTROL_PLAY_PAUSE);
            ConsumerControl.release();
            break;
        }
    }
    delay(1);
}
