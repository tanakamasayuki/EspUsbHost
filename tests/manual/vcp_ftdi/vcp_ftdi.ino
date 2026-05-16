#include "EspUsbHost.h"

EspUsbHost usb;
EspUsbHostCdcSerial CdcSerial(usb);

static constexpr uint16_t EXPECTED_VID = 0x0403;

static constexpr int UART_SNIFF_RX_PIN = 4;
static constexpr uint32_t TEST_BAUDS[] = {9600, 38400, 115200, 460800};
static constexpr size_t TEST_BAUD_COUNT = sizeof(TEST_BAUDS) / sizeof(TEST_BAUDS[0]);

static constexpr size_t MAX_PATTERN_LEN = 500;
static uint8_t ASCII_DATA[] = "EspUsbHostVCPLoopback";
static constexpr size_t ASCII_LEN = sizeof("EspUsbHostVCPLoopback") - 1;
static uint8_t ALLBYTES_DATA[256];
static uint8_t LONG_DATA[MAX_PATTERN_LEN];

struct Pattern
{
    const char *name;
    const uint8_t *data;
    size_t len;
};

static Pattern PATTERNS[] = {
    {"ascii", ASCII_DATA, ASCII_LEN},
    {"allbytes", ALLBYTES_DATA, sizeof(ALLBYTES_DATA)},
    {"long", LONG_DATA, sizeof(LONG_DATA)},
};
static constexpr size_t PATTERN_COUNT = sizeof(PATTERNS) / sizeof(PATTERNS[0]);

static volatile bool expectedDeviceConnected = false;
static volatile bool unexpectedDeviceConnected = false;
static uint16_t connectedVid = 0;
static uint16_t connectedPid = 0;

static void initPatterns()
{
    for (size_t i = 0; i < sizeof(ALLBYTES_DATA); i++)
    {
        ALLBYTES_DATA[i] = static_cast<uint8_t>(i);
    }
    for (size_t i = 0; i < sizeof(LONG_DATA); i++)
    {
        LONG_DATA[i] = static_cast<uint8_t>((i * 37u + 13u) & 0xff);
    }
}

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

static bool runPattern(uint32_t baud, const Pattern &pattern)
{
    static uint8_t usbBuf[MAX_PATTERN_LEN];
    static uint8_t sniffBuf[MAX_PATTERN_LEN];

    drainUsb();
    drainSniff();

    CdcSerial.write(pattern.data, pattern.len);

    size_t usbReceived = 0;
    size_t sniffReceived = 0;
    const uint32_t timeoutMs = 2000 + (pattern.len * 12 * 1000 / baud);
    const uint32_t deadline = millis() + timeoutMs;
    while ((usbReceived < pattern.len || sniffReceived < pattern.len) && millis() < deadline)
    {
        while (CdcSerial.available() && usbReceived < pattern.len)
        {
            usbBuf[usbReceived++] = CdcSerial.read();
        }
        while (Serial1.available() && sniffReceived < pattern.len)
        {
            sniffBuf[sniffReceived++] = Serial1.read();
        }
    }

    const bool usbOk = (usbReceived == pattern.len) && (memcmp(usbBuf, pattern.data, pattern.len) == 0);
    const bool sniffOk = (sniffReceived == pattern.len) && (memcmp(sniffBuf, pattern.data, pattern.len) == 0);

    if (usbOk && sniffOk)
    {
        Serial.printf("BAUD=%u PATTERN=%s OK\n", (unsigned)baud, pattern.name);
        return true;
    }
    Serial.printf("BAUD=%u PATTERN=%s FAIL usb=%u/%u sniff=%u/%u\n",
                  (unsigned)baud, pattern.name,
                  (unsigned)usbReceived, (unsigned)pattern.len,
                  (unsigned)sniffReceived, (unsigned)pattern.len);
    return false;
}

static bool runOneBaud(uint32_t baud)
{
    if (!CdcSerial.setBaudRate(baud))
    {
        Serial.printf("BAUD=%u FAIL setBaudRate\n", (unsigned)baud);
        return false;
    }
    delay(200);
    Serial1.end();
    Serial1.begin(baud, SERIAL_8N1, UART_SNIFF_RX_PIN, -1);
    delay(100);

    bool allOk = true;
    for (size_t i = 0; i < PATTERN_COUNT; i++)
    {
        if (!runPattern(baud, PATTERNS[i]))
        {
            allOk = false;
        }
    }
    return allOk;
}

void setup()
{
    Serial.begin(115200);
    delay(5000);
    initPatterns();
    Serial1.begin(115200, SERIAL_8N1, UART_SNIFF_RX_PIN, -1);
    CdcSerial.begin(115200);
    usb.onDeviceConnected([](const EspUsbHostDeviceInfo &device)
                          {
        if (device.vid == EXPECTED_VID)
        {
            connectedVid = device.vid;
            connectedPid = device.pid;
            expectedDeviceConnected = true;
        } });
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
