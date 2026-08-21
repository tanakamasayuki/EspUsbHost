#include "EspUsbHost.h"

#include <stdio.h>

EspUsbHost usb;

static constexpr char BASE_PATH[] = "/usb";
static constexpr char PEER_FILE[] = "/usb/PEER.TXT";

static void waitForMsc()
{
    const uint32_t started = millis();
    while (!usb.mscReady() && millis() - started < 8000)
    {
        delay(10);
    }
}

static bool readPeerFile(char *out, size_t outSize)
{
    out[0] = '\0';
    FILE *file = fopen(PEER_FILE, "r");
    if (!file)
    {
        return false;
    }
    const size_t read = fread(out, 1, outSize - 1, file);
    out[read] = '\0';
    fclose(file);
    return read > 0;
}

static void reportRead(const char *tag)
{
    char body[32] = {};
    const bool ok = readPeerFile(body, sizeof(body));
    Serial.printf("%s ok=%u data=%s\n", tag, ok ? 1 : 0, body);
}

void setup()
{
    // Event prints can burst faster than the default serial TX buffer
    // drains; enlarge it so lines are not truncated mid-flight.
    Serial.setTxBufferSize(4096);
    Serial.begin(115200);
    delay(500);

    usb.onDeviceConnected([](const EspUsbHostDeviceInfo &device)
                          { Serial.printf("DEVICE_CONNECTED addr=%u class=0x%02x supported=%u\n",
                                          device.address,
                                          device.deviceClass,
                                          device.supported ? 1 : 0); });

    if (!usb.begin())
    {
        Serial.printf("usb.begin() failed: %s\n", usb.lastErrorName());
    }
}

void loop()
{
    if (Serial.available() <= 0)
    {
        delay(1);
        return;
    }

    const char command = Serial.read();
    waitForMsc();

    if (command == 'm')
    {
        const bool ok = usb.mscMount(BASE_PATH);
        Serial.printf("FAT_MOUNT ok=%u mounted=%u error=%s\n",
                      ok ? 1 : 0,
                      usb.mscMounted(BASE_PATH) ? 1 : 0,
                      usb.lastErrorName());
    }
    else if (command == 'r')
    {
        reportRead("FAT_READ");
    }
    else if (command == 'u')
    {
        const bool ok = usb.mscUnmount(BASE_PATH);
        Serial.printf("FAT_UNMOUNT ok=%u mounted=%u error=%s\n",
                      ok ? 1 : 0,
                      usb.mscMounted(BASE_PATH) ? 1 : 0,
                      usb.lastErrorName());
    }
    else if (command == 'y')
    {
        // mscUnmount() issues this before dropping the volume, so a device that
        // rejects it would leave lastError set on an otherwise clean unmount.
        const bool ok = usb.mscSynchronizeCache();
        Serial.printf("FAT_SYNC ok=%u error=%s\n", ok ? 1 : 0, usb.lastErrorName());
    }
    else if (command == 'c')
    {
        // The reported sequence: mount, use it, unmount, tear the host down,
        // then start over on the same object.
        Serial.printf("CYCLE_MOUNT ok=%u\n", usb.mscMount(BASE_PATH) ? 1 : 0);
        reportRead("CYCLE_READ");
        const bool cycleUnmounted = usb.mscUnmount(BASE_PATH);
        Serial.printf("CYCLE_UNMOUNT ok=%u error=%s\n",
                      cycleUnmounted ? 1 : 0,
                      usb.lastErrorName());

        usb.end();
        Serial.printf("CYCLE_STOP mounted=%u error=%s\n",
                      usb.mscMounted(BASE_PATH) ? 1 : 0,
                      usb.lastErrorName());

        Serial.printf("CYCLE_RESTART ready=%u error=%s\n",
                      usb.begin() ? 1 : 0,
                      usb.lastErrorName());
        waitForMsc();
        Serial.printf("CYCLE_REMOUNT ok=%u\n", usb.mscMount(BASE_PATH) ? 1 : 0);
        reportRead("CYCLE_REREAD");
        Serial.printf("CYCLE_FINAL_UNMOUNT ok=%u\n", usb.mscUnmount(BASE_PATH) ? 1 : 0);
    }
    else if (command == 'k')
    {
        // end() while a volume is still mounted. The mount owns a FatFs drive
        // slot and a VFS path, and mscFatMounts only has FF_VOLUMES entries, so
        // leaving it behind refuses the next mount of the same basePath.
        Serial.printf("KEEP_MOUNT ok=%u\n", usb.mscMount(BASE_PATH) ? 1 : 0);

        usb.end();
        Serial.printf("KEEP_STOP mounted=%u error=%s\n",
                      usb.mscMounted(BASE_PATH) ? 1 : 0,
                      usb.lastErrorName());

        Serial.printf("KEEP_RESTART ready=%u error=%s\n",
                      usb.begin() ? 1 : 0,
                      usb.lastErrorName());
        waitForMsc();
        Serial.printf("KEEP_REMOUNT ok=%u\n", usb.mscMount(BASE_PATH) ? 1 : 0);
        reportRead("KEEP_REREAD");
        Serial.printf("KEEP_FINAL_UNMOUNT ok=%u\n", usb.mscUnmount(BASE_PATH) ? 1 : 0);
    }
}
