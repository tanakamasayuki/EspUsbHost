#include "EspUsbHost.h"

#include <stdio.h>

EspUsbHost usb;

// The USB host is not started in setup(). The peer board is flashed after this
// sketch has booted, so a host started here observes esptool resetting the peer
// and records the resulting enumerations as errors. The test starts it once the
// peer is in place, and stops it again in the fixture teardown so the board is
// not left hosting USB after the run.
//
// Q / G / H rather than lower case: every lower-case letter is already a test
// command in one sketch or another, so the lifecycle commands get their own
// range instead of colliding per sketch.
static bool hostStarted = false;

static void startHost()
{
    if (hostStarted)
    {
        return; // idempotent
    }
    if (!usb.begin())
    {
        Serial.printf("usb.begin() failed: %s\n", usb.lastErrorName());
        return;
    }
    hostStarted = true;
    // Nothing is printed here on purpose: an answer would have to be consumed by
    // an expect() in the fixture, which would advance the reader past the
    // enumeration output that follows and that the tests read.
}

static void stopHost()
{
    usb.end();
    hostStarted = false;
    Serial.println("HOST_STATE idle devices=0");
}

// Answered whenever it is asked, so a test can wait for enumeration by polling
// this rather than by waiting for a connect line that is printed once.
static bool handleLifecycle(char command)
{
    if (command == 'Q')
    {
        Serial.printf("HOST_STATE %s devices=%u\n",
                      hostStarted ? "running" : "idle",
                      static_cast<unsigned>(usb.deviceCount()));
        return true;
    }
    if (command == 'G')
    {
        startHost();
        return true;
    }
    if (command == 'H')
    {
        stopHost();
        return true;
    }
    return false;
}

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

    Serial.println("HOST_STATE idle devices=0");
}

void loop()
{
    if (Serial.available() <= 0)
    {
        delay(1);
        return;
    }

    const char command = Serial.read();
    if (handleLifecycle(command))
    {
        return;
    }
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
