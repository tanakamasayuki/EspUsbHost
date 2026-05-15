#include "EspUsbHost.h"

EspUsbHost usb;
EspUsbHostCdcSerial TargetSerial(usb);

static constexpr uint32_t TARGET_BAUD = 115200;
static constexpr uint32_t CONNECT_TIMEOUT_MS = 10000;
static constexpr uint32_t BOOT_LOG_TIMEOUT_MS = 3000;

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

    const bool normalOk = normalReset();
    Serial.printf("normal reset: %s\n", normalOk ? "OK" : "FAIL");

    const bool downloadOk = downloadModeReset();
    Serial.printf("download mode: %s\n", downloadOk ? "OK" : "FAIL");

    Serial.println(normalOk && downloadOk ? "[PASS]" : "[FAIL]");
    delay(5000);
}
