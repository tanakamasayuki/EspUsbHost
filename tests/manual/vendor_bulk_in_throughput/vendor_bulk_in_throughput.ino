#include "EspUsbHost.h"

#include <esp_timer.h>

// Measure the effective bulk IN throughput of the vendor API: the continuous
// read vendorOpen() sets up, against the asynchronous read queue over transfer
// sizes and queue depths.
//
// The continuous read keeps exactly one transfer outstanding and submits the
// next one from the client task after the completion has been handled, so the
// endpoint carries nothing for that turnaround. The queue keeps several
// transfers outstanding and resubmits each from its own completion. Which of the
// two effects -- transfer size or in-flight count -- actually moves the number
// is what this sweep answers, and the answer differs per controller.
//
// The peer must stream on request: flash tests/peer/usb_vendor_read/peer_device
// to the device board. It answers 'S' + a 4-byte little-endian length with that
// many bytes of a 0..255 ramp, so this side can also verify that nothing was
// lost between transfers.

static constexpr uint32_t TEST_TIMEOUT_MS = 60000;
static constexpr uint8_t VENDOR_CLASS = 0xff;
static constexpr size_t BYTES_PER_CONDITION = 1024 * 1024;
static constexpr uint32_t STREAM_TIMEOUT_MS = 30000;

static const size_t TRANSFER_SIZES[] = {512, 2048, 8192, 16384, 32768};
static const size_t QUEUE_DEPTHS[] = {1, 2, 4};

static EspUsbHost usb;
static bool reported = false;
static uint8_t deviceAddress = 0;

static volatile size_t streamBytes = 0;
static volatile uint32_t streamChunks = 0;
static volatile size_t streamMaxChunk = 0;
static volatile uint32_t streamBad = 0;
static volatile bool streamArmed = false;

struct Result
{
    const char *mode;
    size_t depth;
    size_t transferSize;
    size_t bytes;
    uint32_t chunks;
    size_t maxChunk;
    uint32_t bad;
    uint64_t elapsedUs;
    EspUsbHostVendorReadStats stats;
};

static void printResult(const Result &r)
{
    const double seconds = static_cast<double>(r.elapsedUs) / 1000000.0;
    // Decimal MB/s (10^6), as in vendor_bulk_throughput: the same unit as the
    // bus ceilings these numbers are read against.
    const double mbps = seconds > 0.0 ? (static_cast<double>(r.bytes) / seconds) / 1000000.0 : 0.0;
    // Mean bytes per completed transfer: what the device actually managed to put
    // into each one, as opposed to the size that was asked for.
    const double perTransfer = r.stats.completed ? static_cast<double>(r.stats.bytes) / r.stats.completed : 0.0;
    Serial.printf("VENDOR_BULK_IN_THROUGHPUT mode=%s depth=%u xfer=%u bytes=%u chunks=%lu "
                  "max_chunk=%u bad=%lu elapsed_us=%llu mbps=%.3f completed=%lu errors=%lu "
                  "short=%lu starved=%lu per_transfer=%.1f\n",
                  r.mode,
                  static_cast<unsigned>(r.depth),
                  static_cast<unsigned>(r.transferSize),
                  static_cast<unsigned>(r.bytes),
                  static_cast<unsigned long>(r.chunks),
                  static_cast<unsigned>(r.maxChunk),
                  static_cast<unsigned long>(r.bad),
                  static_cast<unsigned long long>(r.elapsedUs),
                  mbps,
                  static_cast<unsigned long>(r.stats.completed),
                  static_cast<unsigned long>(r.stats.errors),
                  static_cast<unsigned long>(r.stats.shortTransfers),
                  static_cast<unsigned long>(r.stats.starved),
                  perTransfer);
}

