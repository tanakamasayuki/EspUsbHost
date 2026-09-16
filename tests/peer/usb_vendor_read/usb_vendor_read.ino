#include "EspUsbHost.h"

#include <esp_timer.h>

// Bulk IN reception: the per-transfer size vendorOpen() sets up, and the
// asynchronous read queue on top of it.
//
// The peer streams a 0..255 ramp on request, so every check here measures the
// same thing in three shapes -- one packet per transfer, one large transfer, and
// several large transfers in flight -- and verifies that the bytes arrive intact
// whichever shape carries them.

EspUsbHost usb;

// See tests/peer/usb_vendor for why the lifecycle commands are upper case and
// why the host is not started in setup().
static bool hostStarted = false;

static void startHost()
{
  if (hostStarted)
  {
    return; // idempotent
  }
  if (!usb.begin())
  {
    Serial.printf("HOST_BEGIN_FAILED %s\n", usb.lastErrorName());
    return;
  }
  hostStarted = true;
}

static void stopHost()
{
  usb.end();
  hostStarted = false;
  Serial.println("HOST_STATE idle devices=0");
}

static bool handleLifecycle(char command)
{
  if (command == 'Q')
  {
    Serial.printf("HOST_STATE %s devices=%u\n",
                  hostStarted ? "running" : "idle",
                  static_cast<unsigned>(usb.deviceCount()));
    return true;
  }
  if (command == 'G')
  {
    startHost();
    return true;
  }
  if (command == 'H')
  {
    stopHost();
    return true;
  }
  return false;
}

static constexpr uint16_t VENDOR_VID = 0x303a;
static constexpr uint16_t VENDOR_PID = 0x4019;
static constexpr size_t STREAM_BYTES = 128 * 1024;
static constexpr size_t BIG_TRANSFER_BYTES = 8192;
static constexpr uint32_t STREAM_TIMEOUT_MS = 30000;

// Window-continuity check. The stream is far larger than the receive ring, so
// the ring overflows continuously while vendorRead() drains it in small
// windows. Bytes lost between one read and the next are expected -- the ring
// only keeps the newest ring-full. What must never happen is a seam inside a
// single window, because the overflow path advances the tail the reader is
// walking. Run for a fixed time rather than to a byte count: most of the stream
// is dropped by design, so a byte target would never be reached.
static constexpr size_t WINDOW_STREAM_BYTES = 4 * 1024 * 1024;
static constexpr uint32_t WINDOW_RUN_MS = 8000;

static volatile bool connected = false;
static uint8_t deviceAddress = 0;

// Written from the USB client task inside onVendorData(), read from loop()
// while it waits for the stream to finish.
static volatile size_t streamBytes = 0;
static volatile uint32_t streamChunks = 0;
static volatile size_t streamMaxChunk = 0;
static volatile uint32_t streamBad = 0;
static uint8_t streamExpected = 0;
static volatile bool streamArmed = false;

static void resetStream()
{
  // Disarm, then let anything still on the wire land and be discarded before
  // counting starts. A stream that timed out leaves the peer mid-send, and those
  // bytes would otherwise be checked against a ramp that has been restarted.
  streamArmed = false;
  delay(150);
  streamBytes = 0;
  streamChunks = 0;
  streamMaxChunk = 0;
  streamBad = 0;
  streamExpected = 0;
  streamArmed = true;
}

// Ask the peer for `bytes` bytes, then wait for them. Returns the elapsed time
// in microseconds, or 0 when the stream did not finish in time.
static uint64_t runStream(size_t bytes)
{
  resetStream();
  const uint8_t request[5] = {'S',
                              static_cast<uint8_t>(bytes & 0xff),
                              static_cast<uint8_t>((bytes >> 8) & 0xff),
                              static_cast<uint8_t>((bytes >> 16) & 0xff),
                              static_cast<uint8_t>((bytes >> 24) & 0xff)};
  const int64_t startedAt = esp_timer_get_time();
  if (!usb.vendorWrite(request, sizeof(request), deviceAddress))
  {
    return 0;
  }
  const uint32_t deadline = millis() + STREAM_TIMEOUT_MS;
  while (streamBytes < bytes && millis() < deadline)
  {
    delay(1);
  }
  const int64_t finishedAt = esp_timer_get_time();
  streamArmed = false;
  return streamBytes >= bytes ? static_cast<uint64_t>(finishedAt - startedAt) : 0;
}

static void reportStream(const char *mode, size_t requested, uint64_t elapsedUs)
{
  const double seconds = static_cast<double>(elapsedUs) / 1000000.0;
  const double mbps = seconds > 0.0 ? (static_cast<double>(streamBytes) / seconds) / 1048576.0 : 0.0;
  const EspUsbHostVendorReadStats stats = usb.vendorReadStats(deviceAddress);
  Serial.printf("VENDOR_STREAM mode=%s requested=%u bytes=%u chunks=%lu max_chunk=%u bad=%lu "
                "elapsed_us=%llu mbps=%.3f submitted=%lu completed=%lu errors=%lu short=%lu "
                "starved=%lu queue_bytes=%llu\n",
                mode,
                static_cast<unsigned>(requested),
                static_cast<unsigned>(streamBytes),
                static_cast<unsigned long>(streamChunks),
                static_cast<unsigned>(streamMaxChunk),
                static_cast<unsigned long>(streamBad),
                static_cast<unsigned long long>(elapsedUs),
                mbps,
                static_cast<unsigned long>(stats.submitted),
                static_cast<unsigned long>(stats.completed),
                static_cast<unsigned long>(stats.errors),
                static_cast<unsigned long>(stats.shortTransfers),
                static_cast<unsigned long>(stats.starved),
                static_cast<unsigned long long>(stats.bytes));
}

