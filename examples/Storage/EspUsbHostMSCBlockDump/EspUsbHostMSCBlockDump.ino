#include "EspUsbHost.h"

EspUsbHost usb;

static bool printed = false;

static uint32_t readLe32(const uint8_t *data)
{
    return static_cast<uint32_t>(data[0]) |
           (static_cast<uint32_t>(data[1]) << 8) |
           (static_cast<uint32_t>(data[2]) << 16) |
           (static_cast<uint32_t>(data[3]) << 24);
}

static void printMbrPartitions(const uint8_t *block)
{
    if (block[510] != 0x55 || block[511] != 0xaa)
    {
        Serial.println("No MBR signature at LBA0.");
        return;
    }

    Serial.println("MBR partitions:");
    for (uint8_t i = 0; i < 4; i++)
    {
        const uint8_t *entry = block + 446 + i * 16;
        const uint8_t type = entry[4];
        const uint32_t firstLba = readLe32(entry + 8);
        const uint32_t sectors = readLe32(entry + 12);
        if (type == 0 || sectors == 0)
        {
            continue;
        }
        Serial.printf("  #%u boot=0x%02x type=0x%02x first_lba=%lu sectors=%lu size_kib=%lu\n",
                      i + 1,
                      entry[0],
                      type,
                      static_cast<unsigned long>(firstLba),
                      static_cast<unsigned long>(sectors),
                      static_cast<unsigned long>((static_cast<uint64_t>(sectors) * 512) / 1024));
    }
}

void setup()
{
    Serial.begin(115200);
    delay(5000);

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
                                 printed = false; });

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
    printMbrPartitions(block);
    printed = true;
}
