#include "EspUsbHost.h"

#include <dirent.h>

EspUsbHost usb;

enum TestState
{
    WAIT_FIRST_MSC,
    WAIT_DISCONNECT,
    WAIT_RECONNECT,
    DONE,
};

static TestState state = WAIT_FIRST_MSC;
static volatile bool sawMscDisconnect = false;

static bool mountAndOpenRoot(const char *label)
{
    if (!usb.mscWaitReady(ESP_USB_HOST_ANY_ADDRESS, 5000, 1000))
    {
        Serial.printf("MSC_HOTPLUG_%s ok=0 step=wait_ready err=%s\n", label, usb.lastErrorName());
        return false;
    }
    if (!usb.mscMount("/usb"))
    {
        Serial.printf("MSC_HOTPLUG_%s ok=0 step=mount err=%s\n", label, usb.lastErrorName());
        return false;
    }
    DIR *root = opendir("/usb");
    if (!root)
    {
        Serial.printf("MSC_HOTPLUG_%s ok=0 step=opendir\n", label);
        usb.mscUnmount("/usb");
        return false;
    }
    closedir(root);
    Serial.printf("MSC_HOTPLUG_%s ok=1 path=/usb\n", label);
    return true;
}

void setup()
{
    Serial.begin(115200);
    delay(5000);

    usb.onDeviceConnected([](const EspUsbHostDeviceInfo &device)
                          { Serial.printf("MSC_HOTPLUG_DEVICE addr=%u vid=%04x pid=%04x supported=%u product=%s\n",
                                          device.address,
                                          device.vid,
                                          device.pid,
                                          device.supported ? 1 : 0,
                                          device.product); });

    usb.onDeviceDisconnected([](const EspUsbHostDeviceInfo &device)
                             {
                                 Serial.printf("MSC_HOTPLUG_DISCONNECT addr=%u vid=%04x pid=%04x\n",
                                               device.address,
                                               device.vid,
                                               device.pid);
                                 sawMscDisconnect = true;
                             });

    if (!usb.begin())
    {
        Serial.printf("MSC_HOTPLUG_BEGIN_FAIL %s\n", usb.lastErrorName());
        return;
    }
    Serial.println("MSC_HOTPLUG_READY");
    Serial.println("MSC_HOTPLUG_CONNECT_NOW");
}

void loop()
{
    if (state == WAIT_FIRST_MSC)
    {
        if (!usb.mscReady())
        {
            delay(10);
            return;
        }
        if (!mountAndOpenRoot("MOUNT"))
        {
            state = DONE;
            return;
        }
        Serial.println("MSC_HOTPLUG_UNPLUG_NOW");
        state = WAIT_DISCONNECT;
        return;
    }

    if (state == WAIT_DISCONNECT)
    {
        if (!sawMscDisconnect)
        {
            delay(10);
            return;
        }
        sawMscDisconnect = false;
        Serial.println("MSC_HOTPLUG_DISCONNECT_CLEANUP observed=1");
        Serial.println("MSC_HOTPLUG_RECONNECT_NOW");
        state = WAIT_RECONNECT;
        return;
    }

    if (state == WAIT_RECONNECT)
    {
        if (!usb.mscReady())
        {
            delay(10);
            return;
        }
        if (!mountAndOpenRoot("REMOUNT"))
        {
            state = DONE;
            return;
        }
        const bool unmountOk = usb.mscUnmount("/usb");
        Serial.printf("MSC_HOTPLUG_FINAL_UNMOUNT ok=%u path=/usb\n", unmountOk ? 1 : 0);
        if (unmountOk)
        {
            Serial.println("[PASS]");
        }
        state = DONE;
        return;
    }

    delay(10);
}