// Ask the peer for BYTES_PER_CONDITION bytes and wait for them to arrive.
static bool runStream(const char *mode, size_t depth, size_t transferSize, Result &out)
{
    // Disarm, then let anything still on the wire land and be discarded before
    // counting starts: a condition that timed out leaves the peer mid-send.
    streamArmed = false;
    delay(150);
    streamBytes = 0;
    streamChunks = 0;
    streamMaxChunk = 0;
    streamBad = 0;
    usb.vendorReadStatsReset(deviceAddress);
    streamArmed = true;

    const size_t bytes = BYTES_PER_CONDITION;
    const uint8_t request[5] = {'S',
                                static_cast<uint8_t>(bytes & 0xff),
                                static_cast<uint8_t>((bytes >> 8) & 0xff),
                                static_cast<uint8_t>((bytes >> 16) & 0xff),
                                static_cast<uint8_t>((bytes >> 24) & 0xff)};

    const int64_t startedAt = esp_timer_get_time();
    const bool wrote = usb.vendorWrite(request, sizeof(request), deviceAddress);
    const uint32_t deadline = millis() + STREAM_TIMEOUT_MS;
    while (wrote && streamBytes < bytes && millis() < deadline)
    {
        delay(1);
    }
    const int64_t finishedAt = esp_timer_get_time();
    streamArmed = false;

    out = Result{mode, depth, transferSize, streamBytes, streamChunks, streamMaxChunk, streamBad,
                 static_cast<uint64_t>(finishedAt - startedAt), usb.vendorReadStats(deviceAddress)};
    return wrote && streamBytes >= bytes && streamBad == 0;
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
    deviceAddress = address;
    Serial.printf("VENDOR_TARGET address=%u\n", address);
    if (!usb.vendorOpen(address))
    {
        Serial.printf("[FAIL] vendorOpen() failed: last_error=%d\n", usb.lastError());
        return;
    }
    Serial.printf("VENDOR_OPEN ok=1 in_ep=0x%02x in_mps=%u xfer=%u\n",
                  usb.vendorInEndpoint(address),
                  usb.vendorInPacketSize(address),
                  static_cast<unsigned>(usb.vendorInTransferBytes(address)));

    bool ok = true;
    Result result;

    // Baseline: the shape every release before this one had, one max-size packet
    // per transfer with the client task in the turnaround.
    if (!runStream("continuous", 1, usb.vendorInTransferBytes(address), result))
    {
        ok = false;
    }
    printResult(result);

    for (size_t depth : QUEUE_DEPTHS)
    {
        for (size_t transferSize : TRANSFER_SIZES)
        {
            if (!usb.vendorReadQueueBegin(depth, transferSize, address))
            {
                Serial.printf("VENDOR_BULK_IN_QUEUE_BEGIN_FAIL depth=%u xfer=%u last_error=%d\n",
                              static_cast<unsigned>(depth), static_cast<unsigned>(transferSize),
                              usb.lastError());
                ok = false;
                continue;
            }
            if (!runStream("queue", depth, transferSize, result))
            {
                ok = false;
            }
            printResult(result);
            usb.vendorReadQueueEnd(address);
            if (usb.vendorReadQueueReady(address))
            {
                Serial.println("VENDOR_CHECK the queue should not be ready after end()");
                ok = false;
            }
        }
    }

    // The endpoint is idle after the last end(), and re-opening has to bring the
    // continuous read back rather than leave it silent.
    if (!usb.vendorOpen(address))
    {
        Serial.println("VENDOR_CHECK vendorOpen() should reopen after the queue ended");
        ok = false;
    }
    else if (!runStream("continuous-again", 1, usb.vendorInTransferBytes(address), result))
    {
        ok = false;
        printResult(result);
    }
    else
    {
        printResult(result);
    }

    Serial.println(ok ? "[PASS]" : "[FAIL]");
}

void setup()
{
    Serial.begin(115200);
    delay(5000);
    Serial.println("vendor_bulk_in_throughput test start");
    Serial.println("Connect a device streaming on a vendor-specific (0xff) bulk IN endpoint.");

    usb.onDeviceConnected([](const EspUsbHostDeviceInfo &device) {
        Serial.printf("connected address=%u vid=%04x pid=%04x\n",
                      device.address, device.vid, device.pid);
    });

    // This callback runs on the USB client task, between transfers, so what it
    // costs is subtracted from what is being measured. A byte-at-a-time compare
    // of the ramp is around a third of a 360 MHz core at 24 MB/s, which would be
    // measuring the check rather than the bus.
    //
    // One byte per chunk is enough here. The ramp is continuous across the whole
    // stream, so the first byte of every chunk must be (bytes so far) & 0xff: a
    // reordered or partly lost transfer breaks that, and a whole lost transfer
    // also stops the stream from ever reaching its length, which is reported as
    // bytes < requested. The full byte-for-byte check lives in the peer test
    // tests/peer/usb_vendor_read, where throughput is not what is being measured.
    usb.onVendorData([](const EspUsbHostVendorData &data) {
        if (!streamArmed || data.length == 0)
        {
            return;
        }
        if (data.data[0] != static_cast<uint8_t>(streamBytes & 0xff))
        {
            streamBad++;
        }
        streamBytes += data.length;
        streamChunks++;
        if (data.length > streamMaxChunk)
        {
            streamMaxChunk = data.length;
        }
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
