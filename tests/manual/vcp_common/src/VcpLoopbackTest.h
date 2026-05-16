#pragma once

#include "EspUsbHost.h"

class VcpLoopbackTest
{
public:
    explicit VcpLoopbackTest(uint16_t expectedVid, uint16_t expectedPid = 0);

    void setup();
    void loop();

private:
    static constexpr int UART_SNIFF_RX_PIN = 4;
    static constexpr size_t ALLBYTES_LEN = 256;
    static constexpr size_t LONG_LEN = 500;
    static constexpr size_t MAX_PATTERN_LEN = LONG_LEN;

    struct Pattern
    {
        const char *name;
        const uint8_t *data;
        size_t len;
    };

    void initPatterns();
    void drainUsb();
    void drainSniff();
    bool runPattern(uint32_t baud, const Pattern &pattern);
    bool runOneBaud(uint32_t baud);
    bool deviceMatches(const EspUsbHostDeviceInfo &device) const;

    EspUsbHost usb_;
    EspUsbHostCdcSerial cdc_;
    uint16_t expectedVid_;
    uint16_t expectedPid_;

    volatile bool expectedConnected_ = false;
    volatile bool unexpectedConnected_ = false;
    uint16_t connectedVid_ = 0;
    uint16_t connectedPid_ = 0;

    uint8_t allBytesData_[ALLBYTES_LEN];
    uint8_t longData_[LONG_LEN];
    Pattern patterns_[3];

    uint8_t usbBuf_[MAX_PATTERN_LEN];
    uint8_t sniffBuf_[MAX_PATTERN_LEN];
};
