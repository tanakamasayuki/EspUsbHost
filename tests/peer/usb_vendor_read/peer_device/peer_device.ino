#include "EspUsbDevice.h"

// Stream source for the host-side bulk IN tests. The host asks for a byte count
// and this sends exactly that many bytes of a 0..255 ramp, which lets the host
// prove that nothing was lost or reordered by the transfers underneath.
//
// The ramp continues across the whole stream rather than restarting per chunk:
// a host that drops one transfer then sees the sequence step, which a per-chunk
// pattern would hide.

EspUsbDevice device;
EspUsbDeviceVendor Vendor(device);

// How much is handed to write() at a time. Taken from writeCapacity() in setup()
// rather than fixed here: a constant smaller than the transmit FIFO caps the
// transfer size no matter how deep the FIFO is built, which makes this sketch --
// not the host under test -- the thing that limits throughput. Measured against
// an ESP32-P4 host, a fixed 512 held the link to 8.5 MB/s where the FIFO-sized
// chunk reaches 24.45 MB/s.
static constexpr size_t CHUNK_MAX = 8192;
static size_t chunkSize = 512;

static volatile uint32_t rxCount = 0;
static volatile uint32_t streamRequests = 0;
static size_t streamRemaining = 0;
static uint8_t streamNext = 0;
static uint8_t chunkBuffer[CHUNK_MAX];

// 'S' + 4 bytes little-endian length. Anything else is echoed, so the same peer
// still answers the plain loopback checks.
static void handleCommand(const uint8_t *data, size_t length)
{
  if (length >= 5 && data[0] == 'S')
  {
    const uint32_t bytes = static_cast<uint32_t>(data[1]) |
                           (static_cast<uint32_t>(data[2]) << 8) |
                           (static_cast<uint32_t>(data[3]) << 16) |
                           (static_cast<uint32_t>(data[4]) << 24);
    streamRemaining = bytes;
    streamNext = 0;
    streamRequests++;
    return;
  }
  Vendor.write(reinterpret_cast<const uint8_t *>("echo:"), 5);
  Vendor.write(data, length);
  Vendor.flush();
}

static void processVendorRx()
{
  size_t available = Vendor.available();
  uint8_t buffer[64];
  while (available > 0)
  {
    const size_t chunk = Vendor.read(buffer, min(available, sizeof(buffer)));
    if (chunk == 0)
    {
      break;
    }
    rxCount += chunk;
    handleCommand(buffer, chunk);
    available = Vendor.available();
  }
}

// Hand over one FIFO's worth per call and return to loop(): waitWritable()
// deschedules this task while the transmit FIFO drains instead of spinning on a
// write() that returns 0.
static void processStream()
{
  while (streamRemaining > 0)
  {
    const size_t want = streamRemaining < chunkSize ? streamRemaining : chunkSize;
    if (!Vendor.waitWritable(want, 100))
    {
      return;
    }
    for (size_t i = 0; i < want; i++)
    {
      chunkBuffer[i] = streamNext++;
    }
    const size_t written = Vendor.write(chunkBuffer, want);
    // No flush per chunk: flushing forces whatever is in the FIFO out as its own
    // transfer, so flushing every write produces one transfer per chunk and the
    // host sees a short packet each time. The stream is flushed once, below,
    // when the whole requested length has been handed over.
    streamRemaining -= written;
    if (written < want)
    {
      // The FIFO took less than it promised: rewind the ramp so the next pass
      // continues where the host's byte stream actually stopped.
      streamNext = static_cast<uint8_t>(streamNext - (want - written));
      return;
    }
  }
  Vendor.flush(); // once, when the requested length has been handed over
}

void setup()
{
  Serial.begin(115200);
  delay(500);

  Vendor.onRx([](size_t)
              { processVendorRx(); });

  chunkSize = EspUsbDeviceVendor::writeCapacity();
  if (chunkSize > CHUNK_MAX)
  {
    chunkSize = CHUNK_MAX;
  }
  Serial.printf("DEVICE_CHUNK %u\n", static_cast<unsigned>(chunkSize));

  EspUsbDeviceConfig config;
  config.vid = 0x303a;
  config.pid = 0x4019;
  config.manufacturer = "EspUsbDevice";
  config.product = "EspUsbDevice USB Vendor";
  config.serialNumber = "espusb-usb-vendor-read";
  Serial.printf("DEVICE_BEGIN %u\n", device.begin(config) ? 1 : 0);
#if defined(CONFIG_IDF_TARGET_ESP32P4)
  // Which bulk IN endpoints actually came up with a two-packet transmit FIFO.
  // Read back after begin(), because EspUsbBulkInBuffering::Auto decides it from
  // the DFIFO budget and quietly leaves one packet when it does not fit. That
  // matters for the throughput sweep: a one-packet device holds this link near
  // 24 MB/s, which is easy to misread as the host's own ceiling. The released
  // library and the working tree both report version "2.3.0", so this value --
  // not the version -- is what says which one is on the board. P4 only: the
  // accessor does not exist in the release the S3 profile pins.
  Serial.printf("DEVICE_BULK_IN_DOUBLE_BUFFERED 0x%04x\n",
                static_cast<unsigned>(device.bulkInDoubleBuffered()));
#endif
}

void loop()
{
  processVendorRx();
  processStream();
  if (Serial.available() > 0)
  {
    const char command = Serial.read();
    if (command == '?')
    {
      Serial.println("DEVICE_READY");
    }
    else if (command == 's')
    {
      Serial.printf("DEVICE_STATUS rx=%lu streams=%lu remaining=%lu\n",
                    static_cast<unsigned long>(rxCount),
                    static_cast<unsigned long>(streamRequests),
                    static_cast<unsigned long>(streamRemaining));
    }
  }
}
