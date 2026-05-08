#include "USB.h"
#include "USBHIDSystemControl.h"

USBHIDSystemControl SystemControl;

void setup()
{
    Serial.begin(115200);
    SystemControl.begin();
    USB.begin();
}

void loop()
{
    if (Serial.available() > 0)
    {
        char command = Serial.read();
        switch (command)
        {
        case 'p':
            SystemControl.press(SYSTEM_CONTROL_POWER_OFF);
            SystemControl.release();
            break;
        case 's':
            SystemControl.press(SYSTEM_CONTROL_STANDBY);
            SystemControl.release();
            break;
        }
    }
    delay(1);
}
