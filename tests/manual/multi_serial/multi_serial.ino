#include "EspUsbHost.h"

EspUsbHost usb;
EspUsbHostCdcSerial CdcSerial1(usb);
EspUsbHostCdcSerial CdcSerial2(usb);

static const char TEST_DATA[] = "MultiSerialLoopback";
static const size_t TEST_LEN = sizeof(TEST_DATA) - 1;

static uint8_t connectedAddresses[2];
static uint8_t connectedCount = 0;
static bool assigned = false;
static bool tested = false;

static bool loopbackTest(EspUsbHostCdcSerial &serial, uint8_t idx)
{
    while (serial.available())
    {
        serial.read();
    }
    delay(50);

    serial.write((const uint8_t *)TEST_DATA, TEST_LEN);

    char buf[TEST_LEN];
    size_t received = 0;
    uint32_t deadline = millis() + 3000;
    while (received < TEST_LEN && millis() < deadline)
    {
        if (serial.available())
        {
            buf[received++] = serial.read();
        }
    }
    bool ok = received == TEST_LEN && memcmp(buf, TEST_DATA, TEST_LEN) == 0;
    Serial.printf("Serial%u: %s\n", (unsigned)idx, ok ? "OK" : "FAIL");
    if (!ok)
    {
        Serial.printf("Serial%u received=%u data=", (unsigned)idx, (unsigned)received);
        for (size_t i = 0; i < received; i++)
        {
            Serial.printf("%02x", static_cast<uint8_t>(buf[i]));
        }
        Serial.println();
    }
    return ok;
}

void setup()
{
    Serial.begin(115200);
    delay(5000);

    usb.onDeviceConnected([](const EspUsbHostDeviceInfo &device)
                          {
        if (connectedCount < 2)
        {
            connectedAddresses[connectedCount++] = device.address;
            Serial.printf("Device %u connected: address=%u vid=%04x\n",
                          (unsigned)connectedCount, device.address, device.vid);
            if (connectedCount == 2)
            {
                CdcSerial1.setAddress(connectedAddresses[0]);
                CdcSerial2.setAddress(connectedAddresses[1]);
                assigned = true;
                Serial.println("Both devices assigned");
            }
        } });

    CdcSerial1.begin(115200);
    CdcSerial2.begin(115200);
    usb.begin();
}

void loop()
{
    if (!assigned)
    {
        delay(100);
        return;
    }

    if (tested)
    {
        delay(1000);
        return;
    }
    tested = true;

    if (!CdcSerial1.connected() || !CdcSerial2.connected())
    {
        Serial.printf("[FAIL] serial not ready serial1=%u serial2=%u\n",
                      CdcSerial1.connected() ? 1 : 0,
                      CdcSerial2.connected() ? 1 : 0);
        return;
    }
    Serial.println("Both serial ports ready");

    bool ok1 = loopbackTest(CdcSerial1, 1);
    bool ok2 = loopbackTest(CdcSerial2, 2);

    if (ok1 && ok2)
    {
        Serial.println("[PASS]");
    }
    else
    {
        Serial.println("[FAIL]");
    }

    delay(5000);
}
