#include "EspUsbHost.h"

EspUsbHost usb;

static bool printed = false;

void setup()
{
    Serial.begin(115200);
    delay(500);

    usb.onDeviceConnected([](const EspUsbHostDeviceInfo &device)
                          { Serial.printf("connected: address=%u vid=%04x pid=%04x class=0x%02x supported=%u product=%s\n",
                                          device.address,
                                          device.vid,
                                          device.pid,
                                          device.deviceClass,
                                          device.supported ? 1 : 0,
                                          device.product); });

    usb.onDeviceDisconnected([](const EspUsbHostDeviceInfo &device)
                             {
                                 Serial.printf("disconnected: address=%u vid=%04x pid=%04x\n",
                                               device.address,
                                               device.vid,
                                               device.pid);
                                 printed = false;
                             });

    if (!usb.begin())
    {
        Serial.printf("usb.begin() failed: %s\n", usb.lastErrorName());
    }
}

void loop()
{
    if (printed || !usb.mscReady())
    {
        delay(10);
        return;
    }

    uint32_t blockCount = 0;
    uint32_t blockSize = 0;
    if (!usb.mscCapacity(blockCount, blockSize))
    {
        Serial.printf("mscCapacity failed: %s\n", usb.lastErrorName());
        delay(1000);
        return;
    }

    Serial.printf("MSC capacity: blocks=%lu block_size=%lu total_kib=%lu\n",
                  static_cast<unsigned long>(blockCount),
                  static_cast<unsigned long>(blockSize),
                  static_cast<unsigned long>((static_cast<uint64_t>(blockCount) * blockSize) / 1024));

    if (blockSize > 512)
    {
        Serial.println("First block is larger than this example buffer.");
        printed = true;
        return;
    }

    uint8_t block[512] = {};
    if (!usb.mscReadBlocks(0, block, 1))
    {
        Serial.printf("mscReadBlocks failed: %s\n", usb.lastErrorName());
        delay(1000);
        return;
    }

    Serial.print("LBA0:");
    for (size_t i = 0; i < 64 && i < blockSize; i++)
    {
        if ((i % 16) == 0)
        {
            Serial.printf("\n%04u:", static_cast<unsigned>(i));
        }
        Serial.printf(" %02x", block[i]);
    }
    Serial.println();
    printed = true;
}
