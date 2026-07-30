#include "EspUsbHost.h"

#include <esp_timer.h>

// Measure the effective bulk OUT throughput of the vendor API: the synchronous
// vendorWrite() baseline against the asynchronous queue at several depths and
// transfer sizes. vendorWrite() waits for each transfer to complete, so the bus
// goes idle between transfers; the queue keeps several in flight instead.
//
// The payload is filler, not a protocol: every byte is 0xAF, which is a no-op
// padding byte for USB graphics adapters and harmless to a vendor loopback
// device. Nothing is read back, so this measures the host-to-device direction
// only.

static constexpr uint32_t TEST_TIMEOUT_MS = 60000;
static constexpr uint8_t VENDOR_CLASS = 0xff;
static constexpr uint8_t FILLER_BYTE = 0xaf;

// Bytes pushed per condition. 256 KB keeps a full-speed run near one second
// while staying long enough to average out scheduling noise.
static constexpr size_t BYTES_PER_CONDITION = 256 * 1024;
static constexpr uint32_t FLUSH_TIMEOUT_MS = 5000;

static const size_t TRANSFER_SIZES[] = {512, 2048, 8192, 16384};
static const size_t QUEUE_DEPTHS[] = {1, 2, 4, 8};

static EspUsbHost usb;
static bool reported = false;
static uint8_t filler[16384];

struct Result
{
    const char *mode;
    size_t depth;
    size_t transferSize;
    size_t bytes;
    uint64_t elapsedUs;
    uint32_t submitFails;
    uint32_t errors;
    uint32_t queueFull;
    uint32_t queueEmptySamples;
    uint32_t samples;
};

static void printResult(const Result &r)
{
    const double seconds = static_cast<double>(r.elapsedUs) / 1000000.0;
    const double mbps = seconds > 0.0 ? (static_cast<double>(r.bytes) / seconds) / 1048576.0 : 0.0;
    const unsigned emptyPct = r.samples ? (r.queueEmptySamples * 100u) / r.samples : 0u;
    Serial.printf("VENDOR_BULK_THROUGHPUT mode=%s depth=%u xfer=%u bytes=%u elapsed_us=%llu "
                  "mbps=%.3f submit_fail=%u errors=%u queue_full=%u queue_empty_pct=%u\n",
                  r.mode,
                  static_cast<unsigned>(r.depth),
                  static_cast<unsigned>(r.transferSize),
                  static_cast<unsigned>(r.bytes),
                  static_cast<unsigned long long>(r.elapsedUs),
                  mbps,
                  r.submitFails,
                  r.errors,
                  r.queueFull,
                  emptyPct);
}

// Synchronous baseline: one transfer at a time, each waited to completion.
static bool runSync(uint8_t address, size_t transferSize, Result &out)
{
    const size_t count = BYTES_PER_CONDITION / transferSize;
    uint32_t submitFails = 0;
    const int64_t startedAt = esp_timer_get_time();
    for (size_t i = 0; i < count; i++)
    {
        if (!usb.vendorWrite(filler, transferSize, address))
        {
            submitFails++;
            if (submitFails > 4)
            {
                break;
            }
        }
    }
    const int64_t finishedAt = esp_timer_get_time();

    out = Result{"sync", 1, transferSize, count * transferSize,
                 static_cast<uint64_t>(finishedAt - startedAt), submitFails, 0, 0, 0, 0};
    return submitFails <= 4;
}

// Asynchronous queue: acquire a pooled DMA buffer, fill it, submit, repeat. The
// queue-empty sampling tells whether the USB pipe or the producer is the limit.
static bool runAsync(uint8_t address, size_t depth, size_t transferSize, Result &out)
{
    if (!usb.vendorWriteQueueBegin(depth, transferSize, address))
    {
        Serial.printf("VENDOR_BULK_QUEUE_BEGIN_FAIL depth=%u xfer=%u last_error=%d\n",
                      static_cast<unsigned>(depth), static_cast<unsigned>(transferSize),
                      usb.lastError());
        return false;
    }
    usb.vendorWriteStatsReset(address);

    const size_t count = BYTES_PER_CONDITION / transferSize;
    uint32_t submitFails = 0;
    uint32_t queueEmptySamples = 0;
    uint32_t samples = 0;
    const int64_t startedAt = esp_timer_get_time();
    for (size_t i = 0; i < count; i++)
    {
        if (usb.vendorWritePending(address) == 0)
        {
            queueEmptySamples++;
        }
        samples++;

        size_t capacity = 0;
        uint8_t *buffer = usb.vendorWriteAcquire(&capacity, FLUSH_TIMEOUT_MS, address);
        if (!buffer)
        {
            submitFails++;
            if (submitFails > 4)
            {
                break;
            }
            continue;
        }
        const size_t length = transferSize < capacity ? transferSize : capacity;
        memset(buffer, FILLER_BYTE, length);
        if (!usb.vendorWriteSubmit(buffer, length, address))
        {
            usb.vendorWriteRelease(buffer, address);
            submitFails++;
            if (submitFails > 4)
            {
                break;
            }
        }
    }
    const bool flushed = usb.vendorWriteFlush(FLUSH_TIMEOUT_MS, address);
    const int64_t finishedAt = esp_timer_get_time();

    const EspUsbHostVendorWriteStats stats = usb.vendorWriteStats(address);
    out = Result{"async", depth, transferSize, static_cast<size_t>(stats.bytes),
                 static_cast<uint64_t>(finishedAt - startedAt), submitFails, stats.errors,
                 stats.queueFullEvents, queueEmptySamples, samples};

    if (!flushed)
    {
        Serial.printf("VENDOR_BULK_FLUSH_TIMEOUT depth=%u xfer=%u pending=%u\n",
                      static_cast<unsigned>(depth), static_cast<unsigned>(transferSize),
                      static_cast<unsigned>(usb.vendorWritePending(address)));
    }
    usb.vendorWriteQueueEnd(address);
    return flushed && submitFails <= 4;
}

