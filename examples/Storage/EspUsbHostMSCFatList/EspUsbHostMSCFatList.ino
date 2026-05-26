#include "EspUsbHost.h"

#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

EspUsbHost usb;

static bool listed = false;

static void printRootEntries(const char *path)
{
    DIR *dir = opendir(path);
    if (!dir)
    {
        Serial.printf("opendir(%s) failed\n", path);
        return;
    }

    Serial.printf("Root entries at %s:\n", path);
    while (true)
    {
        dirent *entry = readdir(dir);
        if (!entry)
        {
            break;
        }

        char fullPath[128] = {};
        snprintf(fullPath, sizeof(fullPath), "%s/%s", path, entry->d_name);

        struct stat st = {};
        if (stat(fullPath, &st) == 0)
        {
            Serial.printf("  %s %s size=%llu\n",
                          S_ISDIR(st.st_mode) ? "DIR " : "FILE",
                          entry->d_name,
                          static_cast<unsigned long long>(st.st_size));
        }
        else
        {
            Serial.printf("  ???? %s\n", entry->d_name);
        }
    }
    closedir(dir);
}

static void writeReadDeleteProbe(const char *path)
{
    char filePath[128] = {};
    snprintf(filePath, sizeof(filePath), "%s/ESPUSBHT.TST", path);

    const char *message = "EspUsbHost MSC FAT write probe\n";
    FILE *file = fopen(filePath, "wb");
    if (!file)
    {
        Serial.printf("fopen(%s, wb) failed\n", filePath);
        return;
    }
    const size_t written = fwrite(message, 1, strlen(message), file);
    fclose(file);
    Serial.printf("Wrote %u bytes to %s\n", static_cast<unsigned>(written), filePath);

    file = fopen(filePath, "rb");
    if (!file)
    {
        Serial.printf("fopen(%s, rb) failed\n", filePath);
        return;
    }
    char buffer[64] = {};
    const size_t readBytes = fread(buffer, 1, sizeof(buffer) - 1, file);
    fclose(file);
    Serial.printf("Read back %u bytes: %s", static_cast<unsigned>(readBytes), buffer);

    if (remove(filePath) == 0)
    {
        Serial.printf("Removed %s\n", filePath);
    }
    else
    {
        Serial.printf("remove(%s) failed\n", filePath);
    }
}

void setup()
{
    Serial.begin(115200);
    delay(5000);

    usb.onDeviceConnected([](const EspUsbHostDeviceInfo &device)
                          {
                              Serial.print("connected: ");
                              espUsbHostPrint(device); });

    usb.onDeviceDisconnected([](const EspUsbHostDeviceInfo &device)
                             {
                                 Serial.print("disconnected: ");
                                 espUsbHostPrint(device);
                                 listed = false; });

    if (!usb.begin())
    {
        Serial.printf("usb.begin() failed: %s\n", usb.lastErrorName());
    }
}

void loop()
{
    if (listed || !usb.mscReady())
    {
        delay(10);
        return;
    }

    if (!usb.mscWaitReady())
    {
        Serial.println("MSC media is not ready yet.");
        delay(1000);
        return;
    }

    EspUsbHostMscBlockDeviceInfo info;
    if (usb.mscGetBlockDeviceInfo(info))
    {
        Serial.printf("MSC block device: address=%u lun=%u/%u blocks=%llu block_size=%lu bytes=%llu\n",
                      info.address,
                      info.lun,
                      info.maxLun,
                      static_cast<unsigned long long>(info.blockCount),
                      static_cast<unsigned long>(info.blockSize),
                      static_cast<unsigned long long>(info.capacityBytes));
    }

    if (!usb.mscMount("/usb"))
    {
        Serial.printf("mscMount failed: %s\n", usb.lastErrorName());
        delay(1000);
        return;
    }

    printRootEntries("/usb");
    writeReadDeleteProbe("/usb");

    if (!usb.mscUnmount("/usb"))
    {
        Serial.printf("mscUnmount failed: %s\n", usb.lastErrorName());
    }

    listed = true;
}
