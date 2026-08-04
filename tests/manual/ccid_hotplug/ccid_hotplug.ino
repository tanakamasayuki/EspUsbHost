#include "EspUsbHost.h"

// Verifies the CCID slot-change notifications delivered over the reader's
// interrupt IN endpoint: lift the card off the reader, then put it back.

EspUsbHost usb;

static constexpr uint32_t TEST_TIMEOUT_MS = 60000;
static constexpr uint32_t SETTLE_MS = 1500;

static bool opened = false;
static bool finished = false;
static bool sawRemoval = false;
static bool sawInsertion = false;
static uint32_t lastDeviceEventMs = 0;

void setup()
{
    Serial.begin(115200);
    delay(5000);

    usb.onDeviceConnected([](const EspUsbHostDeviceInfo &device)
                          { lastDeviceEventMs = millis();
                            Serial.printf("connected address=%u vid=%04x pid=%04x product=\"%s\"\n",
                                          device.address, device.vid, device.pid, device.product); });

    usb.onCcidCardRemoved([](const EspUsbHostCcidSlotEvent &event)
                          {
        Serial.printf("CCID_REMOVED address=%u slot=%u\n", event.address, event.slot);
        sawRemoval = true; });

    usb.onCcidCardInserted([](const EspUsbHostCcidSlotEvent &event)
                           {
        Serial.printf("CCID_INSERTED address=%u slot=%u\n", event.address, event.slot);
        // Only count an insertion that follows the removal, so the state the
        // reader reports right after opening cannot pass the test by itself.
        if (sawRemoval)
        {
            sawInsertion = true;
        } });

    usb.begin();
    lastDeviceEventMs = millis();
    Serial.println("ccid_hotplug test start");
}

void loop()
{
    static uint32_t startedAt = millis();

    if (!opened && millis() - lastDeviceEventMs >= SETTLE_MS && millis() - startedAt >= 3000)
    {
        opened = true;
        if (!usb.ccidOpen())
        {
            finished = true;
            Serial.println("CCID_OPEN failed");
            Serial.println("[FAIL]");
            return;
        }
        EspUsbHostCcidInterface info;
        usb.ccidGetInterface(info);
        Serial.printf("CCID_INTERFACE address=%u iface=%u interrupt=0x%02x slots=%u\n",
                      info.address,
                      info.interfaceNumber,
                      info.interruptEndpoint,
                      info.slotCount);
        if (info.interruptEndpoint == 0)
        {
            finished = true;
            Serial.println("CCID_INTERRUPT endpoint missing");
            Serial.println("[FAIL]");
            Serial.println("This reader reports no interrupt IN endpoint, so it sends no slot-change notifications.");
            return;
        }
        Serial.println("Remove the card from the reader, then put it back.");
    }

    if (!finished && sawRemoval && sawInsertion)
    {
        finished = true;
        Serial.println("[PASS]");
    }

    if (!finished && millis() - startedAt > TEST_TIMEOUT_MS)
    {
        finished = true;
        Serial.printf("CCID_EVENTS removed=%u inserted=%u\n", sawRemoval ? 1 : 0, sawInsertion ? 1 : 0);
        Serial.println("[FAIL]");
        Serial.println("No card removal and re-insertion was reported within the timeout.");
    }

    delay(10);
}
