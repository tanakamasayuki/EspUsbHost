#include <Arduino.h>

#if ARDUINO_USB_MODE
void setup() {}
void loop() {}
#else

#include "USB.h"
#include "USBMSC.h"
#include "diskio_impl.h"
#include "ff.h"

USBMSC MSC;

// 128 KB is enough for a FAT12 volume with one sector per cluster and leaves
// the sketch well inside internal RAM.
static constexpr uint32_t BLOCK_COUNT = 256;
static constexpr uint16_t BLOCK_SIZE = 512;
static uint8_t disk[BLOCK_COUNT][BLOCK_SIZE];
static bool volumeFormatted = false;

static constexpr char PEER_FILE_NAME[] = "PEER.TXT";
static constexpr char PEER_FILE_BODY[] = "MSC_FAT_PEER";

static int32_t onRead(uint32_t lba, uint32_t offset, void *buffer, uint32_t bufsize)
{
    if (lba >= BLOCK_COUNT || offset >= BLOCK_SIZE)
    {
        return -1;
    }
    uint8_t *out = static_cast<uint8_t *>(buffer);
    uint32_t remaining = bufsize;
    uint32_t currentLba = lba;
    uint32_t currentOffset = offset;
    while (remaining > 0)
    {
        if (currentLba >= BLOCK_COUNT)
        {
            return -1;
        }
        const uint32_t chunk = min<uint32_t>(remaining, BLOCK_SIZE - currentOffset);
        memcpy(out, disk[currentLba] + currentOffset, chunk);
        out += chunk;
        remaining -= chunk;
        currentLba++;
        currentOffset = 0;
    }
    return bufsize;
}

static int32_t onWrite(uint32_t lba, uint32_t offset, uint8_t *buffer, uint32_t bufsize)
{
    if (lba >= BLOCK_COUNT || offset >= BLOCK_SIZE)
    {
        return -1;
    }
    uint32_t remaining = bufsize;
    uint32_t currentLba = lba;
    uint32_t currentOffset = offset;
    while (remaining > 0)
    {
        if (currentLba >= BLOCK_COUNT)
        {
            return -1;
        }
        const uint32_t chunk = min<uint32_t>(remaining, BLOCK_SIZE - currentOffset);
        memcpy(disk[currentLba] + currentOffset, buffer, chunk);
        buffer += chunk;
        remaining -= chunk;
        currentLba++;
        currentOffset = 0;
    }
    return bufsize;
}

static bool onStartStop(uint8_t power_condition, bool start, bool load_eject)
{
    (void)power_condition;
    (void)start;
    (void)load_eject;
    return true;
}

// FatFs disk driver over the RAM disk above. It exists only so the peer can
// format and populate the volume with the same FatFs implementation the host
// mounts it with; it is unregistered again before USB comes up.
static DSTATUS ramDiskInitialize(BYTE pdrv)
{
    (void)pdrv;
    return 0;
}

static DSTATUS ramDiskStatus(BYTE pdrv)
{
    (void)pdrv;
    return 0;
}

static DRESULT ramDiskRead(BYTE pdrv, BYTE *buff, uint32_t sector, unsigned count)
{
    (void)pdrv;
    if (sector + count > BLOCK_COUNT)
    {
        return RES_PARERR;
    }
    memcpy(buff, disk[sector], static_cast<size_t>(count) * BLOCK_SIZE);
    return RES_OK;
}

static DRESULT ramDiskWrite(BYTE pdrv, const BYTE *buff, uint32_t sector, unsigned count)
{
    (void)pdrv;
    if (sector + count > BLOCK_COUNT)
    {
        return RES_PARERR;
    }
    memcpy(disk[sector], buff, static_cast<size_t>(count) * BLOCK_SIZE);
    return RES_OK;
}