static uint8_t findVendorDeviceAddress()
{
    EspUsbHostDeviceInfo devices[ESP_USB_HOST_MAX_DEVICES];
    const size_t deviceCount = usb.getDevices(devices, ESP_USB_HOST_MAX_DEVICES);
    for (size_t i = 0; i < deviceCount; i++)
    {
        EspUsbHostInterfaceInfo interfaces[ESP_USB_HOST_MAX_INTERFACES];
        const size_t interfaceCount = usb.getInterfaces(devices[i].address, interfaces,
                                                        ESP_USB_HOST_MAX_INTERFACES);
        for (size_t j = 0; j < interfaceCount; j++)
        {
            if (interfaces[j].interfaceClass == VENDOR_CLASS)
            {
                return devices[i].address;
            }
        }
    }
    return 0;
}

static void runTest(uint8_t address)
{
    Serial.printf("VENDOR_TARGET address=%u\n", address);
    if (!usb.vendorOpen(address))
    {
        Serial.printf("[FAIL] vendorOpen() failed: last_error=%d\n", usb.lastError());
        return;
    }
    Serial.printf("VENDOR_OPEN ok=1 out_ep=0x%02x out_mps=%u\n",
                  usb.vendorOutEndpoint(address), usb.vendorOutPacketSize(address));

    bool ok = true;
    Result result;

    for (size_t transferSize : TRANSFER_SIZES)
    {
        if (!runSync(address, transferSize, result))
        {
            ok = false;
        }
        printResult(result);
    }

    for (size_t depth : QUEUE_DEPTHS)
    {
        for (size_t transferSize : TRANSFER_SIZES)
        {
            if (!runAsync(address, depth, transferSize, result))
            {
                ok = false;
            }
            printResult(result);
        }
    }

    // The queue must be reusable after a full begin/end cycle.
    if (!usb.vendorWriteQueueBegin(2, 1024, address))
    {
        Serial.println("VENDOR_CHECK queue could not be reopened after end()");
        ok = false;
    }
    else
    {
        if (usb.vendorWriteQueueFree(address) != 2 || usb.vendorWritePending(address) != 0)
        {
            Serial.println("VENDOR_CHECK a fresh queue should report all slots free");
            ok = false;
        }
        // Acquire without submitting, then release: the slot must come back.
        size_t capacity = 0;
        uint8_t *buffer = usb.vendorWriteAcquire(&capacity, 100, address);
        if (!buffer || capacity != 1024 || usb.vendorWriteQueueFree(address) != 1)
        {
            Serial.println("VENDOR_CHECK acquire should take exactly one slot");
            ok = false;
        }
        if (buffer)
        {
            usb.vendorWriteRelease(buffer, address);
        }
        if (usb.vendorWriteQueueFree(address) != 2)
        {
            Serial.println("VENDOR_CHECK release should return the slot");
            ok = false;
        }
        // Oversized submits must fail instead of being silently split.
        if (usb.vendorWriteAsync(filler, 2048, address))
        {
            Serial.println("VENDOR_CHECK vendorWriteAsync() should reject an oversized payload");
            ok = false;
        }
        usb.vendorWriteQueueEnd(address);
    }

    if (usb.vendorWriteQueueReady(address))
    {
        Serial.println("VENDOR_CHECK the queue should not be ready after end()");
        ok = false;
    }
    if (usb.vendorWriteAcquire(nullptr, 0, address) != nullptr)
    {
        Serial.println("VENDOR_CHECK acquire should fail without a queue");
        ok = false;
    }

    Serial.println(ok ? "[PASS]" : "[FAIL]");
}

void setup()
{
    Serial.begin(115200);
    delay(5000);
    Serial.println("vendor_bulk_throughput test start");
    Serial.println("Connect a device with a vendor-specific (0xff) bulk OUT interface.");
    memset(filler, FILLER_BYTE, sizeof(filler));

    usb.onDeviceConnected([](const EspUsbHostDeviceInfo &device) {
        Serial.printf("connected address=%u vid=%04x pid=%04x\n",
                      device.address, device.vid, device.pid);
    });

    usb.begin();
}

void loop()
{
    if (!reported)
    {
        const uint8_t address = findVendorDeviceAddress();
        if (address != 0)
        {
            reported = true;
            runTest(address);
        }
        else if (millis() > TEST_TIMEOUT_MS)
        {
            reported = true;
            Serial.println("[FAIL] no vendor-specific (0xff) interface found before the timeout");
        }
    }

    delay(10);
}