void setup()
{
  Serial.setTxBufferSize(4096);
  Serial.begin(115200);
  delay(500);

  usb.onDeviceConnected([](const EspUsbHostDeviceInfo &device)
                        {
                          Serial.printf("HOST_CONNECTED address=%u vid=%04x pid=%04x\n",
                                        device.address, device.vid, device.pid);
                          if (device.vid == VENDOR_VID && device.pid == VENDOR_PID)
                          {
                            deviceAddress = device.address;
                            connected = true;
                          }
                        });

  usb.onDeviceDisconnected([](const EspUsbHostDeviceInfo &device)
                           {
                             Serial.printf("HOST_DISCONNECTED address=%u\n", device.address);
                             if (connected && device.address == deviceAddress)
                             {
                               connected = false;
                               deviceAddress = 0;
                             }
                           });

  // The ramp is checked here rather than from a copy in loop(): the data pointer
  // is only valid during the callback, and copying 128 KB to check it later would
  // measure the copy instead of the transfers.
  usb.onVendorData([](const EspUsbHostVendorData &data)
                   {
                     if (!streamArmed)
                     {
                       return;
                     }
                     uint32_t bad = 0;
                     for (size_t i = 0; i < data.length; i++)
                     {
                       if (data.data[i] != streamExpected)
                       {
                         bad++;
                         streamExpected = data.data[i];
                       }
                       streamExpected++;
                     }
                     streamBad += bad;
                     streamBytes += data.length;
                     streamChunks++;
                     if (data.length > streamMaxChunk)
                     {
                       streamMaxChunk = data.length;
                     }
                   });

  Serial.println("HOST_STATE idle devices=0");
}

