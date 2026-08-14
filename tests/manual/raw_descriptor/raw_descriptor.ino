// Reads the raw DEVICE and CONFIGURATION descriptors of every enumerated device
// with standard GET_DESCRIPTOR requests on EP0, and walks the configuration
// block by block.
//
// device_dump prints what the library parsed. This prints the bytes themselves,
// including the class-specific descriptors the parsed dump never shows (HID,
// CDC functional, CCID, UAC), which is what a USBPcap capture or `lsusb -v`
// output has to be compared against.

#include "EspUsbHost.h"

EspUsbHost usb;

// setup(8) + data must fit ESP-IDF's 256-byte control transfer.
static constexpr size_t MAX_CONTROL_DATA = 248;
static constexpr uint32_t SETTLE_MS = 2000;
static constexpr uint32_t TEST_TIMEOUT_MS = 25000;

static uint32_t lastDeviceEventMs = 0;
static bool dumped = false;

static const char *descriptorTypeName(uint8_t type)
{
    switch (type)
    {
    case 0x01:
        return "DEVICE";
    case 0x02:
        return "CONFIGURATION";
    case 0x03:
        return "STRING";
    case 0x04:
        return "INTERFACE";
    case 0x05:
        return "ENDPOINT";
    case 0x0b:
        return "INTERFACE_ASSOCIATION";
    case 0x21:
        return "HID/class-specific(0x21)";
    case 0x24:
        return "CS_INTERFACE";
    case 0x25:
        return "CS_ENDPOINT";
    case 0x29:
        return "HUB";
    default:
        return "class/vendor";
    }
}

static void printHexLine(const char *prefix, uint8_t address, const uint8_t *data, size_t length)
{
    Serial.printf("%s address=%u len=%u data=", prefix, address, (unsigned)length);
    for (size_t i = 0; i < length; i++)
    {
        Serial.printf("%02x", data[i]);
        if (i + 1 < length)
        {
            Serial.print(" ");
        }
    }
    Serial.println();
}

static bool getDescriptor(uint8_t address, uint8_t type, uint8_t *buffer, size_t length, size_t *actual)
{
    return usb.vendorControlTransfer(0x80, 0x06, (uint16_t)(type << 8), 0, buffer, length, actual, address);
}

static void walkConfiguration(uint8_t address, const uint8_t *data, size_t length)
{
    size_t offset = 0;
    while (offset + 2 <= length)
    {
        const uint8_t blockLength = data[offset];
        const uint8_t type = data[offset + 1];
        if (blockLength < 2 || offset + blockLength > length)
        {
            Serial.printf("block address=%u offset=%u truncated bLength=%u remaining=%u\n",
                          address, (unsigned)offset, (unsigned)blockLength, (unsigned)(length - offset));
            return;
        }
        Serial.printf("block address=%u offset=%u len=%u type=0x%02x(%s) data=",
                      address, (unsigned)offset, (unsigned)blockLength, type, descriptorTypeName(type));
        for (size_t i = 0; i < blockLength; i++)
        {
            Serial.printf("%02x", data[offset + i]);
            if (i + 1 < blockLength)
            {
                Serial.print(" ");
            }
        }
        Serial.println();
        offset += blockLength;
    }
}

static bool dumpDevice(uint8_t address)
{
    static uint8_t buffer[MAX_CONTROL_DATA];
    size_t actual = 0;
    bool ok = true;

    if (getDescriptor(address, 0x01, buffer, 18, &actual) && actual >= 18)
    {
        printHexLine("device", address, buffer, actual);
    }
    else
    {
        Serial.printf("device address=%u GET_DESCRIPTOR(DEVICE) failed: %s\n", address, usb.lastErrorName());
        ok = false;
    }

    if (!getDescriptor(address, 0x02, buffer, 9, &actual) || actual < 9)
    {
        Serial.printf("config address=%u GET_DESCRIPTOR(CONFIGURATION) header failed: %s\n",
                      address, usb.lastErrorName());
        return false;
    }
    const size_t totalLength = (size_t)buffer[2] | ((size_t)buffer[3] << 8);
    size_t request = totalLength;
    if (request > MAX_CONTROL_DATA)
    {
        request = MAX_CONTROL_DATA;
        Serial.printf("config address=%u truncated wTotalLength=%u limit=%u\n",
                      address, (unsigned)totalLength, (unsigned)MAX_CONTROL_DATA);
    }
    if (!getDescriptor(address, 0x02, buffer, request, &actual) || actual == 0)
    {
        Serial.printf("config address=%u GET_DESCRIPTOR(CONFIGURATION,%u) failed: %s\n",
                      address, (unsigned)request, usb.lastErrorName());
        return false;
    }
    Serial.printf("config address=%u total=%u read=%u\n", address, (unsigned)totalLength, (unsigned)actual);
    printHexLine("config", address, buffer, actual);
    walkConfiguration(address, buffer, actual);
    return ok;
}

static bool dump()
{
    EspUsbHostDeviceInfo devices[ESP_USB_HOST_MAX_DEVICES];
    const size_t count = usb.getDevices(devices, ESP_USB_HOST_MAX_DEVICES);
    bool ok = count > 0;
    for (size_t i = 0; i < count; i++)
    {
        Serial.printf("target address=%u vid=%04x pid=%04x product=\"%s\"\n",
                      devices[i].address, devices[i].vid, devices[i].pid, devices[i].product);
        if (!dumpDevice(devices[i].address))
        {
            ok = false;
        }
    }
    Serial.printf("dump devices=%u\n", (unsigned)count);
    return ok;
}

void setup()
{
    Serial.setTxBufferSize(4096);
    Serial.begin(115200);
    delay(5000);

    usb.onDeviceConnected([](const EspUsbHostDeviceInfo &device)
                          {
        lastDeviceEventMs = millis();
        Serial.printf("connected address=%u vid=%04x pid=%04x\n", device.address, device.vid, device.pid); });

    usb.onDeviceDisconnected([](const EspUsbHostDeviceInfo &device)
                             {
        lastDeviceEventMs = millis();
        Serial.printf("disconnected address=%u\n", device.address); });

    usb.begin();
    lastDeviceEventMs = millis();
    Serial.println("raw_descriptor test start");
}

void loop()
{
    static uint32_t startedAt = millis();

    if (!dumped)
    {
        const bool settled = millis() - lastDeviceEventMs >= SETTLE_MS;
        if (usb.deviceCount() > 0 && settled)
        {
            Serial.println(dump() ? "[PASS]" : "[FAIL]");
            dumped = true;
        }
        else if (millis() - startedAt > TEST_TIMEOUT_MS)
        {
            Serial.println("[FAIL]");
            Serial.println("No USB device was enumerated.");
            dumped = true;
        }
    }

    delay(10);
}
