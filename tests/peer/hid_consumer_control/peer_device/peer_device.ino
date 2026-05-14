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
        case 'n':
            ConsumerControl.press(CONSUMER_CONTROL_SCAN_NEXT);
            ConsumerControl.release();
            break;
        case 's':
            ConsumerControl.press(CONSUMER_CONTROL_SCAN_PREVIOUS);
            ConsumerControl.release();
            break;
        case 'm':
            ConsumerControl.press(CONSUMER_CONTROL_MUTE);
            ConsumerControl.release();
            break;
        }
    }
    delay(1);
}
