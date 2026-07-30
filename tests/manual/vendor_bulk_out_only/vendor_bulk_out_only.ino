#include "EspUsbHost.h"

// Verify that vendorOpen() accepts a vendor-specific interface whose only bulk
// endpoint is a bulk OUT. USB graphics adapters are the reference case: they
// expose one 0xff interface with a bulk OUT plus an interrupt IN that the
// vendor API does not use. Before the OUT-only fallback, vendorOpen() failed
// with "no bulk IN/OUT pair" on such devices.
//
// The sketch dumps the descriptor layout first so the same run doubles as a
// survey of the attached adapter, then opens the interface and reports the
// endpoint max packet sizes.

static constexpr uint32_t TEST_TIMEOUT_MS = 60000;
static constexpr uint8_t VENDOR_CLASS = 0xff;

static EspUsbHost usb;
static bool reported = false;

struct VendorInterfaceSurvey
{
    bool found = false;
    uint8_t number = 0;
    uint8_t endpointCount = 0;
    bool hasBulkIn = false;
    bool hasBulkOut = false;
    bool hasInterruptIn = false;
    uint8_t bulkOutCount = 0;
    uint8_t firstBulkOut = 0;
    uint16_t bulkOutPacketSize = 0;
};

static VendorInterfaceSurvey surveyVendorInterface(uint8_t address)
{
    VendorInterfaceSurvey survey;

    EspUsbHostInterfaceInfo interfaces[ESP_USB_HOST_MAX_INTERFACES];
    const size_t interfaceCount = usb.getInterfaces(address, interfaces, ESP_USB_HOST_MAX_INTERFACES);
    EspUsbHostEndpointInfo endpoints[ESP_USB_HOST_MAX_ENDPOINTS];
    const size_t endpointCount = usb.getEndpoints(address, endpoints, ESP_USB_HOST_MAX_ENDPOINTS);

    for (size_t i = 0; i < interfaceCount; i++)
    {
        const EspUsbHostInterfaceInfo &itf = interfaces[i];
        Serial.printf("INTERFACE number=%u alternate=%u class=0x%02x subclass=0x%02x protocol=0x%02x endpoints=%u claimed=%u\n",
                      itf.number, itf.alternate, itf.interfaceClass, itf.interfaceSubClass,
                      itf.interfaceProtocol, itf.endpointCount, itf.claimed ? 1 : 0);
    }

    for (size_t i = 0; i < endpointCount; i++)
    {
        const EspUsbHostEndpointInfo &ep = endpoints[i];
        const uint8_t type = ep.attributes & 0x03;
        const char *typeName = type == 0x00 ? "control" : type == 0x01 ? "isochronous"
                                              : type == 0x02 ? "bulk"
                                                             : "interrupt";
        Serial.printf("ENDPOINT interface=%u address=0x%02x dir=%s type=%s mps=%u interval=%u\n",
                      ep.interfaceNumber, ep.address, (ep.address & 0x80) ? "IN" : "OUT",
                      typeName, ep.maxPacketSize, ep.interval);
    }

    for (size_t i = 0; i < interfaceCount; i++)
    {
        const EspUsbHostInterfaceInfo &itf = interfaces[i];
        if (itf.interfaceClass != VENDOR_CLASS)
        {
            continue;
        }

        VendorInterfaceSurvey candidate;
        candidate.found = true;
        candidate.number = itf.number;
        candidate.endpointCount = itf.endpointCount;
        for (size_t e = 0; e < endpointCount; e++)
        {
            const EspUsbHostEndpointInfo &ep = endpoints[e];
            if (ep.interfaceNumber != itf.number)
            {
                continue;
            }
            const bool isIn = (ep.address & 0x80) != 0;
            const uint8_t type = ep.attributes & 0x03;
            if (type == 0x02 && isIn)
            {
                candidate.hasBulkIn = true;
            }
            else if (type == 0x02)
            {
                if (!candidate.hasBulkOut)
                {
                    // Descriptor order decides which endpoint vendorOpen() takes.
                    candidate.firstBulkOut = ep.address;
                    candidate.bulkOutPacketSize = ep.maxPacketSize;
                }
                candidate.hasBulkOut = true;
                candidate.bulkOutCount++;
            }
            else if (type == 0x03 && isIn)
            {
                candidate.hasInterruptIn = true;
            }
        }

        // Report the OUT-only interface if there is one; that is the case under
        // test. Otherwise keep the first vendor interface found.
        if (candidate.hasBulkOut && !candidate.hasBulkIn)
        {
            return candidate;
        }
        if (!survey.found)
        {
            survey = candidate;
        }
    }

    return survey;
}

// The adapter may sit behind a hub, so the target is not necessarily the first
// device that connects. Pick the first device that exposes a vendor-specific
// interface.
static uint8_t findVendorDeviceAddress()
{
    EspUsbHostDeviceInfo devices[ESP_USB_HOST_MAX_DEVICES];
    const size_t deviceCount = usb.getDevices(devices, ESP_USB_HOST_MAX_DEVICES);
    for (size_t i = 0; i < deviceCount; i++)
    {
        EspUsbHostInterfaceInfo interfaces[ESP_USB_HOST_MAX_INTERFACES];
        const size_t interfaceCount = usb.getInterfaces(devices[i].address, interfaces,
                                                        ESP_USB_HOST_MAX_INTERFACES);
        for (size_t j = 0; j < interfaceCount; j++)
        {
            if (interfaces[j].interfaceClass == VENDOR_CLASS)
            {
                return devices[i].address;
            }
        }
    }
    return 0;
}

