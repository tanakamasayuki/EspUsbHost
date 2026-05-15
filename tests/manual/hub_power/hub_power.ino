#include "EspUsbHost.h"

EspUsbHost usb;

static constexpr uint32_t DEVICE_WAIT_MS = 10000;
static constexpr uint32_t DISCONNECT_WAIT_MS = 12000;
static constexpr uint32_t RECONNECT_WAIT_MS = 10000;
static constexpr uint8_t DEFAULT_HUB_ADDRESS = 1;

static volatile bool disconnected = false;
static volatile bool reconnected = false;
static bool tested = false;
static uint8_t targetAddress = 0;
static uint16_t targetVid = 0;
static uint16_t targetPid = 0;
static uint8_t targetHubAddress = 0;
static uint8_t targetPort = 0;

static bool readAndPrintPortStatus(const char *label, bool &powered, bool &connected, bool &enabled)
{
    uint16_t status = 0;
    uint16_t change = 0;
    const bool ok = usb.getHubPortStatus(targetHubAddress, targetPort, status, change);
    powered = (status & 0x0100) != 0;
    connected = (status & 0x0001) != 0;
    enabled = (status & 0x0002) != 0;
    Serial.printf("%s status request: %s status=0x%04x change=0x%04x powered=%u connected=%u enabled=%u\n",
                  label,
                  ok ? "OK" : "FAIL",
                  status,
                  change,
                  powered ? 1 : 0,
                  connected ? 1 : 0,
                  enabled ? 1 : 0);
    return ok;
}

static const char *parentName(uint8_t parentAddress)
{
    return parentAddress ? "hub" : "root";
}

static void printDevice(const EspUsbHostDeviceInfo &device)
{
    Serial.printf("device address=%u vid=%04x pid=%04x parent=%s",
                  device.address,
                  device.vid,
                  device.pid,
                  parentName(device.parentAddress));
    if (device.parentAddress)
    {
        Serial.printf(":%u", device.parentAddress);
    }
    Serial.printf(" portId=0x%02x product=\"%s\"\n", device.portId, device.product);
}

static bool chooseTarget()
{
    EspUsbHostDeviceInfo devices[ESP_USB_HOST_MAX_DEVICES];
    const size_t count = usb.getDevices(devices, ESP_USB_HOST_MAX_DEVICES);
    for (size_t i = 0; i < count; i++)
    {
        const EspUsbHostDeviceInfo &device = devices[i];
        if (device.portId == 0)
        {
            continue;
        }

        targetAddress = device.address;
        targetVid = device.vid;
        targetPid = device.pid;
        targetHubAddress = device.parentAddress ? device.parentAddress : DEFAULT_HUB_ADDRESS;
        targetPort = device.parentAddress ? (device.portId & 0x0f) : device.portId;
        if (targetPort == 0 || targetPort > 0x0f)
        {
            continue;
        }

        Serial.printf("target address=%u vid=%04x pid=%04x hub=%u port=%u portId=0x%02x\n",
                      targetAddress,
                      targetVid,
                      targetPid,
                      targetHubAddress,
                      targetPort,
                      device.portId);
        return true;
    }
    return false;
}

static bool waitForFlag(volatile bool &flag, uint32_t timeoutMs)
{
    const uint32_t deadline = millis() + timeoutMs;
    while (!flag && millis() < deadline)
    {
        delay(10);
    }
    return flag;
}

static bool waitForPortState(const char *label, bool expectedPowered, bool expectedConnected, uint32_t timeoutMs)
{
    const uint32_t deadline = millis() + timeoutMs;
    while (millis() < deadline)
    {
        bool powered = false;
        bool connected = false;
        bool enabled = false;
        if (readAndPrintPortStatus(label, powered, connected, enabled) &&
            powered == expectedPowered &&
            connected == expectedConnected)
        {
            return true;
        }
        delay(250);
    }
    return false;
}

