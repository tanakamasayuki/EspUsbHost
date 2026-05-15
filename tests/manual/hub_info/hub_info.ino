#include "EspUsbHost.h"

EspUsbHost usb;

static constexpr uint32_t TEST_TIMEOUT_MS = 20000;
static constexpr uint32_t SETTLE_MS = 1500;

static bool passed = false;
static bool topologyReady = false;
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

static bool isHubDevice(const EspUsbHostDeviceInfo &device)
{
    if (device.deviceClass == USB_CLASS_HUB)
    {
        return true;
    }

    EspUsbHostInterfaceInfo interfaces[ESP_USB_HOST_MAX_INTERFACES];
    const size_t interfaceCount = usb.getInterfaces(device.address, interfaces, ESP_USB_HOST_MAX_INTERFACES);
    for (size_t i = 0; i < interfaceCount; i++)
    {
        if (interfaces[i].interfaceClass == USB_CLASS_HUB)
        {
            return true;
        }
    }
    return false;
}

static void printDevice(const EspUsbHostDeviceInfo &device)
{
    Serial.printf("device address=%u vid=%04x pid=%04x supported=%u hub=%u parent=",
                  device.address,
                  device.vid,
                  device.pid,
                  device.supported ? 1 : 0,
                  device.isHub ? 1 : 0);
    if (device.parentAddress)
    {
        Serial.printf("%u", device.parentAddress);
    }
    else
    {
        Serial.print("root");
    }
    Serial.printf(" portId=0x%02x speed=%s class=0x%02x interfaces=%u product=\"%s\"\n",
                  device.portId,
                  speedName(device.speed),
                  device.deviceClass,
                  device.configurationInterfaceCount,
                  device.product);
}

static void printHubInfo(uint8_t hubAddress)
{
    EspUsbHostHubInfo hub;
    if (!usb.getHubInfo(hubAddress, hub))
    {
        Serial.printf("hub address=%u descriptor: unavailable\n", hubAddress);
        return;
    }

    Serial.printf("hub address=%u ports=%u characteristics=0x%04x ppps=%u ganged_power=%u no_power_switch=%u compound=%u per_port_oc=%u ganged_oc=%u no_oc=%u pgood_ms=%u current_ma=%u\n",
                  hub.address,
                  hub.portCount,
                  hub.characteristics,
                  hub.perPortPowerSwitching ? 1 : 0,
                  hub.gangedPowerSwitching ? 1 : 0,
                  hub.noPowerSwitching ? 1 : 0,
                  hub.compound ? 1 : 0,
                  hub.perPortOverCurrent ? 1 : 0,
                  hub.gangedOverCurrent ? 1 : 0,
                  hub.noOverCurrent ? 1 : 0,
                  hub.powerOnToPowerGoodMs,
                  hub.controllerCurrentMa);
}

static void printTopology()
{
    EspUsbHostDeviceInfo devices[ESP_USB_HOST_MAX_DEVICES];
    const size_t count = usb.getDevices(devices, ESP_USB_HOST_MAX_DEVICES);
    Serial.printf("topology devices=%u\n", (unsigned)count);
    printHubInfo(1);
    for (size_t i = 0; i < count; i++)
    {
        printDevice(devices[i]);
        if (devices[i].isHub)
        {
            printHubInfo(devices[i].address);
        }
    }
}

static void checkPass()
{
    if (passed)
    {
        return;
    }

    bool hubFound = false;
    bool childBehindHubFound = false;
    uint8_t devicesWithPort = 0;
    uint8_t firstPortId = 0;
    bool multiplePortIds = false;

    EspUsbHostDeviceInfo devices[ESP_USB_HOST_MAX_DEVICES];
    const size_t count = usb.getDevices(devices, ESP_USB_HOST_MAX_DEVICES);
    for (size_t i = 0; i < count; i++)
    {
        if (isHubDevice(devices[i]))
        {
            hubFound = true;
        }
        if (devices[i].parentAddress != 0 && devices[i].portId != 0)
        {
            childBehindHubFound = true;
        }
        if (devices[i].portId != 0)
        {
            devicesWithPort++;
            if (firstPortId == 0)
            {
                firstPortId = devices[i].portId;
            }
            else if (firstPortId != devices[i].portId)
            {
                multiplePortIds = true;
            }
        }
    }

    const bool explicitHubTopology = hubFound && childBehindHubFound;
    const bool rootHubPortTopology = count >= 2 && devicesWithPort >= 2 && multiplePortIds;
    if (explicitHubTopology || rootHubPortTopology)
    {
        topologyReady = true;
    }

    if (topologyReady && millis() - lastDeviceEventMs >= SETTLE_MS)
    {
        passed = true;
        printTopology();
        Serial.printf("topology mode=%s\n", explicitHubTopology ? "explicit-hub" : "root-port");
        Serial.println("[PASS]");
    }
}

void setup()
{
    Serial.begin(115200);
    delay(5000);

    usb.onDeviceConnected([](const EspUsbHostDeviceInfo &device)
                          {
        lastDeviceEventMs = millis();
        Serial.printf("connected address=%u vid=%04x pid=%04x\n",
                      device.address,
                      device.vid,
                      device.pid);
        printDevice(device);
        checkPass(); });

    usb.onDeviceDisconnected([](const EspUsbHostDeviceInfo &device)
                             {
        lastDeviceEventMs = millis();
        Serial.printf("disconnected address=%u vid=%04x pid=%04x\n",
                      device.address,
                      device.vid,
                      device.pid); });

    usb.begin();
    lastDeviceEventMs = millis();
    Serial.println("hub_info test start");
    Serial.println("Connect a USB hub with at least two downstream USB devices.");
}

void loop()
{
    static uint32_t startedAt = millis();
    static bool failed = false;

    if (!passed)
    {
        checkPass();
    }

    if (!passed && !failed && millis() - startedAt > TEST_TIMEOUT_MS)
    {
        printTopology();
        Serial.println("[FAIL]");
        Serial.println("USB hub topology was not detected. Connect a hub with at least two downstream USB devices.");
        failed = true;
    }

    delay(10);
}
