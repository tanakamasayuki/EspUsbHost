#include "EspUsbHost.h"

EspUsbHost usb;

static const int TARGET_CYCLES = 5;
static int connectCount = 0;
static int disconnectCount = 0;
static bool passed = false;

void setup()
{
    Serial.begin(115200);
    delay(5000);

    usb.onDeviceConnected([](const EspUsbHostDeviceInfo &device)
                          {
        connectCount++;
        Serial.printf("Connected %d/%d\n", connectCount, TARGET_CYCLES); });

    usb.onDeviceDisconnected([](const EspUsbHostDeviceInfo &device)
                             {
        disconnectCount++;
        Serial.printf("Disconnected %d/%d\n", disconnectCount, TARGET_CYCLES);
        if (disconnectCount >= TARGET_CYCLES && !passed)
        {
            passed = true;
            Serial.println("[PASS]");
        } });

    usb.begin();
    Serial.printf("Plug and unplug a USB device %d times...\n", TARGET_CYCLES);
}

void loop()
{
    delay(1);
}
