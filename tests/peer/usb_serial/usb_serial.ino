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
        else if (command == 'c')
        {
            EspUsbHostSerialConfig config;
            config.baud = 57600;
            config.dataBits = 7;
            config.parity = ESP_USB_HOST_SERIAL_PARITY_EVEN;
            config.stopBits = ESP_USB_HOST_SERIAL_STOP_BITS_2;
            Serial.printf("SERIAL_CONFIG %u\n", CdcSerial.setConfig(config) ? 1 : 0);
        }
        else if (command == 'b')
        {
            Serial.printf("SERIAL_BAUD %u\n", CdcSerial.setBaudRate(115200) ? 1 : 0);
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
