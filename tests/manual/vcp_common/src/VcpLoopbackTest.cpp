#include "VcpLoopbackTest.h"

#include <Arduino.h>
#include <string.h>

namespace
{
    constexpr uint32_t TEST_BAUDS[] = {9600, 38400, 115200, 460800};
    constexpr size_t TEST_BAUD_COUNT = sizeof(TEST_BAUDS) / sizeof(TEST_BAUDS[0]);
    const uint8_t ASCII_DATA[] = "EspUsbHostVCPLoopback";
    constexpr size_t ASCII_LEN = sizeof(ASCII_DATA) - 1;
}

VcpLoopbackTest::VcpLoopbackTest(uint16_t expectedVid, uint16_t expectedPid)
    : cdc_(usb_), expectedVid_(expectedVid), expectedPid_(expectedPid)
{
    patterns_[0] = {"ascii", ASCII_DATA, ASCII_LEN};
    patterns_[1] = {"allbytes", allBytesData_, ALLBYTES_LEN};
    patterns_[2] = {"long", longData_, LONG_LEN};
}

void VcpLoopbackTest::initPatterns()
{
    for (size_t i = 0; i < ALLBYTES_LEN; i++)
    {
        allBytesData_[i] = static_cast<uint8_t>(i);
    }
    for (size_t i = 0; i < LONG_LEN; i++)
    {
        longData_[i] = static_cast<uint8_t>((i * 37u + 13u) & 0xff);
    }
}

void VcpLoopbackTest::drainUsb()
{
    while (cdc_.available())
    {
        cdc_.read();
    }
}

void VcpLoopbackTest::drainSniff()
{
    while (Serial1.available())
    {
        Serial1.read();
    }
}

bool VcpLoopbackTest::deviceMatches(const EspUsbHostDeviceInfo &device) const
{
    if (device.vid != expectedVid_)
    {
        return false;
    }
    if (expectedPid_ != 0 && device.pid != expectedPid_)
    {
        return false;
    }
    return true;
}

bool VcpLoopbackTest::runPattern(uint32_t baud, const Pattern &pattern)
{
    drainUsb();
    drainSniff();

    cdc_.write(pattern.data, pattern.len);

    size_t usbReceived = 0;
    size_t sniffReceived = 0;
    const uint32_t timeoutMs = 2000 + (pattern.len * 12u * 1000u / baud);
    const uint32_t deadline = millis() + timeoutMs;
    while ((usbReceived < pattern.len || sniffReceived < pattern.len) && millis() < deadline)
    {
        while (cdc_.available() && usbReceived < pattern.len)
        {
            usbBuf_[usbReceived++] = cdc_.read();
        }
        while (Serial1.available() && sniffReceived < pattern.len)
        {
            sniffBuf_[sniffReceived++] = Serial1.read();
        }
    }

    const bool usbOk = (usbReceived == pattern.len) && (memcmp(usbBuf_, pattern.data, pattern.len) == 0);
    const bool sniffOk = (sniffReceived == pattern.len) && (memcmp(sniffBuf_, pattern.data, pattern.len) == 0);

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

bool VcpLoopbackTest::runOneBaud(uint32_t baud)
{
    if (!cdc_.setBaudRate(baud))
    {
        Serial.printf("BAUD=%u FAIL setBaudRate\n", (unsigned)baud);
        return false;
    }
    delay(200);
    Serial1.end();
    Serial1.setRxBufferSize(2048);
    Serial1.begin(baud, SERIAL_8N1, UART_SNIFF_RX_PIN, -1);
    delay(100);

    bool allOk = true;
    for (const Pattern &p : patterns_)
    {
        if (!runPattern(baud, p))
        {
            allOk = false;
        }
    }
    return allOk;
}

void VcpLoopbackTest::setup()
{
    Serial.begin(115200);
    delay(5000);
    initPatterns();
    Serial1.setRxBufferSize(2048);
    Serial1.begin(115200, SERIAL_8N1, UART_SNIFF_RX_PIN, -1);
    cdc_.begin(115200);
    usb_.onDeviceConnected([this](const EspUsbHostDeviceInfo &device)
                           {
        if (deviceMatches(device))
        {
            connectedVid_ = device.vid;
            connectedPid_ = device.pid;
            expectedConnected_ = true;
        } });
    usb_.begin();
}

void VcpLoopbackTest::loop()
{
    if (unexpectedConnected_)
    {
        Serial.println("[FAIL]");
        Serial.printf("unexpected vid=%04x pid=%04x\n", connectedVid_, connectedPid_);
        delay(5000);
        return;
    }

    if (!expectedConnected_)
    {
        delay(100);
        return;
    }

    if (!cdc_.connected())
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
