#include "EspUsbHost.h"

EspUsbHost usb;
EspUsbHostCdcSerial CdcSerial(usb);

static const char TEST_DATA[] = "EspUsbHostVCPLoopback";
static const size_t TEST_LEN = sizeof(TEST_DATA) - 1;
static constexpr uint16_t EXPECTED_VID = 0x0403;

static volatile bool expectedDeviceConnected = false;
static volatile bool unexpectedDeviceConnected = false;
static uint16_t connectedVid = 0;
static uint16_t connectedPid = 0;

void setup()
{
    Serial.begin(115200);
    delay(5000);
    CdcSerial.begin(115200);
    usb.onDeviceConnected([](const EspUsbHostDeviceInfo &device)
                          {
        connectedVid = device.vid;
        connectedPid = device.pid;
        expectedDeviceConnected = device.vid == EXPECTED_VID;
        unexpectedDeviceConnected = device.vid != EXPECTED_VID; });
    usb.begin();
}

void loop()
{
    if (unexpectedDeviceConnected)
    {
        Serial.println("[FAIL]");
        Serial.printf("unexpected vid=%04x pid=%04x\n", connectedVid, connectedPid);
        delay(5000);
        return;
    }

    if (!expectedDeviceConnected)
    {
        delay(100);
        return;
    }

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
        Serial.println("[FAIL]");
        Serial.printf("received=%u\n", (unsigned)received);
    }

    delay(5000);
}