static void runTest(uint8_t address)
{
    Serial.printf("VENDOR_TARGET address=%u\n", address);
    usb.printDeviceInfo(address);

    const VendorInterfaceSurvey survey = surveyVendorInterface(address);
    if (!survey.found)
    {
        Serial.println("VENDOR_SURVEY none");
        Serial.println("[FAIL] no vendor-specific (0xff) interface on the attached device");
        return;
    }

    Serial.printf("VENDOR_SURVEY interface=%u endpoints=%u bulk_in=%u bulk_out=%u interrupt_in=%u first_bulk_out=0x%02x out_mps=%u\n",
                  survey.number, survey.endpointCount, survey.hasBulkIn ? 1 : 0,
                  survey.bulkOutCount, survey.hasInterruptIn ? 1 : 0,
                  survey.firstBulkOut, survey.bulkOutPacketSize);

    if (!survey.hasBulkOut)
    {
        Serial.println("[FAIL] the vendor interface has no bulk OUT endpoint");
        return;
    }
    if (survey.hasBulkIn)
    {
        Serial.println("VENDOR_NOTE the vendor interface has a bulk IN/OUT pair, so the");
        Serial.println("VENDOR_NOTE OUT-only fallback is not exercised. Use a bulk-OUT-only");
        Serial.println("VENDOR_NOTE device such as a USB graphics adapter.");
        Serial.println("[FAIL] wrong device for this test");
        return;
    }

    const size_t channelsBefore = usb.endpointChannelCount(address);
    if (!usb.vendorOpen(address, survey.number))
    {
        Serial.printf("VENDOR_OPEN ok=0 last_error=%d\n", usb.lastError());
        Serial.println("[FAIL] vendorOpen() rejected a bulk-OUT-only interface");
        return;
    }
    const size_t channelsAfter = usb.endpointChannelCount(address);

    const uint16_t outMps = usb.vendorOutPacketSize(address);
    const uint16_t inMps = usb.vendorInPacketSize(address);
    const uint8_t outEndpoint = usb.vendorOutEndpoint(address);
    const uint8_t inEndpoint = usb.vendorInEndpoint(address);
    Serial.printf("VENDOR_OPEN ok=1 interface=%u out_ep=0x%02x in_ep=0x%02x out_mps=%u in_mps=%u channels=%u->%u\n",
                  survey.number, outEndpoint, inEndpoint, outMps, inMps,
                  static_cast<unsigned>(channelsBefore), static_cast<unsigned>(channelsAfter));

    // A second call must be idempotent and must not claim another channel.
    const bool reopen = usb.vendorOpen(address, survey.number);
    const size_t channelsReopen = usb.endpointChannelCount(address);
    Serial.printf("VENDOR_REOPEN ok=%u channels=%u\n", reopen ? 1 : 0,
                  static_cast<unsigned>(channelsReopen));

    // vendorRead() must stay quiet: no bulk IN endpoint means no data path.
    uint8_t buffer[16];
    const size_t read = usb.vendorRead(buffer, sizeof(buffer), address);
    Serial.printf("VENDOR_READ bytes=%u\n", static_cast<unsigned>(read));

    bool ok = true;
    if (outMps != survey.bulkOutPacketSize || outMps == 0)
    {
        Serial.println("VENDOR_CHECK out_mps mismatch");
        ok = false;
    }
    if (inMps != 0 || inEndpoint != 0)
    {
        Serial.println("VENDOR_CHECK in_mps / in_ep should be 0 without a bulk IN endpoint");
        ok = false;
    }
    if (outEndpoint != survey.firstBulkOut)
    {
        // With several bulk OUT endpoints the first in descriptor order must win;
        // a USB graphics adapter needs endpoint 0x01, not the later one.
        Serial.printf("VENDOR_CHECK out_ep should be the first bulk OUT 0x%02x\n", survey.firstBulkOut);
        ok = false;
    }
    if (channelsAfter != channelsBefore + 1)
    {
        Serial.println("VENDOR_CHECK endpoint channel count should grow by exactly 1");
        ok = false;
    }
    if (!reopen || channelsReopen != channelsAfter)
    {
        Serial.println("VENDOR_CHECK reopen should succeed without claiming another channel");
        ok = false;
    }
    if (read != 0)
    {
        Serial.println("VENDOR_CHECK vendorRead() should return 0 without a bulk IN endpoint");
        ok = false;
    }

    Serial.println(ok ? "[PASS]" : "[FAIL]");
}

void setup()
{
    Serial.begin(115200);
    delay(5000);
    Serial.println("vendor_bulk_out_only test start");
    Serial.println("Connect a USB graphics adapter (or any vendor device whose 0xff");
    Serial.println("interface has a bulk OUT but no bulk IN) to the host port.");

    usb.onDeviceConnected([](const EspUsbHostDeviceInfo &device) {
        Serial.printf("connected address=%u vid=%04x pid=%04x\n",
                      device.address, device.vid, device.pid);
    });

    usb.begin();
}

void loop()
{
    if (!reported)
    {
        const uint8_t address = findVendorDeviceAddress();
        if (address != 0)
        {
            reported = true;
            runTest(address);
        }
        else if (millis() > TEST_TIMEOUT_MS)
        {
            reported = true;
            Serial.println("[FAIL] no vendor-specific (0xff) interface found before the timeout");
        }
    }

    delay(10);
}
