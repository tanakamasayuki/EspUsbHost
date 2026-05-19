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
}
