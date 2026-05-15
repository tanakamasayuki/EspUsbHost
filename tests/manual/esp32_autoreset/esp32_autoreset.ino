#include "EspUsbHost.h"

EspUsbHost usb;
EspUsbHostCdcSerial TargetSerial(usb);

static constexpr uint32_t TARGET_BAUD = 115200;
static constexpr uint32_t CONNECT_TIMEOUT_MS = 10000;
static constexpr uint32_t BOOT_LOG_TIMEOUT_MS = 3000;
static constexpr uint32_t UART_SNIFF_TIMEOUT_MS = 15000;

static void drainTargetSerial()
{
    while (TargetSerial.available())
    {
        TargetSerial.read();
    }
}

static bool readUntilAny(const char *needle1, const char *needle2, uint32_t timeoutMs)
{
    char window[96] = {};
    size_t windowLen = 0;
    const uint32_t deadline = millis() + timeoutMs;

    while (millis() < deadline)
    {
        while (TargetSerial.available())
        {
            const int value = TargetSerial.read();
            if (value < 0)
            {
                break;
            }

            const char c = static_cast<char>(value);
            Serial.write(c);

            if (windowLen + 1 >= sizeof(window))
            {
                memmove(window, window + 1, sizeof(window) - 2);
                windowLen = sizeof(window) - 2;
            }
            window[windowLen++] = c;
            window[windowLen] = '\0';

            if ((needle1 && strstr(window, needle1)) ||
                (needle2 && strstr(window, needle2)))
            {
                return true;
            }
        }
        delay(1);
    }
    return false;
}

static bool sniffTargetBootLog(uint32_t timeoutMs)
{
    Serial.println("UART_SNIFF");
    Serial.println("Press the target ESP32 reset button now.");
    char window[96] = {};
    size_t windowLen = 0;
    uint8_t firstBytes[32] = {};
    size_t firstByteCount = 0;
    size_t receivedCount = 0;
    const uint32_t deadline = millis() + timeoutMs;
    while (millis() < deadline)
    {
        while (TargetSerial.available())
        {
            const int value = TargetSerial.read();
            if (value >= 0)
            {
                const uint8_t byteValue = static_cast<uint8_t>(value);
                receivedCount++;
                if (firstByteCount < sizeof(firstBytes))
                {
                    firstBytes[firstByteCount++] = byteValue;
                }

                const char c = static_cast<char>(byteValue);
                Serial.write(c);

                if (windowLen + 1 >= sizeof(window))
                {
                    memmove(window, window + 1, sizeof(window) - 2);
                    windowLen = sizeof(window) - 2;
                }
                window[windowLen++] = c;
                window[windowLen] = '\0';
                if (strstr(window, "rst:") || strstr(window, "boot:"))
                {
                    Serial.println();
                    Serial.printf("uart sniff bytes=%u boot log found\n", (unsigned)receivedCount);
                    return true;
                }
            }
        }
        delay(1);
    }
    Serial.println();
    Serial.printf("uart sniff bytes=%u first=", (unsigned)receivedCount);
    for (size_t i = 0; i < firstByteCount; i++)
    {
        Serial.printf("%02x", firstBytes[i]);
    }
    Serial.println();
    return false;
}

static void setLines(bool dtr, bool rts)
{
    TargetSerial.setDtr(dtr);
    TargetSerial.setRts(rts);
    delay(100);
}

static bool normalReset()
{
    Serial.println("NORMAL_RESET");
    drainTargetSerial();

    Serial.println("lines: DTR=0 RTS=0");
    setLines(false, false);
    Serial.println("lines: DTR=0 RTS=1");
    setLines(false, true);
    delay(100);
    Serial.println("lines: DTR=0 RTS=0");
    setLines(false, false);

    return readUntilAny("rst:", "boot:", BOOT_LOG_TIMEOUT_MS);
}

static bool downloadModeReset()
{
    Serial.println("DOWNLOAD_RESET");
    drainTargetSerial();

    Serial.println("lines: DTR=0 RTS=1");
    setLines(false, true);
    delay(100);
    Serial.println("lines: DTR=1 RTS=0");
    setLines(true, false);
    delay(100);

    const bool ok = readUntilAny("download", "DOWNLOAD", BOOT_LOG_TIMEOUT_MS);
    Serial.println("lines: DTR=0 RTS=0");
    setLines(false, false);
    return ok;
}

void setup()
{
    Serial.begin(115200);
    delay(5000);
    TargetSerial.begin(TARGET_BAUD);
    usb.onDeviceConnected([](const EspUsbHostDeviceInfo &device)
                          {
        Serial.printf("connected: address=%u vid=%04x pid=%04x\n",
                      device.address, device.vid, device.pid); });
    usb.begin();
    Serial.println("ESP32 autoreset test start");
    Serial.println("Connect a USB-serial adapter to a target ESP32 UART0 with TX/RX/DTR/RTS auto-reset wiring.");
}

void loop()
{
    const uint32_t connectDeadline = millis() + CONNECT_TIMEOUT_MS;
    while (!TargetSerial.connected() && millis() < connectDeadline)
    {
        delay(100);
    }

    if (!TargetSerial.connected())
    {
        Serial.println("[FAIL]");
        Serial.println("USB serial adapter not ready");
        delay(5000);
        return;
    }

    Serial.println("USB serial adapter ready");
    delay(500);

    drainTargetSerial();
    const bool sniffOk = sniffTargetBootLog(UART_SNIFF_TIMEOUT_MS);
    Serial.printf("manual reset log: %s\n", sniffOk ? "OK" : "FAIL");
    if (!sniffOk)
    {
        Serial.println("No target ESP32 boot log detected during manual sniff; continuing with DTR/RTS reset tests.");
    }

    const bool normalOk = normalReset();
    Serial.printf("normal reset: %s\n", normalOk ? "OK" : "FAIL");

    const bool downloadOk = downloadModeReset();
    Serial.printf("download mode: %s\n", downloadOk ? "OK" : "FAIL");

    Serial.println(normalOk && downloadOk ? "[PASS]" : "[FAIL]");
    delay(5000);
}
