#include "EspUsbHost.h"

#include <string.h>

EspUsbHost usb;
EspUsbHostMscFS usbMassStorage;

static bool listed = false;

static void printRootEntries(fs::FS &fs)
{
    File root = fs.open("/");
    if (!root || !root.isDirectory())
    {
        Serial.println("open(/) failed");
        return;
    }

    Serial.println("Root entries:");
    while (true)
    {
        File entry = root.openNextFile();
        if (!entry)
        {
            break;
        }

        Serial.printf("  %s %s size=%u\n",
                      entry.isDirectory() ? "DIR " : "FILE",
                      entry.name(),
                      static_cast<unsigned>(entry.size()));
        entry.close();
    }
    root.close();
}

static void writeReadDeleteProbe(fs::FS &fs)
{
    const char *filePath = "/ESPUSBHT.TST";

    const char *message = "EspUsbHost MSC FAT write probe\n";
    File file = fs.open(filePath, FILE_WRITE);
    if (!file)
    {
        Serial.printf("open(%s, FILE_WRITE) failed\n", filePath);
        return;
    }
    const size_t written = file.print(message);
    file.close();
    Serial.printf("Wrote %u bytes to %s\n", static_cast<unsigned>(written), filePath);

    file = fs.open(filePath, FILE_READ);
    if (!file)
    {
        Serial.printf("open(%s, FILE_READ) failed\n", filePath);
        return;
    }
    char buffer[64] = {};
    const size_t readBytes = file.readBytes(buffer, sizeof(buffer) - 1);
    file.close();
    Serial.printf("Read back %u bytes: %s", static_cast<unsigned>(readBytes), buffer);

    if (fs.remove(filePath))
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
                                 usbMassStorage.end();
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

    if (!usbMassStorage.begin(usb, "/usb"))
    {
        Serial.printf("USB MSC FS mount failed: %s\n", usb.lastErrorName());
        delay(1000);
        return;
    }

    printRootEntries(usbMassStorage);
    writeReadDeleteProbe(usbMassStorage);

    usbMassStorage.end();

    listed = true;
}
