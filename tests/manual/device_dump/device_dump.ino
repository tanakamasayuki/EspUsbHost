// Dumps every enumerated device: descriptors, interfaces, endpoints, plus a
// per-interface note for the classes this library does not claim by itself.
//
// Use it to work out what an unsupported device actually exposes before writing
// a wrapper for it (USBTMC, printer, vendor-specific bulk, ...).

#include "EspUsbHost.h"

EspUsbHost usb;

static constexpr uint32_t SETTLE_MS = 2000;
static constexpr uint32_t TEST_TIMEOUT_MS = 25000;

static uint32_t lastDeviceEventMs = 0;
static bool dumped = false;

static const char *interfaceNote(const EspUsbHostInterfaceInfo &intf)
{
    if (intf.interfaceClass == 0xfe && intf.interfaceSubClass == 0x03)
    {
        return intf.interfaceProtocol == 0x01 ? "USBTMC USB488 (SCPI over bulk)" : "USBTMC (bulk message protocol)";
    }
    if (intf.interfaceClass == 0x07)
    {
        return "printer";
    }
    if (intf.interfaceClass == 0xff)
    {
        return "vendor-specific";
    }
    return nullptr;
}

// Extra lines for interfaces a wrapper would have to drive by hand: which bulk
// endpoints it would get, and whether an interrupt IN is present.
static void printUnclaimedInterfaces(const EspUsbHostDeviceInfo &device)
{
    EspUsbHostInterfaceInfo interfaces[ESP_USB_HOST_MAX_INTERFACES];
    const size_t interfaceCount = usb.getInterfaces(device.address, interfaces, ESP_USB_HOST_MAX_INTERFACES);
    EspUsbHostEndpointInfo endpoints[ESP_USB_HOST_MAX_ENDPOINTS];
    const size_t endpointCount = usb.getEndpoints(device.address, endpoints, ESP_USB_HOST_MAX_ENDPOINTS);

    for (size_t i = 0; i < interfaceCount; i++)
    {
        const EspUsbHostInterfaceInfo &intf = interfaces[i];
        const char *note = interfaceNote(intf);
        if (!note)
        {
            continue;
        }
        Serial.printf("note address=%u interface=%u class=0x%02x subclass=0x%02x protocol=0x%02x claimed=%u kind=\"%s\"\n",
                      device.address,
                      intf.number,
                      intf.interfaceClass,
                      intf.interfaceSubClass,
                      intf.interfaceProtocol,
                      intf.claimed ? 1 : 0,
                      note);

        uint8_t bulkOut = 0;
        uint8_t bulkIn = 0;
        uint8_t interruptIn = 0;
        uint16_t bulkOutMps = 0;
        uint16_t bulkInMps = 0;
        for (size_t e = 0; e < endpointCount; e++)
        {
            const EspUsbHostEndpointInfo &ep = endpoints[e];
            if (ep.interfaceNumber != intf.number)
            {
                continue;
            }
            const bool in = (ep.address & USB_B_ENDPOINT_ADDRESS_EP_DIR_MASK) != 0;
            const uint8_t type = ep.attributes & USB_BM_ATTRIBUTES_XFERTYPE_MASK;
            if (type == USB_BM_ATTRIBUTES_XFER_BULK)
            {
                if (in && bulkIn == 0)
                {
                    bulkIn = ep.address;
                    bulkInMps = ep.maxPacketSize;
                }
                else if (!in && bulkOut == 0)
                {
                    bulkOut = ep.address;
                    bulkOutMps = ep.maxPacketSize;
                }
            }
            else if (type == USB_BM_ATTRIBUTES_XFER_INT && in && interruptIn == 0)
            {
                interruptIn = ep.address;
            }
        }
        Serial.printf("note address=%u interface=%u bulk_out=0x%02x mps=%u bulk_in=0x%02x mps=%u interrupt_in=0x%02x\n",
                      device.address,
                      intf.number,
                      bulkOut,
                      bulkOutMps,
                      bulkIn,
                      bulkInMps,
                      interruptIn);
    }
}

static void dump()
{
    usb.printAllDeviceInfo();

    EspUsbHostDeviceInfo devices[ESP_USB_HOST_MAX_DEVICES];
    const size_t count = usb.getDevices(devices, ESP_USB_HOST_MAX_DEVICES);
    for (size_t i = 0; i < count; i++)
    {
        printUnclaimedInterfaces(devices[i]);
    }
    Serial.printf("dump devices=%u\n", static_cast<unsigned>(count));
}

void setup()
{
    Serial.begin(115200);
    delay(5000);

    usb.onDeviceConnected([](const EspUsbHostDeviceInfo &device)
                          {
        lastDeviceEventMs = millis();
        Serial.printf("connected address=%u vid=%04x pid=%04x product=\"%s\"\n",
                      device.address,
                      device.vid,
                      device.pid,
                      device.product); });

    usb.onDeviceDisconnected([](const EspUsbHostDeviceInfo &device)
                             {
        lastDeviceEventMs = millis();
        Serial.printf("disconnected address=%u vid=%04x pid=%04x\n",
                      device.address,
                      device.vid,
                      device.pid); });

    usb.begin();
    lastDeviceEventMs = millis();
    Serial.println("device_dump test start");
}

void loop()
{
    static uint32_t startedAt = millis();

    if (!dumped)
    {
        const bool settled = millis() - lastDeviceEventMs >= SETTLE_MS;
        if (usb.deviceCount() > 0 && settled)
        {
            dump();
            Serial.println("[PASS]");
            dumped = true;
        }
        else if (millis() - startedAt > TEST_TIMEOUT_MS)
        {
            dump();
            Serial.println("[FAIL]");
            Serial.println("No USB device was enumerated.");
            dumped = true;
        }
    }

    delay(10);
}