void setup()
{
    Serial.begin(115200);
    delay(5000);

    usb.onDeviceConnected([](const EspUsbHostDeviceInfo &device)
                          {
        Serial.printf("connected address=%u vid=%04x pid=%04x\n", device.address, device.vid, device.pid);
        printDevice(device);
        if (tested && device.portId != 0 && (device.portId & 0x0f) == targetPort)
        {
            reconnected = true;
        } });

    usb.onDeviceDisconnected([](const EspUsbHostDeviceInfo &device)
                             {
        Serial.printf("disconnected address=%u vid=%04x pid=%04x portId=0x%02x\n",
                      device.address,
                      device.vid,
                      device.pid,
                      device.portId);
        if (device.address == targetAddress || (device.vid == targetVid && device.pid == targetPid))
        {
            disconnected = true;
        } });

    usb.begin();
    Serial.println("hub_power test start");
    Serial.println("Connect a USB hub with at least one downstream USB device.");
}

void loop()
{
    if (tested)
    {
        delay(100);
        return;
    }

    const uint32_t deadline = millis() + DEVICE_WAIT_MS;
    while (!chooseTarget() && millis() < deadline)
    {
        delay(100);
    }

    if (targetAddress == 0)
    {
        Serial.println("[FAIL]");
        Serial.println("No downstream USB device with a usable portId was detected.");
        tested = true;
        return;
    }

    tested = true;
    disconnected = false;
    reconnected = false;

    bool poweredBefore = false;
    bool connectedBefore = false;
    bool enabledBefore = false;
    readAndPrintPortStatus("before", poweredBefore, connectedBefore, enabledBefore);

    Serial.println("PORT_POWER_OFF");
    const bool offSubmitted = usb.setHubPortPower(targetHubAddress, targetPort, false);
    Serial.printf("power off request: %s\n", offSubmitted ? "OK" : "FAIL");
    delay(500);
    bool poweredAfterOff = true;
    bool connectedAfterOff = true;
    bool enabledAfterOff = true;
    bool statusAfterOffOk = readAndPrintPortStatus("after off", poweredAfterOff, connectedAfterOff, enabledAfterOff);
    if (statusAfterOffOk && (poweredAfterOff || connectedAfterOff))
    {
        statusAfterOffOk = waitForPortState("after off retry", false, false, 3000);
        poweredAfterOff = false;
        connectedAfterOff = false;
    }
    if (connectedAfterOff)
    {
        const bool disconnectedOk = offSubmitted && waitForFlag(disconnected, DISCONNECT_WAIT_MS);
        Serial.printf("disconnect callback after power off: %s\n", disconnectedOk ? "OK" : "not observed");
    }
    else
    {
        Serial.println("disconnect callback after power off: skipped; port status already disconnected");
    }

    delay(1000);

    Serial.println("PORT_POWER_ON");
    const bool onSubmitted = usb.setHubPortPower(targetHubAddress, targetPort, true);
    Serial.printf("power on request: %s\n", onSubmitted ? "OK" : "FAIL");
    const bool reconnectedOk = onSubmitted && waitForFlag(reconnected, RECONNECT_WAIT_MS);
    bool poweredAfterOn = false;
    bool connectedAfterOn = false;
    bool enabledAfterOn = false;
    bool statusAfterOnOk = readAndPrintPortStatus("after on", poweredAfterOn, connectedAfterOn, enabledAfterOn);
    if (!statusAfterOnOk || !poweredAfterOn || !connectedAfterOn)
    {
        statusAfterOnOk = waitForPortState("after on retry", true, true, 5000);
        poweredAfterOn = statusAfterOnOk;
        connectedAfterOn = statusAfterOnOk;
    }
    Serial.printf("reconnect after power on: %s\n", reconnectedOk ? "OK" : "FAIL");

    const bool powerStatusOk = statusAfterOffOk && statusAfterOnOk &&
                               poweredBefore && connectedBefore &&
                               !poweredAfterOff && !connectedAfterOff &&
                               poweredAfterOn && connectedAfterOn;
    Serial.printf("power status toggle: %s\n", powerStatusOk ? "OK" : "FAIL");

    Serial.println(powerStatusOk && reconnectedOk ? "[PASS]" : "[FAIL]");
}
