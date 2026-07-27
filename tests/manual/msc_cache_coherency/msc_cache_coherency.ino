// Reproduces CPU-cache / USB-DMA incoherency on MSC bulk IN transfers.
//
// The same static LBA range is read twice with different transfer shapes while
// a background task thrashes the data cache. The media never changes, so any
// difference between the two reads is corruption. On ESP32-P4 the corrupted
// bytes are expected to line up with the 64-byte L1 cache line.

#include "EspUsbHost.h"

#include <esp_heap_caps.h>

EspUsbHost usb;

static constexpr uint32_t TEST_LBA = 0;
static constexpr uint32_t CHUNK_BLOCKS = 64; // 32 KiB per multi-sector read
static constexpr uint32_t ITERATIONS = 64;
static constexpr size_t CACHE_THRASH_BYTES = 96 * 1024;

static bool tested = false;
static uint8_t *reference = nullptr;
static uint8_t *readback = nullptr;
static volatile bool thrashRun = false;

// Keeps the data cache busy so dirty lines belonging to the in-flight USB DMA
// buffer are actually evicted while the controller is writing to it.
static void cacheThrashTask(void *arg)
{
    uint8_t *scratch = static_cast<uint8_t *>(arg);
    uint8_t pattern = 0;
    while (true)
    {
        if (thrashRun)
        {
            for (size_t i = 0; i < CACHE_THRASH_BYTES; i += 4)
            {
                scratch[i] = pattern;
            }
            pattern++;
        }
        else
        {
            vTaskDelay(1);
        }
        taskYIELD();
    }
}

static uint32_t compareBlocks(const uint8_t *a, const uint8_t *b, size_t length,
                              size_t &firstOffset, uint32_t &alignedTo64)
{
    uint32_t diffBytes = 0;
    bool first = true;
    size_t runStart = 0;
    bool inRun = false;
    for (size_t i = 0; i < length; i++)
    {
        if (a[i] == b[i])
        {
            inRun = false;
            continue;
        }
        diffBytes++;
        if (first)
        {
            firstOffset = i;
            first = false;
        }
        if (!inRun)
        {
            inRun = true;
            runStart = i;
            if ((runStart % 64) == 0)
            {
                alignedTo64++;
            }
            Serial.printf("MSC_CACHE_DIFF offset=%u lba_off=%u in_block=%u ref=%02x got=%02x aligned64=%u\n",
                          static_cast<unsigned>(runStart),
                          static_cast<unsigned>(runStart / 512),
                          static_cast<unsigned>(runStart % 512),
                          a[runStart],
                          b[runStart],
                          (runStart % 64) == 0 ? 1 : 0);
        }
    }
    return diffBytes;
}

