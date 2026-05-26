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
    delay(5000);

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

    EspUsbHostMscInquiry inquiry;
    if (usb.mscInquiry(inquiry))
    {
        Serial.printf("MSC_INQUIRY removable=%u vendor='%s' product='%s' revision='%s'\n",
                      inquiry.removable ? 1 : 0,
                      inquiry.vendor,
                      inquiry.product,
                      inquiry.revision);
    }
    EspUsbHostMscSense sense;
    if (usb.mscRequestSense(sense))
    {
        Serial.printf("MSC_SENSE response=0x%02x key=0x%02x asc=0x%02x ascq=0x%02x\n",
                      sense.responseCode,
                      sense.senseKey,
                      sense.additionalSenseCode,
                      sense.additionalSenseQualifier);
    }
    const bool ready = usb.mscTestUnitReady();
    Serial.printf("MSC_TEST_UNIT_READY ok=%u\n", ready ? 1 : 0);

    uint8_t maxLun = 0;
    if (usb.mscMaxLun(maxLun))
    {
        Serial.printf("MSC_MAX_LUN max_lun=%u\n", maxLun);
    }
    if (usb.mscSelectLun(0))
    {
        Serial.println("MSC_SELECT_LUN lun=0 ok=1");
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

    EspUsbHostMscBlockDeviceInfo blockInfo;
    if (usb.mscGetBlockDeviceInfo(blockInfo))
    {
        Serial.printf("MSC_BLOCK_DEVICE addr=%u iface=%u lun=%u max_lun=%u blocks=%llu block_size=%lu bytes=%llu\n",
                      blockInfo.address,
                      blockInfo.interfaceNumber,
                      blockInfo.lun,
                      blockInfo.maxLun,
                      static_cast<unsigned long long>(blockInfo.blockCount),
                      static_cast<unsigned long>(blockInfo.blockSize),
                      static_cast<unsigned long long>(blockInfo.capacityBytes));
    }

    uint64_t blockCount64 = 0;
    uint32_t blockSize64 = 0;
    if (usb.mscCapacity64(blockCount64, blockSize64))
    {
        Serial.printf("MSC_CAPACITY64 blocks=%llu block_size=%lu\n",
                      static_cast<unsigned long long>(blockCount64),
                      static_cast<unsigned long>(blockSize64));
    }

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

    uint8_t block64[512] = {};
    if (!usb.mscReadBlocks64(0, block64, 1))
    {
        Serial.printf("MSC_READ64_FAIL %s\n", usb.lastErrorName());
        delay(1000);
        return;
    }
    Serial.printf("MSC_LBA0_64 b0=%02x b1=%02x b510=%02x b511=%02x\n",
                  block64[0],
                  block64[1],
                  block64[510],
                  block64[511]);

    const bool syncOk = usb.mscSynchronizeCache();
    Serial.printf("MSC_SYNC_CACHE ok=%u\n", syncOk ? 1 : 0);

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