static DRESULT ramDiskIoctl(BYTE pdrv, BYTE cmd, void *buff)
{
    (void)pdrv;
    switch (cmd)
    {
    case CTRL_SYNC:
        return RES_OK;
    case GET_SECTOR_COUNT:
        *static_cast<LBA_t *>(buff) = BLOCK_COUNT;
        return RES_OK;
    case GET_SECTOR_SIZE:
        *static_cast<WORD *>(buff) = BLOCK_SIZE;
        return RES_OK;
    case GET_BLOCK_SIZE:
        *static_cast<DWORD *>(buff) = 1;
        return RES_OK;
    default:
        return RES_PARERR;
    }
}

static const ff_diskio_impl_t RAM_DISKIO = {
    .init = &ramDiskInitialize,
    .status = &ramDiskStatus,
    .read = &ramDiskRead,
    .write = &ramDiskWrite,
    .ioctl = &ramDiskIoctl,
};

static bool prepareVolume()
{
    BYTE pdrv = FF_DRV_NOT_USED;
    if (ff_diskio_get_drive(&pdrv) != ESP_OK || pdrv == FF_DRV_NOT_USED)
    {
        return false;
    }
    ff_diskio_register(pdrv, &RAM_DISKIO);

    char drive[3] = {static_cast<char>('0' + pdrv), ':', '\0'};
    static uint8_t work[FF_MAX_SS];
    MKFS_PARM opt = {};
    opt.fmt = FM_FAT | FM_SFD;  // FAT12/16 with no partition table
    opt.n_fat = 1;
    opt.align = 1;
    opt.n_root = 16;
    opt.au_size = BLOCK_SIZE;  // one sector per cluster

    bool ok = false;
    static FATFS fs;
    if (f_mkfs(drive, &opt, work, sizeof(work)) == FR_OK &&
        f_mount(&fs, drive, 1) == FR_OK)
    {
        char path[32] = {};
        snprintf(path, sizeof(path), "%s%s", drive, PEER_FILE_NAME);
        FIL file;
        if (f_open(&file, path, FA_CREATE_ALWAYS | FA_WRITE) == FR_OK)
        {
            UINT written = 0;
            ok = f_write(&file, PEER_FILE_BODY, sizeof(PEER_FILE_BODY) - 1, &written) == FR_OK &&
                 written == sizeof(PEER_FILE_BODY) - 1;
            f_close(&file);
        }
        f_mount(nullptr, drive, 0);
    }
    ff_diskio_unregister(pdrv);
    return ok;
}

void setup()
{
    Serial.begin(115200);

    memset(disk, 0, sizeof(disk));
    volumeFormatted = prepareVolume();

    MSC.vendorID("ESP32");
    MSC.productID("MSC_FAT");
    MSC.productRevision("1.0");
    MSC.onStartStop(onStartStop);
    MSC.onRead(onRead);
    MSC.onWrite(onWrite);
    MSC.mediaPresent(true);
    MSC.isWritable(true);
    MSC.begin(BLOCK_COUNT, BLOCK_SIZE);
    USB.begin();
    Serial.printf("DEVICE_MSC_FAT_READY formatted=%u blocks=%lu\n",
                  volumeFormatted ? 1 : 0,
                  static_cast<unsigned long>(BLOCK_COUNT));
}

void loop()
{
    if (Serial.available() > 0)
    {
        const char command = Serial.read();
        if (command == 's')
        {
            // The boot banner is printed while the board is still being
            // flashed, so a test cannot rely on it being in its own log.
            Serial.printf("DEVICE_FAT_STATUS formatted=%u blocks=%lu block_size=%u\n",
                          volumeFormatted ? 1 : 0,
                          static_cast<unsigned long>(BLOCK_COUNT),
                          BLOCK_SIZE);
        }
        else if (command == 'z')
        {
            // Rebooting detaches USB and re-attaches after boot, which is the
            // only way this board can hand the host a real disconnect. The
            // core has no USB detach API.
            Serial.println("DEVICE_REBOOT");
            Serial.flush();
            delay(50);
            ESP.restart();
        }
    }
    delay(1);
}

#endif
