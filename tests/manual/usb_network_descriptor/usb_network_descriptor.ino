#include "EspUsbHost.h"

EspUsbHost usb;

static constexpr uint32_t TEST_TIMEOUT_MS = 20000;
static bool passed = false;
static bool failed = false;
static uint8_t pendingAddress = 0;
static bool scanRequested = false;

static void printDeviceSummary(const EspUsbHostDeviceInfo &device)
{
    Serial.printf("device address=%u vid=%04x pid=%04x config=%u interfaces=%u class=0x%02x product=\"%s\"\n",
                  device.address,
                  device.vid,
                  device.pid,
                  device.configurationValue,
                  device.configurationInterfaceCount,
                  device.deviceClass,
                  device.product);
}

static void printActiveDescriptors(uint8_t address)
{
    EspUsbHostInterfaceInfo interfaces[ESP_USB_HOST_MAX_INTERFACES];
    const size_t interfaceCount = usb.getInterfaces(address, interfaces, ESP_USB_HOST_MAX_INTERFACES);
    for (size_t i = 0; i < interfaceCount; i++)
    {
        espUsbHostPrint(interfaces[i], Serial);
    }

    EspUsbHostEndpointInfo endpoints[ESP_USB_HOST_MAX_ENDPOINTS];
    const size_t endpointCount = usb.getEndpoints(address, endpoints, ESP_USB_HOST_MAX_ENDPOINTS);
    for (size_t i = 0; i < endpointCount; i++)
    {
        espUsbHostPrint(endpoints[i], Serial);
    }
}

static void scanNetworkDescriptors(uint8_t address)
{
    EspUsbHostNetworkInterfaceInfo networks[ESP_USB_HOST_MAX_NETWORK_INTERFACES];
    const size_t count = usb.getNetworkInterfaces(address, networks, ESP_USB_HOST_MAX_NETWORK_INTERFACES);
    Serial.printf("network candidates=%u\n", static_cast<unsigned>(count));

    bool completeFound = false;
    for (size_t i = 0; i < count; i++)
    {
        espUsbHostPrint(networks[i], Serial);
        if (networks[i].complete())
        {
            completeFound = true;
        }
    }

    if (completeFound)
    {
        passed = true;
        Serial.println("[PASS]");
    }
}

void setup()
{
    Serial.begin(115200);
    delay(5000);

    usb.onDeviceConnected([](const EspUsbHostDeviceInfo &device)
                          {
        printDeviceSummary(device);
        printActiveDescriptors(device.address);
        pendingAddress = device.address;
        scanRequested = true; });

    usb.onDeviceDisconnected([](const EspUsbHostDeviceInfo &device)
                             {
        Serial.printf("disconnected address=%u vid=%04x pid=%04x\n",
                      device.address,
                      device.vid,
                      device.pid);
        if (pendingAddress == device.address)
        {
            pendingAddress = 0;
            scanRequested = false;
        } });

    usb.begin();
    Serial.println("usb_network_descriptor test start");
    Serial.println("Connect a USB Ethernet adapter that exposes CDC-ECM or CDC-NCM.");
}

void loop()
{
    static uint32_t startedAt = millis();

    if (scanRequested && pendingAddress != 0 && !passed && !failed)
    {
        scanRequested = false;
        delay(500);
        scanNetworkDescriptors(pendingAddress);
    }

    if (!passed && !failed && millis() - startedAt > TEST_TIMEOUT_MS)
    {
        Serial.println("[FAIL]");
        Serial.println("No complete CDC-ECM or CDC-NCM network interface candidate was found.");
        failed = true;
    }

    delay(10);
}
