#include "EspUsbDevice.h"

// Device half of the bulk IN throughput sweep, on EspUsbDevice's direct transfer
// path: writeDirect() arms one transfer on this sketch's own memory instead of
// copying through the class transmit FIFO, and onTxComplete() arms the next from
// the completion. That is what keeps the stream gapless.
//
// The buffered pairing lives in tests/peer/usb_vendor_read/peer_device and is
// the correctness peer; this one exists to measure a rate, and the two cannot
// share a sketch because the direct path is selected by a build_opt.h flag that
// reaches every profile of the sketch it sits beside.
//
// In a direct build the class FIFOs are gone: available() and read() return 0
// and flush() does nothing, so the host's 'S' command arrives through onRxData()
// rather than being polled from loop().

EspUsbDevice device;
EspUsbDeviceVendor Vendor(device);

// One block per transfer. 64-byte aligned and in internal RAM because
// writeDirect() refuses anything else rather than quietly copying it, and a
// multiple of 256 so the 0..255 ramp the host verifies runs unbroken across
// block boundaries.
static constexpr size_t BLOCK_BYTES = 32768;
static uint8_t blockBuffer[BLOCK_BYTES] __attribute__((aligned(64)));

static volatile size_t streamRemaining = 0;
static volatile uint32_t armFailures = 0;

static void armNext()
{
  const size_t remaining = streamRemaining;
  if (remaining == 0)
  {
    return;
  }
  const size_t want = remaining < BLOCK_BYTES ? remaining : BLOCK_BYTES;
  if (!Vendor.writeDirect(blockBuffer, want))
  {
    armFailures++;
  }
}

void setup()
{
  Serial.begin(115200);
  delay(500);

  for (size_t i = 0; i < BLOCK_BYTES; i++)
  {
    blockBuffer[i] = static_cast<uint8_t>(i);
  }

  // Says whether the build flag actually took. The library keeps the buffered
  // behaviour silently when it did not, and the only other symptom is a
  // throughput number that looks like the buffered path.
  Serial.printf("DEVICE_DIRECT %u\n", EspUsbDeviceVendor::directWriteSupported() ? 1 : 0);
  Serial.printf("DEVICE_CHUNK %u\n", static_cast<unsigned>(BLOCK_BYTES));

  Vendor.onRxData([](const uint8_t *data, size_t length)
                  {
                    if (length >= 5 && data[0] == 'S')
                    {
                      const uint32_t bytes = static_cast<uint32_t>(data[1]) |
                                             (static_cast<uint32_t>(data[2]) << 8) |
                                             (static_cast<uint32_t>(data[3]) << 16) |
                                             (static_cast<uint32_t>(data[4]) << 24);
                      streamRemaining = bytes;
                      armFailures = 0;
                      armNext();
                    }
                  });

  // Arming the next transfer from the completion is the whole point: the
  // endpoint never goes idle between blocks.
  Vendor.onTxComplete([](size_t sent)
                      {
                        const size_t remaining = streamRemaining;
                        streamRemaining = sent >= remaining ? 0 : remaining - sent;
                        armNext();
                      });

  EspUsbDeviceConfig config;
  config.vid = 0x303a;
  config.pid = 0x4019;
  config.manufacturer = "EspUsbDevice";
  config.product = "EspUsbDevice USB Vendor";
  config.serialNumber = "espusb-usb-vendor-read";
  Serial.printf("DEVICE_BEGIN %u\n", device.begin(config) ? 1 : 0);
#if defined(CONFIG_IDF_TARGET_ESP32P4)
  Serial.printf("DEVICE_BULK_IN_DOUBLE_BUFFERED 0x%04x\n",
                static_cast<unsigned>(device.bulkInDoubleBuffered()));
#endif
}

void loop()
{
  if (Serial.available() > 0 && Serial.read() == '?')
  {
    Serial.printf("DEVICE_READY arm_failures=%lu\n", static_cast<unsigned long>(armFailures));
  }
  delay(1);
}
