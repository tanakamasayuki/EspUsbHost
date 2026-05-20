#include "VcpLoopbackTest.h"

#include <Arduino.h>
#include <string.h>

namespace
{
    struct TestConfig
    {
        const char *name;
        EspUsbHostSerialConfig config;
    };
    const TestConfig TEST_CONFIGS[] = {
        {"9600-8N1", {9600, 8, ESP_USB_HOST_SERIAL_PARITY_NONE, ESP_USB_HOST_SERIAL_STOP_BITS_1}},
        {"38400-8N1", {38400, 8, ESP_USB_HOST_SERIAL_PARITY_NONE, ESP_USB_HOST_SERIAL_STOP_BITS_1}},
        {"115200-8N1", {115200, 8, ESP_USB_HOST_SERIAL_PARITY_NONE, ESP_USB_HOST_SERIAL_STOP_BITS_1}},
        {"460800-8N1", {460800, 8, ESP_USB_HOST_SERIAL_PARITY_NONE, ESP_USB_HOST_SERIAL_STOP_BITS_1}},
        {"57600-7E1", {57600, 7, ESP_USB_HOST_SERIAL_PARITY_EVEN, ESP_USB_HOST_SERIAL_STOP_BITS_1}},
        {"57600-7O2", {57600, 7, ESP_USB_HOST_SERIAL_PARITY_ODD, ESP_USB_HOST_SERIAL_STOP_BITS_2}},
    };
    constexpr size_t TEST_CONFIG_COUNT = sizeof(TEST_CONFIGS) / sizeof(TEST_CONFIGS[0]);
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

uint32_t VcpLoopbackTest::arduinoSerialConfig(const EspUsbHostSerialConfig &config) const
{
    if (config.dataBits == 7 && config.parity == ESP_USB_HOST_SERIAL_PARITY_EVEN && config.stopBits == ESP_USB_HOST_SERIAL_STOP_BITS_1)
    {
        return SERIAL_7E1;
    }
    if (config.dataBits == 7 && config.parity == ESP_USB_HOST_SERIAL_PARITY_ODD && config.stopBits == ESP_USB_HOST_SERIAL_STOP_BITS_2)
    {
        return SERIAL_7O2;
    }
    return SERIAL_8N1;
}

bool VcpLoopbackTest::runPattern(const EspUsbHostSerialConfig &config, const char *configName, const Pattern &pattern)
{
    drainUsb();
    drainSniff();

    cdc_.write(pattern.data, pattern.len);

    size_t usbReceived = 0;
    size_t sniffReceived = 0;
    const uint32_t timeoutMs = 2000 + (pattern.len * 12u * 1000u / config.baud);
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
        Serial.printf("CONFIG=%s PATTERN=%s OK\n", configName, pattern.name);
        return true;
    }
    Serial.printf("CONFIG=%s PATTERN=%s FAIL usb=%u/%u sniff=%u/%u\n",
                  configName, pattern.name,
                  (unsigned)usbReceived, (unsigned)pattern.len,
                  (unsigned)sniffReceived, (unsigned)pattern.len);
    return false;
}

bool VcpLoopbackTest::runOneConfig(const EspUsbHostSerialConfig &config, const char *configName)
{
    if (!cdc_.setConfig(config))
    {
        Serial.printf("CONFIG=%s FAIL setConfig\n", configName);
        return false;
    }
    delay(200);
    Serial1.end();
    Serial1.setRxBufferSize(2048);
    Serial1.begin(config.baud, arduinoSerialConfig(config), UART_SNIFF_RX_PIN, -1);
    delay(100);

    bool allOk = true;
    for (const Pattern &p : patterns_)
    {
        if ((config.dataBits < 8 && strcmp(p.name, "ascii") != 0))
        {
            continue;
        }
        if (!runPattern(config, configName, p))
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
    for (size_t i = 0; i < TEST_CONFIG_COUNT; i++)
    {
        if (!runOneConfig(TEST_CONFIGS[i].config, TEST_CONFIGS[i].name))
        {
            allOk = false;
        }
    }

    Serial.println(allOk ? "[PASS]" : "[FAIL]");
    delay(5000);
}
