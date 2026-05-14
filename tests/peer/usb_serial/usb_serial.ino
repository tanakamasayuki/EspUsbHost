#include "EspUsbHost.h"

EspUsbHost usb;
EspUsbHostCdcSerial CdcSerial(usb);

void setup()
{
    Serial.begin(115200);
    delay(500);

    CdcSerial.begin(115200);

    if (!usb.begin())
    {
        Serial.printf("usb.begin() failed: %s\n", usb.lastErrorName());
    }
}

void loop()
{
    if (Serial.available() > 0)
    {
        char command = Serial.read();
        if (command == 'h')
        {
            Serial.printf("SERIAL_TX %u\n", CdcSerial.write(reinterpret_cast<const uint8_t *>("host to serial\n"), 15) == 15 ? 1 : 0);
        }
        else if (command == 'd')
        {
            Serial.printf("SERIAL_DTR %u\n", CdcSerial.setDtr(true) ? 1 : 0);
        }
        else if (command == 'D')
        {
            Serial.printf("SERIAL_DTR_OFF %u\n", CdcSerial.setDtr(false) ? 1 : 0);
        }
        else if (command == 'r')
        {
            Serial.printf("SERIAL_RTS %u\n", CdcSerial.setRts(true) ? 1 : 0);
        }
        else if (command == 'R')
        {
            Serial.printf("SERIAL_RTS_OFF %u\n", CdcSerial.setRts(false) ? 1 : 0);
        }
    }
    if (CdcSerial.available() > 0)
    {
        Serial.print("SERIAL_RX ");
        while (CdcSerial.available() > 0)
        {
            Serial.write(CdcSerial.read());
        }
        Serial.println();
    }
    delay(1);
}