void setup()
{
    Serial.begin(115200);
    delay(5000);

    usb.onDeviceConnected([](const EspUsbHostDeviceInfo &device)
                          { Serial.printf("MSC_DEVICE addr=%u vid=%04x pid=%04x product=%s\n",
                                          device.address, device.vid, device.pid, device.product); });

    if (!usb.begin())
    {
        Serial.printf("MSC_BEGIN_FAIL %s\n", usb.lastErrorName());
        return;
    }

    const size_t chunkBytes = CHUNK_BLOCKS * 512;
    reference = static_cast<uint8_t *>(heap_caps_malloc(chunkBytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    readback = static_cast<uint8_t *>(heap_caps_malloc(chunkBytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    uint8_t *scratch = static_cast<uint8_t *>(heap_caps_malloc(CACHE_THRASH_BYTES, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    if (!reference || !readback || !scratch)
    {
        Serial.println("MSC_CACHE_ALLOC_FAIL");
        return;
    }
    xTaskCreate(cacheThrashTask, "cache_thrash", 2048, scratch, 1, nullptr);

    Serial.println("MSC_CACHE_READY");
}

void loop()
{
    if (tested || !usb.mscReady())
    {
        delay(10);
        return;
    }

    if (!usb.mscWaitReady(ESP_USB_HOST_ANY_ADDRESS, 5000, 1000))
    {
        Serial.println("MSC_CACHE_NOT_READY");
        delay(1000);
        return;
    }

    uint32_t blockCount = 0;
    uint32_t blockSize = 0;
    if (!usb.mscCapacity(blockCount, blockSize) || blockSize != 512 || blockCount < TEST_LBA + CHUNK_BLOCKS)
    {
        Serial.printf("MSC_CACHE_UNSUPPORTED blocks=%lu block_size=%lu\n",
                      static_cast<unsigned long>(blockCount),
                      static_cast<unsigned long>(blockSize));
        tested = true;
        return;
    }

    const size_t chunkBytes = CHUNK_BLOCKS * 512;

    // Reference: read one sector at a time, three times, and require all three
    // passes to agree. A single-sector transfer moves 512 bytes per DMA, so it
    // is the least exposed shape; agreement across three passes means the media
    // content is stable and the reference is trustworthy.
    thrashRun = false;
    for (uint8_t pass = 0; pass < 3; pass++)
    {
        for (uint32_t i = 0; i < CHUNK_BLOCKS; i++)
        {
            uint8_t sector[512] = {};
            if (!usb.mscReadBlocks(TEST_LBA + i, sector, 1))
            {
                Serial.printf("MSC_CACHE_REF_READ_FAIL lba=%lu %s\n",
                              static_cast<unsigned long>(TEST_LBA + i), usb.lastErrorName());
                tested = true;
                return;
            }
            if (pass == 0)
            {
                memcpy(reference + i * 512, sector, 512);
            }
            else if (memcmp(reference + i * 512, sector, 512) != 0)
            {
                Serial.printf("MSC_CACHE_REF_UNSTABLE pass=%u lba=%lu\n",
                              pass, static_cast<unsigned long>(TEST_LBA + i));
                tested = true;
                return;
            }
        }
    }
    Serial.printf("MSC_CACHE_REFERENCE ok=1 lba=%lu blocks=%lu bytes=%u\n",
                  static_cast<unsigned long>(TEST_LBA),
                  static_cast<unsigned long>(CHUNK_BLOCKS),
                  static_cast<unsigned>(chunkBytes));

    // Repeatedly re-read the same range as one large multi-sector transfer
    // while the cache is under pressure.
    thrashRun = true;
    uint32_t badIterations = 0;
    uint32_t totalDiffBytes = 0;
    uint32_t alignedTo64 = 0;
    size_t firstOffset = 0;
    for (uint32_t iteration = 0; iteration < ITERATIONS; iteration++)
    {
        memset(readback, 0xa5, chunkBytes);
        if (!usb.mscReadBlocks(TEST_LBA, readback, CHUNK_BLOCKS))
        {
            Serial.printf("MSC_CACHE_READ_FAIL iteration=%lu %s\n",
                          static_cast<unsigned long>(iteration), usb.lastErrorName());
            tested = true;
            return;
        }
        size_t offset = 0;
        const uint32_t diffBytes = compareBlocks(reference, readback, chunkBytes, offset, alignedTo64);
        if (diffBytes > 0)
        {
            if (badIterations == 0)
            {
                firstOffset = offset;
            }
            badIterations++;
            totalDiffBytes += diffBytes;
            Serial.printf("MSC_CACHE_MISMATCH iteration=%lu diff_bytes=%lu first_offset=%u\n",
                          static_cast<unsigned long>(iteration),
                          static_cast<unsigned long>(diffBytes),
                          static_cast<unsigned>(offset));
        }
    }
    thrashRun = false;

    Serial.printf("MSC_CACHE_RESULT iterations=%lu bad_iterations=%lu diff_bytes=%lu runs_aligned64=%lu first_offset=%u\n",
                  static_cast<unsigned long>(ITERATIONS),
                  static_cast<unsigned long>(badIterations),
                  static_cast<unsigned long>(totalDiffBytes),
                  static_cast<unsigned long>(alignedTo64),
                  static_cast<unsigned>(firstOffset));
    if (badIterations == 0)
    {
        Serial.println("[PASS]");
    }
    else
    {
        Serial.println("[FAIL]");
    }
    tested = true;
}
