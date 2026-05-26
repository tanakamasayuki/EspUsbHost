#include "EspUsbHost.h"

EspUsbHost usb;

static void waitForMsc()
{
    const uint32_t started = millis();
    while (!usb.mscReady() && millis() - started < 5000)
    {
        delay(10);
    }
}

void setup()
{
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

    if (command == 'c')
    {
        uint32_t blocks = 0;
        uint32_t blockSize = 0;
        const bool ok = usb.mscCapacity(blocks, blockSize);
        Serial.printf("MSC_CAPACITY ok=%u blocks=%lu block_size=%lu\n",
                      ok ? 1 : 0,
                      static_cast<unsigned long>(blocks),
                      static_cast<unsigned long>(blockSize));
    }
    else if (command == 'd')
    {
        EspUsbHostMscBlockDeviceInfo info;
        const bool ok = usb.mscGetBlockDeviceInfo(info);
        Serial.printf("MSC_BLOCK_DEVICE ok=%u addr=%u iface=%u lun=%u max_lun=%u blocks=%llu block_size=%lu bytes=%llu\n",
                      ok ? 1 : 0,
                      info.address,
                      info.interfaceNumber,
                      info.lun,
                      info.maxLun,
                      static_cast<unsigned long long>(info.blockCount),
                      static_cast<unsigned long>(info.blockSize),
                      static_cast<unsigned long long>(info.capacityBytes));
    }
    else if (command == 'C')
    {
        uint64_t blocks = 0;
        uint32_t blockSize = 0;
        const bool ok = usb.mscCapacity64(blocks, blockSize);
        Serial.printf("MSC_CAPACITY64 ok=%u blocks=%lu block_size=%lu\n",
                      ok ? 1 : 0,
                      static_cast<unsigned long>(blocks),
                      static_cast<unsigned long>(blockSize));
    }
    else if (command == 'i')
    {
        EspUsbHostMscInquiry inquiry;
        const bool ok = usb.mscInquiry(inquiry);
        Serial.printf("MSC_INQUIRY ok=%u removable=%u vendor='%s' product='%s' revision='%s'\n",
                      ok ? 1 : 0,
                      inquiry.removable ? 1 : 0,
                      inquiry.vendor,
                      inquiry.product,
                      inquiry.revision);
    }
    else if (command == 'l')
    {
        uint8_t maxLun = 0xff;
        const bool ok = usb.mscMaxLun(maxLun);
        Serial.printf("MSC_MAX_LUN ok=%u max_lun=%u\n",
                      ok ? 1 : 0,
                      maxLun);
    }
    else if (command == 'L')
    {
        const bool ok = usb.mscSelectLun(0);
        Serial.printf("MSC_SELECT_LUN ok=%u\n", ok ? 1 : 0);
    }
    else if (command == 's')
    {
        EspUsbHostMscSense sense;
        const bool ok = usb.mscRequestSense(sense);
        Serial.printf("MSC_SENSE ok=%u response=0x%02x key=0x%02x asc=0x%02x ascq=0x%02x\n",
                      ok ? 1 : 0,
                      sense.responseCode,
                      sense.senseKey,
                      sense.additionalSenseCode,
                      sense.additionalSenseQualifier);
    }
    else if (command == 'S')
    {
        EspUsbHostMscSense sense;
        const bool ok = usb.mscLastSense(sense);
        Serial.printf("MSC_LAST_SENSE ok=%u response=0x%02x key=0x%02x asc=0x%02x ascq=0x%02x\n",
                      ok ? 1 : 0,
                      sense.responseCode,
                      sense.senseKey,
                      sense.additionalSenseCode,
                      sense.additionalSenseQualifier);
    }
    else if (command == 't')
    {
        const bool ok = usb.mscTestUnitReady();
        Serial.printf("MSC_TEST_UNIT_READY ok=%u\n", ok ? 1 : 0);
    }
    else if (command == 'y')
    {
        const bool ok = usb.mscSynchronizeCache();
        Serial.printf("MSC_SYNC_CACHE ok=%u\n", ok ? 1 : 0);
    }
    else if (command == 'r')
    {
        uint8_t block[512] = {};
        const bool ok = usb.mscReadBlocks(0, block, 1);
        Serial.printf("MSC_READ ok=%u b0=%02x b1=%02x b510=%02x b511=%02x\n",
                      ok ? 1 : 0,
                      block[0],
                      block[1],
                      block[510],
                      block[511]);
    }
    else if (command == 'R')
    {
        uint8_t block[512] = {};
        const bool ok = usb.mscReadBlocks64(0, block, 1);
        Serial.printf("MSC_READ64 ok=%u b0=%02x b1=%02x b510=%02x b511=%02x\n",
                      ok ? 1 : 0,
                      block[0],
                      block[1],
                      block[510],
                      block[511]);
    }
    else if (command == 'w')
    {
        uint8_t block[512] = {};
        for (size_t i = 0; i < sizeof(block); i++)
        {
            block[i] = static_cast<uint8_t>(i ^ 0xa5);
        }
        const bool writeOk = usb.mscWriteBlocks(4, block, 1);
        memset(block, 0, sizeof(block));
        const bool readOk = usb.mscReadBlocks(4, block, 1);
        Serial.printf("MSC_WRITE_READ write=%u read=%u b0=%02x b1=%02x b255=%02x b511=%02x\n",
                      writeOk ? 1 : 0,
                      readOk ? 1 : 0,
                      block[0],
                      block[1],
                      block[255],
                      block[511]);
    }
    else if (command == 'W')
    {
        uint8_t block[512] = {};
        for (size_t i = 0; i < sizeof(block); i++)
        {
            block[i] = static_cast<uint8_t>(0x5a ^ i);
        }
        const bool writeOk = usb.mscWriteBlocks64(5, block, 1);
        memset(block, 0, sizeof(block));
        const bool readOk = usb.mscReadBlocks64(5, block, 1);
        Serial.printf("MSC_WRITE_READ64 write=%u read=%u b0=%02x b1=%02x b255=%02x b511=%02x\n",
                      writeOk ? 1 : 0,
                      readOk ? 1 : 0,
                      block[0],
                      block[1],
                      block[255],
                      block[511]);
    }
    else if (command == 'm')
    {
        uint8_t blocks[1024] = {};
        for (size_t i = 0; i < sizeof(blocks); i++)
        {
            blocks[i] = static_cast<uint8_t>((i + 0x31) & 0xff);
        }
        const bool writeOk = usb.mscWriteBlocks(6, blocks, 2);
        memset(blocks, 0, sizeof(blocks));
        const bool readOk = usb.mscReadBlocks(6, blocks, 2);
        Serial.printf("MSC_MULTI write=%u read=%u b0=%02x b511=%02x b512=%02x b1023=%02x\n",
                      writeOk ? 1 : 0,
                      readOk ? 1 : 0,
                      blocks[0],
                      blocks[511],
                      blocks[512],
                      blocks[1023]);
    }
    else if (command == 'g')
    {
        static uint8_t blocks[512 * 9] = {};
        for (size_t i = 0; i < sizeof(blocks); i++)
        {
            blocks[i] = static_cast<uint8_t>((i * 3 + 0x17) & 0xff);
        }
        const bool writeOk = usb.mscWriteBlocks(1, blocks, 9);
        memset(blocks, 0, sizeof(blocks));
        const bool readOk = usb.mscReadBlocks(1, blocks, 9);
        Serial.printf("MSC_CHUNKED write=%u read=%u b0=%02x b4095=%02x b4096=%02x b4607=%02x\n",
                      writeOk ? 1 : 0,
                      readOk ? 1 : 0,
                      blocks[0],
                      blocks[4095],
                      blocks[4096],
                      blocks[4607]);
    }
    else if (command == 'o')
    {
        uint8_t block[512] = {};
        const bool readOk = usb.mscReadBlocks(16, block, 1);
        const bool writeOk = usb.mscWriteBlocks(16, block, 1);
        Serial.printf("MSC_OUT_OF_RANGE read=%u write=%u\n",
                      readOk ? 1 : 0,
                      writeOk ? 1 : 0);
    }
}
