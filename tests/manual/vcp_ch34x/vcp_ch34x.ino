#include "EspUsbHost.h"

EspUsbHost usb;
EspUsbHostCdcSerial CdcSerial(usb);

static const char TEST_DATA[] = "EspUsbHostVCPLoopback";
static const size_t TEST_LEN = sizeof(TEST_DATA) - 1;
static constexpr uint16_t EXPECTED_VID = 0x1a86;

static constexpr int UART_SNIFF_RX_PIN = 4;
static constexpr uint32_t TEST_BAUDS[] = {9600, 38400, 115200, 460800};
static constexpr size_t TEST_BAUD_COUNT = sizeof(TEST_BAUDS) / sizeof(TEST_BAUDS[0]);

static volatile bool expectedDeviceConnected = false;
static volatile bool unexpectedDeviceConnected = false;
static uint16_t connectedVid = 0;
static uint16_t connectedPid = 0;

static void drainUsb()
{
    while (CdcSerial.available())
    {
        CdcSerial.read();
    }
}

static void drainSniff()
{
    while (Serial1.available())
    {
        Serial1.read();
    }
}

static bool runOneBaud(uint32_t baud)
{
    if (!CdcSerial.setBaudRate(baud))
    {
        Serial.printf("BAUD=%u FAIL setBaudRate\n", (unsigned)baud);
        return false;
    }
    delay(500);
    Serial1.end();
    Serial1.begin(baud, SERIAL_8N1, UART_SNIFF_RX_PIN, -1);
    delay(200);
    drainUsb();
    drainSniff();

    CdcSerial.write((const uint8_t *)TEST_DATA, TEST_LEN);

    char usbBuf[TEST_LEN];
    char sniffBuf[TEST_LEN];
    size_t usbReceived = 0;
    size_t sniffReceived = 0;
    uint32_t deadline = millis() + 2000;
    while ((usbReceived < TEST_LEN || sniffReceived < TEST_LEN) && millis() < deadline)
    {
        if (CdcSerial.available() && usbReceived < TEST_LEN)
        {
            usbBuf[usbReceived++] = CdcSerial.read();
        }
        if (Serial1.available() && sniffReceived < TEST_LEN)
        {
            sniffBuf[sniffReceived++] = Serial1.read();
        }
    }

    const bool usbOk = (usbReceived == TEST_LEN) && (memcmp(usbBuf, TEST_DATA, TEST_LEN) == 0);
    const bool sniffOk = (sniffReceived == TEST_LEN) && (memcmp(sniffBuf, TEST_DATA, TEST_LEN) == 0);

    if (usbOk && sniffOk)
    {
        Serial.printf("BAUD=%u OK\n", (unsigned)baud);
        return true;
    }
    Serial.printf("BAUD=%u FAIL usb=%u/%u sniff=%u/%u\n",
                  (unsigned)baud,
                  (unsigned)usbReceived, (unsigned)TEST_LEN,
                  (unsigned)sniffReceived, (unsigned)TEST_LEN);
    return false;
}

void setup()
{
    Serial.begin(115200);
    delay(5000);
    Serial1.begin(115200, SERIAL_8N1, UART_SNIFF_RX_PIN, -1);
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

    bool allOk = true;
    for (size_t i = 0; i < TEST_BAUD_COUNT; i++)
    {
        if (!runOneBaud(TEST_BAUDS[i]))
        {
            allOk = false;
        }
    }

    Serial.println(allOk ? "[PASS]" : "[FAIL]");
    delay(5000);
}
