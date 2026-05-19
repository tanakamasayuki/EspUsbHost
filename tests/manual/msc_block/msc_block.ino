#include "EspUsbHost.h"

EspUsbHost usb;

static bool tested = false;

static uint32_t readLe32(const uint8_t *data)
{
    return static_cast<uint32_t>(data[0]) |
           (static_cast<uint32_t>(data[1]) << 8) |
           (static_cast<uint32_t>(data[2]) << 16) |
           (static_cast<uint32_t>(data[3]) << 24);
}

static void printPartitions(const uint8_t *block)
{
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
        Serial.printf("MSC_PARTITION index=%u boot=0x%02x type=0x%02x first_lba=%lu sectors=%lu\n",
                      i + 1,
                      entry[0],
                      type,
                      static_cast<unsigned long>(firstLba),
                      static_cast<unsigned long>(sectors));
    }
}

void setup()
{
    Serial.begin(115200);
    delay(1000);

    usb.onDeviceConnected([](const EspUsbHostDeviceInfo &device)
                          { Serial.printf("MSC_DEVICE addr=%u vid=%04x pid=%04x supported=%u product=%s\n",
                                          device.address,
                                          device.vid,
                                          device.pid,
                                          device.supported ? 1 : 0,
                                          device.product); });

    usb.onDeviceDisconnected([](const EspUsbHostDeviceInfo &device)
                             {
                                 Serial.printf("MSC_DISCONNECT addr=%u vid=%04x pid=%04x\n",
                                               device.address,
                                               device.vid,
                                               device.pid);
                                 tested = false; });

    if (!usb.begin())
    {
        Serial.printf("MSC_BEGIN_FAIL %s\n", usb.lastErrorName());
        return;
    }
    Serial.println("MSC_MANUAL_READY");
}

void loop()
{
    if (tested || !usb.mscReady())
    {
        delay(10);
        return;
    }

    uint32_t blockCount = 0;
    uint32_t blockSize = 0;
    if (!usb.mscCapacity(blockCount, blockSize))
    {
        Serial.printf("MSC_CAPACITY_FAIL %s\n", usb.lastErrorName());
        delay(1000);
        return;
    }
    Serial.printf("MSC_CAPACITY blocks=%lu block_size=%lu\n",
                  static_cast<unsigned long>(blockCount),
                  static_cast<unsigned long>(blockSize));

    if (blockSize == 0 || blockSize > 512)
    {
        Serial.printf("MSC_UNSUPPORTED_BLOCK_SIZE %lu\n", static_cast<unsigned long>(blockSize));
        tested = true;
        return;
    }

    uint8_t block[512] = {};
    if (!usb.mscReadBlocks(0, block, 1))
    {
        Serial.printf("MSC_READ_FAIL %s\n", usb.lastErrorName());
        delay(1000);
        return;
    }

    Serial.printf("MSC_LBA0 b0=%02x b1=%02x b510=%02x b511=%02x\n",
                  block[0],
                  block[1],
                  block[510],
                  block[511]);
    if (block[510] != 0x55 || block[511] != 0xaa)
    {
        Serial.println("MSC_SIGNATURE_MISSING");
        tested = true;
        return;
    }

    printPartitions(block);
    Serial.println("[PASS]");
    tested = true;
}
