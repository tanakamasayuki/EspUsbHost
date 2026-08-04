#include "EspUsbHost.h"

// CCID probe. Dumps every interface and endpoint of the connected devices so
// that a CCID (bInterfaceClass == 0x0b) interface and its bulk OUT / bulk IN /
// interrupt IN endpoints can be identified before the CCID API is implemented.

EspUsbHost usb;

static constexpr uint8_t USB_CLASS_CCID = 0x0b;
static constexpr uint32_t TEST_TIMEOUT_MS = 20000;
static constexpr uint32_t SETTLE_MS = 1500;

static bool passed = false;
static uint32_t lastDeviceEventMs = 0;

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

static const char *endpointTypeName(uint8_t attributes)
{
    switch (attributes & 0x03)
    {
    case 0:
        return "control";
    case 1:
        return "isochronous";
    case 2:
        return "bulk";
    case 3:
        return "interrupt";
    default:
        return "unknown";
    }
}

static bool dumpDevice(const EspUsbHostDeviceInfo &device)
{
    Serial.printf("DEVICE address=%u vid=%04x pid=%04x class=0x%02x subclass=0x%02x protocol=0x%02x speed=%s interfaces=%u manufacturer=\"%s\" product=\"%s\" serial=\"%s\"\n",
                  device.address,
                  device.vid,
                  device.pid,
                  device.deviceClass,
                  device.deviceSubClass,
                  device.deviceProtocol,
                  speedName(device.speed),
                  device.configurationInterfaceCount,
                  device.manufacturer,
                  device.product,
                  device.serial);

    bool ccidFound = false;

    EspUsbHostInterfaceInfo interfaces[ESP_USB_HOST_MAX_INTERFACES];
    const size_t interfaceCount = usb.getInterfaces(device.address, interfaces, ESP_USB_HOST_MAX_INTERFACES);
    for (size_t i = 0; i < interfaceCount; i++)
    {
        const EspUsbHostInterfaceInfo &interface = interfaces[i];
        const bool isCcid = interface.interfaceClass == USB_CLASS_CCID;
        Serial.printf("  INTERFACE number=%u alt=%u class=0x%02x subclass=0x%02x protocol=0x%02x endpoints=%u%s\n",
                      interface.number,
                      interface.alternate,
                      interface.interfaceClass,
                      interface.interfaceSubClass,
                      interface.interfaceProtocol,
                      interface.endpointCount,
                      isCcid ? " ccid=1" : "");
        if (isCcid)
        {
            ccidFound = true;
        }
    }

    EspUsbHostEndpointInfo endpoints[ESP_USB_HOST_MAX_ENDPOINTS];
    const size_t endpointCount = usb.getEndpoints(device.address, endpoints, ESP_USB_HOST_MAX_ENDPOINTS);
    for (size_t i = 0; i < endpointCount; i++)
    {
        const EspUsbHostEndpointInfo &endpoint = endpoints[i];
        Serial.printf("  ENDPOINT interface=%u address=0x%02x dir=%s type=%s mps=%u interval=%u\n",
                      endpoint.interfaceNumber,
                      endpoint.address,
                      (endpoint.address & 0x80) ? "in" : "out",
                      endpointTypeName(endpoint.attributes),
                      endpoint.maxPacketSize,
                      endpoint.interval);
    }

    return ccidFound;
}

static void dumpAll()
{
    EspUsbHostDeviceInfo devices[ESP_USB_HOST_MAX_DEVICES];
    const size_t count = usb.getDevices(devices, ESP_USB_HOST_MAX_DEVICES);
    Serial.printf("devices=%u\n", (unsigned)count);

    bool ccidFound = false;
    for (size_t i = 0; i < count; i++)
    {
        if (dumpDevice(devices[i]))
        {
            ccidFound = true;
        }
    }

    Serial.printf("CCID_FOUND %u\n", ccidFound ? 1 : 0);
    Serial.println(ccidFound ? "[PASS]" : "[FAIL]");
}

void setup()
{
    Serial.begin(115200);
    delay(5000);

    usb.onDeviceConnected([](const EspUsbHostDeviceInfo &device)
                          { lastDeviceEventMs = millis();
                            Serial.printf("connected address=%u vid=%04x pid=%04x\n",
                                          device.address, device.vid, device.pid); });

    usb.begin();
    lastDeviceEventMs = millis();
    Serial.println("ccid_info test start");
}

void loop()
{
    static uint32_t startedAt = millis();

    if (!passed && millis() - lastDeviceEventMs >= SETTLE_MS && millis() - startedAt >= 3000)
    {
        passed = true;
        dumpAll();
    }

    if (!passed && millis() - startedAt > TEST_TIMEOUT_MS)
    {
        passed = true;
        dumpAll();
    }

    delay(10);
}
