#include <Arduino.h>

#if ARDUINO_USB_MODE
void setup() {}
void loop() {}
#else

#include "USB.h"
#include "USBMSC.h"

USBMSC MSC;

static constexpr uint32_t BLOCK_COUNT = 16;
static constexpr uint16_t BLOCK_SIZE = 512;
static uint8_t disk[BLOCK_COUNT][BLOCK_SIZE];

static int32_t onRead(uint32_t lba, uint32_t offset, void *buffer, uint32_t bufsize)
{
    if (lba >= BLOCK_COUNT || offset + bufsize > BLOCK_SIZE)
    {
        return -1;
    }
    memcpy(buffer, disk[lba] + offset, bufsize);
    return bufsize;
}

static int32_t onWrite(uint32_t lba, uint32_t offset, uint8_t *buffer, uint32_t bufsize)
{
    if (lba >= BLOCK_COUNT || offset + bufsize > BLOCK_SIZE)
    {
        return -1;
    }
    memcpy(disk[lba] + offset, buffer, bufsize);
    Serial.printf("DEVICE_WRITE lba=%lu offset=%lu size=%lu\n",
                  static_cast<unsigned long>(lba),
                  static_cast<unsigned long>(offset),
                  static_cast<unsigned long>(bufsize));
    return bufsize;
}

static bool onStartStop(uint8_t power_condition, bool start, bool load_eject)
{
    (void)power_condition;
    (void)start;
    (void)load_eject;
    return true;
}

void setup()
{
    Serial.begin(115200);

    memset(disk, 0, sizeof(disk));
    disk[0][0] = 0xeb;
    disk[0][1] = 0x3c;
    disk[0][510] = 0x55;
    disk[0][511] = 0xaa;

    MSC.vendorID("ESP32");
    MSC.productID("MSC_PEER");
    MSC.productRevision("1.0");
    MSC.onStartStop(onStartStop);
    MSC.onRead(onRead);
    MSC.onWrite(onWrite);
    MSC.mediaPresent(true);
    MSC.isWritable(true);
    MSC.begin(BLOCK_COUNT, BLOCK_SIZE);
    USB.begin();
    Serial.println("DEVICE_MSC_READY");
}

void loop()
{
    delay(1);
}

#endif
