#include "EspUsbHost.h"

EspUsbHost usb;
EspUsbHostCdcSerial CdcSerial(usb);

static const char TEST_DATA[] = "EspUsbHostVCPLoopback";
static const size_t TEST_LEN = sizeof(TEST_DATA) - 1;

void setup()
{
    Serial.begin(115200);
    delay(5000);
    CdcSerial.begin(115200);
    usb.begin();
}

void loop()
{
    if (!CdcSerial.connected())
    {
        delay(100);
        return;
    }

    CdcSerial.write((const uint8_t *)TEST_DATA, TEST_LEN);

    char buf[TEST_LEN];
    size_t received = 0;
    uint32_t deadline = millis() + 1000;
    while (received < TEST_LEN && millis() < deadline)
    {
        if (CdcSerial.available())
        {
            buf[received++] = CdcSerial.read();
        }
    }

    if (received == TEST_LEN && memcmp(buf, TEST_DATA, TEST_LEN) == 0)
    {
        Serial.println("[PASS]");
    }
    else
    {
        Serial.printf("[FAIL] received=%u\n", (unsigned)received);
    }

    delay(5000);
}