void loop()
{
  if (Serial.available() > 0)
  {
    const char command = Serial.read();
    if (handleLifecycle(command))
    {
      return;
    }
    if (command == 'i')
    {
      Serial.printf("VENDOR_INFO connected=%u in_ep=0x%02x in_mps=%u xfer=%u queue=%u pending=%u\n",
                    connected ? 1 : 0,
                    usb.vendorInEndpoint(deviceAddress),
                    usb.vendorInPacketSize(deviceAddress),
                    static_cast<unsigned>(usb.vendorInTransferBytes(deviceAddress)),
                    usb.vendorReadQueueReady(deviceAddress) ? 1 : 0,
                    static_cast<unsigned>(usb.vendorReadPending(deviceAddress)));
    }
    else if (command == 'o')
    {
      // Default reads: one endpoint-sized packet per transfer.
      const bool ok = usb.vendorOpen(deviceAddress);
      Serial.printf("VENDOR_OPEN ok=%u xfer=%u mps=%u\n",
                    ok ? 1 : 0,
                    static_cast<unsigned>(usb.vendorInTransferBytes(deviceAddress)),
                    usb.vendorInPacketSize(deviceAddress));
    }
    else if (command == 'O')
    {
      // Same interface, but one large transfer instead of one packet.
      const bool ok = usb.vendorOpen(deviceAddress, 0xff, ESP_USB_HOST_VENDOR_READ_CONTINUOUS,
                                     BIG_TRANSFER_BYTES);
      Serial.printf("VENDOR_OPEN_BIG ok=%u xfer=%u\n",
                    ok ? 1 : 0,
                    static_cast<unsigned>(usb.vendorInTransferBytes(deviceAddress)));
    }
    else if (command == 'C')
    {
      // Re-opening with a different size has to fail rather than silently keep
      // the size the outstanding transfer was allocated with.
      const bool ok = usb.vendorOpen(deviceAddress, 0xff, ESP_USB_HOST_VENDOR_READ_CONTINUOUS, 2048);
      Serial.printf("VENDOR_OPEN_CONFLICT ok=%u xfer=%u error=%d\n",
                    ok ? 1 : 0,
                    static_cast<unsigned>(usb.vendorInTransferBytes(deviceAddress)),
                    usb.lastError());
    }
    else if (command == 'q')
    {
      const bool ok = usb.vendorReadQueueBegin(2, BIG_TRANSFER_BYTES, deviceAddress);
      Serial.printf("VENDOR_RQ_BEGIN ok=%u ready=%u xfer=%u pending=%u\n",
                    ok ? 1 : 0,
                    usb.vendorReadQueueReady(deviceAddress) ? 1 : 0,
                    static_cast<unsigned>(usb.vendorInTransferBytes(deviceAddress)),
                    static_cast<unsigned>(usb.vendorReadPending(deviceAddress)));
    }
    else if (command == 'Z')
    {
      // Invalid shapes must be refused before anything is allocated.
      const bool depthZero = usb.vendorReadQueueBegin(0, BIG_TRANSFER_BYTES, deviceAddress);
      const bool tooDeep = usb.vendorReadQueueBegin(ESP_USB_HOST_VENDOR_READ_QUEUE_MAX_DEPTH + 1,
                                                    BIG_TRANSFER_BYTES, deviceAddress);
      const bool noBytes = usb.vendorReadQueueBegin(2, 0, deviceAddress);
      Serial.printf("VENDOR_RQ_REJECT depth0=%u deep=%u bytes0=%u\n",
                    depthZero ? 1 : 0, tooDeep ? 1 : 0, noBytes ? 1 : 0);
    }
    else if (command == 'e')
    {
      usb.vendorReadQueueEnd(deviceAddress);
      Serial.printf("VENDOR_RQ_END ready=%u pending=%u\n",
                    usb.vendorReadQueueReady(deviceAddress) ? 1 : 0,
                    static_cast<unsigned>(usb.vendorReadPending(deviceAddress)));
    }
    else if (command == 'S')
    {
      // Sync reads must be refused while the queue owns the endpoint.
      uint8_t buffer[64] = {};
      size_t length = 0;
      const bool ok = usb.vendorReadSync(buffer, sizeof(buffer), &length, 200, deviceAddress);
      Serial.printf("VENDOR_READ_SYNC ok=%u error=%d\n", ok ? 1 : 0, usb.lastError());
    }
    else if (command == 'n')
    {
      usb.vendorReadStatsReset(deviceAddress);
      const uint64_t elapsed = runStream(STREAM_BYTES);
      reportStream(usb.vendorReadQueueReady(deviceAddress) ? "queue" : "continuous",
                   STREAM_BYTES, elapsed);
    }
    else if (command == 'W')
    {
      resetStream();
      const uint8_t request[5] = {'S',
                                  static_cast<uint8_t>(WINDOW_STREAM_BYTES & 0xff),
                                  static_cast<uint8_t>((WINDOW_STREAM_BYTES >> 8) & 0xff),
                                  static_cast<uint8_t>((WINDOW_STREAM_BYTES >> 16) & 0xff),
                                  static_cast<uint8_t>((WINDOW_STREAM_BYTES >> 24) & 0xff)};
      uint32_t windows = 0;
      uint32_t seams = 0;
      uint32_t bytes = 0;
      usb.vendorReadStatsReset(deviceAddress);
      if (usb.vendorWrite(request, sizeof(request), deviceAddress))
      {
        // A small window, and a pause between reads. Both halves matter, and
        // getting either wrong hides the fault:
        //
        //   Reading in a tight loop keeps the ring near empty, and the overflow
        //   path only runs when it is full, so the code under test never runs.
        //
        //   Reading a whole ring at once empties it as the copy proceeds, so a
        //   push arriving midway finds free space, takes no drop, and moves no
        //   tail -- the seam cannot form however long the copy is.
        //
        // A short read from a ring the pause keeps full is the case that bites:
        // the ring stays full for the whole copy, so any push landing inside it
        // must discard, and discarding walks the tail this loop is reading.
        uint8_t window[63] = {};
        const uint32_t deadline = millis() + WINDOW_RUN_MS;
        while (millis() < deadline)
        {
          delay(2);
          const size_t got = usb.vendorRead(window, sizeof(window), deviceAddress);
          if (got < 2)
          {
            continue;
          }
          windows++;
          bytes += got;
          // The peer sends an unbroken 0..255 ramp, so every byte inside one
          // window must be one more than the byte before it.
          for (size_t i = 1; i < got; i++)
          {
            if (window[i] != static_cast<uint8_t>(window[i - 1] + 1))
            {
              seams++;
            }
          }
        }
      }
      streamArmed = false;
      // Report the push size the ring actually saw. onVendorData() fires once
      // per push, so streamChunks is the push count and streamBytes/streamChunks
      // is their average size -- which is the variable this check depends on,
      // and the one the block-mode request is trying to move. (vendorReadStats()
      // is no use here: it counts the asynchronous read queue, not this path.)
      Serial.printf("VENDOR_WINDOW_PUSH pushes=%lu bytes=%lu max=%lu bad=%lu\n",
                    static_cast<unsigned long>(streamChunks),
                    static_cast<unsigned long>(streamBytes),
                    static_cast<unsigned long>(streamMaxChunk),
                    static_cast<unsigned long>(streamBad));
      Serial.printf("VENDOR_WINDOW windows=%lu bytes=%lu seams=%lu\n",
                    static_cast<unsigned long>(windows),
                    static_cast<unsigned long>(bytes),
                    static_cast<unsigned long>(seams));
    }
    else if (command == 'x')
    {
      usb.end();
      connected = false;
      deviceAddress = 0;
      Serial.printf("HOST_REBEGIN %u\n", usb.begin() ? 1 : 0);
    }
  }
  delay(1);
}
