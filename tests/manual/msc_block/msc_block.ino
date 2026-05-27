#include "EspUsbHost.h"

#include <dirent.h>
#include <stdio.h>
#include <sys/stat.h>

EspUsbHost usb;
EspUsbHostMscFS usbMscFs;

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
    const bool waitReady = usb.mscWaitReady(ESP_USB_HOST_ANY_ADDRESS, 5000, 1000);
    Serial.printf("MSC_WAIT_READY ok=%u\n", waitReady ? 1 : 0);

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

    if (!usb.mscMount("/usb"))
    {
        Serial.printf("MSC_MOUNT ok=0 err=%s\n", usb.lastErrorName());
        tested = true;
        return;
    }
    Serial.println("MSC_MOUNT ok=1 path=/usb");

    DIR *root = opendir("/usb");
    if (!root)
    {
        Serial.println("MSC_OPENDIR ok=0 path=/usb");
        usb.mscUnmount("/usb");
        tested = true;
        return;
    }
    Serial.println("MSC_OPENDIR ok=1 path=/usb");
    uint8_t entryCount = 0;
    char firstFilePath[128] = {};
    while (entryCount < 5)
    {
        dirent *entry = readdir(root);
        if (!entry)
        {
            break;
        }
        Serial.printf("MSC_ROOT_ENTRY name=%s type=%u\n", entry->d_name, entry->d_type);
        if (firstFilePath[0] == '\0' &&
            strcmp(entry->d_name, ".") != 0 &&
            strcmp(entry->d_name, "..") != 0)
        {
            char candidate[128] = {};
            snprintf(candidate, sizeof(candidate), "/usb/%s", entry->d_name);
            struct stat st = {};
            if (stat(candidate, &st) == 0 && S_ISREG(st.st_mode))
            {
                strncpy(firstFilePath, candidate, sizeof(firstFilePath) - 1);
            }
        }
        entryCount++;
    }
    closedir(root);
    Serial.printf("MSC_ROOT_ENTRY_COUNT count=%u\n", entryCount);

    if (firstFilePath[0] != '\0')
    {
        FILE *file = fopen(firstFilePath, "rb");
        if (!file)
        {
            Serial.printf("MSC_FILE_READ ok=0 path=%s\n", firstFilePath);
            usb.mscUnmount("/usb");
            tested = true;
            return;
        }
        uint8_t bytes[16] = {};
        const size_t readBytes = fread(bytes, 1, sizeof(bytes), file);
        fclose(file);
        Serial.printf("MSC_FILE_READ ok=1 path=%s bytes=%u b0=%02x\n",
                      firstFilePath,
                      static_cast<unsigned>(readBytes),
                      readBytes > 0 ? bytes[0] : 0);
    }
    else
    {
        Serial.println("MSC_FILE_READ skipped=no_regular_file");
    }

    const char *writePath = "/usb/ESPUSBHT.TST";
    const uint8_t writeData[] = "EspUsbHost MSC write test\n";
    FILE *writeFile = fopen(writePath, "wb");
    if (!writeFile)
    {
        Serial.printf("MSC_FILE_WRITE ok=0 path=%s\n", writePath);
        usb.mscUnmount("/usb");
        tested = true;
        return;
    }
    const size_t written = fwrite(writeData, 1, sizeof(writeData) - 1, writeFile);
    const int closeWrite = fclose(writeFile);
    Serial.printf("MSC_FILE_WRITE ok=%u path=%s bytes=%u\n",
                  (written == sizeof(writeData) - 1 && closeWrite == 0) ? 1 : 0,
                  writePath,
                  static_cast<unsigned>(written));
    if (written != sizeof(writeData) - 1 || closeWrite != 0)
    {
        usb.mscUnmount("/usb");
        tested = true;
        return;
    }

    FILE *readBackFile = fopen(writePath, "rb");
    if (!readBackFile)
    {
        Serial.printf("MSC_FILE_READBACK ok=0 path=%s\n", writePath);
        usb.mscUnmount("/usb");
        tested = true;
        return;
    }
    uint8_t readBack[sizeof(writeData)] = {};
    const size_t readBackBytes = fread(readBack, 1, sizeof(readBack), readBackFile);
    fclose(readBackFile);
    const bool readBackOk = readBackBytes == sizeof(writeData) - 1 &&
                            memcmp(readBack, writeData, sizeof(writeData) - 1) == 0;
    Serial.printf("MSC_FILE_READBACK ok=%u path=%s bytes=%u\n",
                  readBackOk ? 1 : 0,
                  writePath,
                  static_cast<unsigned>(readBackBytes));
    if (!readBackOk)
    {
        usb.mscUnmount("/usb");
        tested = true;
        return;
    }

    const int removeResult = remove(writePath);
    Serial.printf("MSC_FILE_REMOVE ok=%u path=%s\n", removeResult == 0 ? 1 : 0, writePath);
    if (removeResult != 0)
    {
        usb.mscUnmount("/usb");
        tested = true;
        return;
    }

    const bool duplicateMountOk = usb.mscMount("/usb");
    Serial.printf("MSC_MOUNT_DUPLICATE ok=%u path=/usb\n", duplicateMountOk ? 1 : 0);
    if (duplicateMountOk)
    {
        usb.mscUnmount("/usb");
        tested = true;
        return;
    }

    const bool unmountOk = usb.mscUnmount("/usb");
    Serial.printf("MSC_UNMOUNT ok=%u path=/usb\n", unmountOk ? 1 : 0);
    if (!unmountOk)
    {
        tested = true;
        return;
    }

    if (!usb.mscMount("/usb"))
    {
        Serial.printf("MSC_REMOUNT ok=0 err=%s\n", usb.lastErrorName());
        tested = true;
        return;
    }
    Serial.println("MSC_REMOUNT ok=1 path=/usb");
    const bool finalUnmountOk = usb.mscUnmount("/usb");
    Serial.printf("MSC_FINAL_UNMOUNT ok=%u path=/usb\n", finalUnmountOk ? 1 : 0);
    if (!finalUnmountOk)
    {
        tested = true;
        return;
    }

    if (!usbMscFs.begin(usb, "/usbfs"))
    {
        Serial.printf("MSC_FS_BEGIN ok=0 err=%s\n", usb.lastErrorName());
        tested = true;
        return;
    }
    Serial.printf("MSC_FS_BEGIN ok=1 mounted=%u base=%s\n",
                  usbMscFs.mounted() ? 1 : 0,
                  usbMscFs.basePath());

    const char *fsPath = "/ESPUSBFS.TST";
    File fsWrite = usbMscFs.open(fsPath, FILE_WRITE);
    if (!fsWrite)
    {
        Serial.printf("MSC_FS_WRITE ok=0 path=%s\n", fsPath);
        usbMscFs.end();
        tested = true;
        return;
    }
    const char *fsMessage = "EspUsbHost MSC FS test\n";
    const size_t fsWritten = fsWrite.print(fsMessage);
    fsWrite.close();
    Serial.printf("MSC_FS_WRITE ok=%u path=%s bytes=%u\n",
                  fsWritten == strlen(fsMessage) ? 1 : 0,
                  fsPath,
                  static_cast<unsigned>(fsWritten));
    if (fsWritten != strlen(fsMessage))
    {
        usbMscFs.end();
        tested = true;
        return;
    }

    File fsRead = usbMscFs.open(fsPath, FILE_READ);
    if (!fsRead)
    {
        Serial.printf("MSC_FS_READ ok=0 path=%s\n", fsPath);
        usbMscFs.end();
        tested = true;
        return;
    }
    char fsReadBack[32] = {};
    const size_t fsReadBytes = fsRead.readBytes(fsReadBack, strlen(fsMessage));
    fsRead.close();
    const bool fsReadOk = fsReadBytes == strlen(fsMessage) &&
                          memcmp(fsReadBack, fsMessage, strlen(fsMessage)) == 0;
    Serial.printf("MSC_FS_READ ok=%u path=%s bytes=%u\n",
                  fsReadOk ? 1 : 0,
                  fsPath,
                  static_cast<unsigned>(fsReadBytes));
    if (!fsReadOk)
    {
        usbMscFs.end();
        tested = true;
        return;
    }

    const bool fsRemoveOk = usbMscFs.remove(fsPath);
    Serial.printf("MSC_FS_REMOVE ok=%u path=%s\n", fsRemoveOk ? 1 : 0, fsPath);
    usbMscFs.end();
    Serial.printf("MSC_FS_END mounted=%u\n", usbMscFs.mounted() ? 1 : 0);
    if (!fsRemoveOk || usbMscFs.mounted())
    {
        tested = true;
        return;
    }

    Serial.println("[PASS]");
    tested = true;
}
