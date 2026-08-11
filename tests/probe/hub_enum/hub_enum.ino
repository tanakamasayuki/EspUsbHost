#include "EspUsbHost.h"

// Probe for the crash a CH335F hub (1a86:8094) produces with an ALIENTEK DP100
// behind it. Each part alone is fine - the hub with other devices, the DP100 on
// any other hub or connected directly - so the question is what the combination
// needs, and whether this library holding the hub open is part of it.
//
// The run has two halves, and starts in the safe one:
//
//   phase 1  hub tracking OFF. The library never opens the external hub, so
//            ESP-IDF's hub driver owns it alone. If this is stable and phase 2 is
//            not, the client handle is a necessary ingredient.
//   phase 2  hub tracking ON, the shipped default. This is the configuration that
//            crashed: the scan opens the hub and keeps the handle, and roughly a
//            second later the hub driver fails to submit its status-change
//            transfer, declares the hub broken and asserts in its release path.
//
// Starting with tracking off matters for more than tidiness: a board that boots
// straight into a crash loop can be hard to flash again, and this sketch always
// comes up in the harmless phase.
//
// No hub descriptor or port status is read either: that is EP0 traffic on a device
// the hub driver owns, which is part of what is under suspicion.
//
// Nothing is asserted: the log is the output.

EspUsbHost usb;

static constexpr uint32_t PHASE1_MS = 12000; // hub tracking off
static constexpr uint32_t PHASE2_MS = 20000; // hub tracking on
static constexpr uint32_t REPORT_INTERVAL_MS = 3000;

static const char *speedName(usb_speed_t speed)
{
    switch (speed)
    {
    case USB_SPEED_LOW:
        return "low";
    case USB_SPEED_FULL:
        return "full";
    case USB_SPEED_HIGH:
        return "high";
    default:
        return "unknown";
    }
}

static void printDevices(const char *phase)
{
    EspUsbHostDeviceInfo devices[ESP_USB_HOST_MAX_DEVICES];
    const size_t count = usb.getDevices(devices, ESP_USB_HOST_MAX_DEVICES);

    // The host stack's own address list, which shows devices it has enumerated even
    // when this library is not tracking them.
    uint8_t addresses[ESP_USB_HOST_MAX_DEVICES] = {};
    const size_t addressCount = usb.getHostDeviceAddresses(addresses, sizeof(addresses));

    Serial.printf("[%s] tracked=%u hub_tracking=%u host_addresses=%u:",
                  phase,
                  static_cast<unsigned>(count),
                  usb.hubTrackingEnabled() ? 1 : 0,
                  static_cast<unsigned>(addressCount));
    for (size_t i = 0; i < addressCount; i++)
    {
        Serial.printf(" %u", addresses[i]);
    }
    Serial.println();

    for (size_t i = 0; i < count; i++)
    {
        const EspUsbHostDeviceInfo &device = devices[i];
        Serial.printf("  device address=%u vid=%04x pid=%04x class=0x%02x parent=%u portId=0x%02x speed=%s hub=%u product=\"%s\"\n",
                      device.address,
                      device.vid,
                      device.pid,
                      device.deviceClass,
                      device.parentAddress,
                      device.portId,
                      speedName(device.speed),
                      device.isHub ? 1 : 0,
                      device.product);

        EspUsbHostInterfaceInfo interfaces[ESP_USB_HOST_MAX_INTERFACES];
        const size_t interfaceCount = usb.getInterfaces(device.address, interfaces, ESP_USB_HOST_MAX_INTERFACES);
        for (size_t n = 0; n < interfaceCount; n++)
        {
            Serial.printf("    interface %u class=0x%02x subclass=0x%02x protocol=0x%02x claimed=%u\n",
                          interfaces[n].number,
                          interfaces[n].interfaceClass,
                          interfaces[n].interfaceSubClass,
                          interfaces[n].interfaceProtocol,
                          interfaces[n].claimed ? 1 : 0);
        }
    }
}

void setup()
{
    Serial.begin(115200);
    delay(5000);

    // The Arduino core's DebugLevel only moves its own tag; the USB host
    // component's own messages need the runtime level raised too.
    esp_log_level_set("*", ESP_LOG_VERBOSE);

    usb.onDeviceConnected([](const EspUsbHostDeviceInfo &device)
                          { Serial.printf("CONNECTED address=%u vid=%04x pid=%04x parent=%u portId=0x%02x speed=%s product=\"%s\"\n",
                                          device.address,
                                          device.vid,
                                          device.pid,
                                          device.parentAddress,
                                          device.portId,
                                          speedName(device.speed),
                                          device.product); });

    usb.onDeviceDisconnected([](const EspUsbHostDeviceInfo &device)
                             { Serial.printf("DISCONNECTED address=%u vid=%04x pid=%04x\n",
                                             device.address, device.vid, device.pid); });

    // Phase 1: leave external hubs alone. Set before begin() so the very first scan
    // never opens one.
    usb.setHubTrackingEnabled(false);
    usb.begin();
    Serial.println("TEST_BEGIN hub_enum_probe");
    Serial.println("PHASE 1 hub_tracking=0");
}

void loop()
{
    static uint32_t startedAt = millis();
    static uint32_t lastReportMs = 0;
    static bool phase2 = false;
    static bool done = false;

    const uint32_t elapsed = millis() - startedAt;

    if (!phase2 && elapsed > PHASE1_MS)
    {
        phase2 = true;
        Serial.println("PHASE 1 END");
        Serial.println("PHASE 2 hub_tracking=1 (the configuration that crashed)");
        usb.setHubTrackingEnabled(true);
    }

    if (millis() - lastReportMs >= REPORT_INTERVAL_MS)
    {
        lastReportMs = millis();
        printDevices(phase2 ? "phase2" : "phase1");
    }

    if (!done && elapsed > PHASE1_MS + PHASE2_MS)
    {
        done = true;
        Serial.println("PROBE_DONE");
    }

    delay(10);
}
