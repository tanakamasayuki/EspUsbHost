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
    // en: LBA0 contains an MBR only when the boot signature is present.
    // ja: LBA0がMBRとして扱えるのは、ブートシグネチャがある場合だけです。
    if (block[510] != 0x55 || block[511] != 0xaa)
    {
        Serial.println("No MBR signature at LBA0.");
        return;
    }

    Serial.println("MBR partitions:");
    // en: The classic MBR partition table has four 16-byte entries at offset 446.
    // ja: 古典的なMBRパーティションテーブルは、オフセット446から16バイトのエントリが4つ並びます。
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
                          {
                              Serial.print("connected: ");
                              espUsbHostPrint(device);
                          });

    usb.onDeviceDisconnected([](const EspUsbHostDeviceInfo &device)
                             {
                                 Serial.print("disconnected: ");
                                 espUsbHostPrint(device);
                                 printed = false;
                             });

    if (!usb.begin())
    {
        Serial.printf("usb.begin() failed: %s\n", usb.lastErrorName());
    }
}

void loop()
{
    // en: MSC commands block until USB transfers complete, so run them from loop(), not USB callbacks.
    // ja: MSCコマンドはUSB転送完了を待つため、USBコールバック内ではなくloop()から実行します。
    if (printed || !usb.mscReady())
    {
        delay(10);
        return;
    }

    EspUsbHostMscInquiry inquiry;
    if (usb.mscInquiry(inquiry))
    {
        Serial.printf("MSC inquiry: removable=%u vendor='%s' product='%s' revision='%s'\n",
                      inquiry.removable ? 1 : 0,
                      inquiry.vendor,
                      inquiry.product,
                      inquiry.revision);
    }
    EspUsbHostMscSense sense;
    if (usb.mscRequestSense(sense))
    {
        Serial.printf("MSC sense: response=0x%02x key=0x%02x asc=0x%02x ascq=0x%02x\n",
                      sense.responseCode,
                      sense.senseKey,
                      sense.additionalSenseCode,
                      sense.additionalSenseQualifier);
    }
    Serial.printf("MSC test unit ready: %u\n", usb.mscTestUnitReady() ? 1 : 0);

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
    // en: Dump only the first 64 bytes to keep the example output readable.
    // ja: サンプル出力を読みやすくするため、先頭64バイトだけをダンプします。
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
